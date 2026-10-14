#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

#include "ulibc.h"

// Форматирование прав доступа в стиле UNIX (drwxr-xr-x)
static void format_perms(uint32_t mode, uint8_t type, char* out) {
    out[0] = (type == TYPE_DIR) ? 'd' : '-';
    
    if (mode > 0) {
        out[1] = (mode & 0400) ? 'r' : '-';
        out[2] = (mode & 0200) ? 'w' : '-';
        out[3] = (mode & 0100) ? 'x' : '-';
        out[4] = (mode & 0040) ? 'r' : '-';
        out[5] = (mode & 0020) ? 'w' : '-';
        out[6] = (mode & 0010) ? 'x' : '-';
        out[7] = (mode & 0004) ? 'r' : '-';
        out[8] = (mode & 0002) ? 'w' : '-';
        out[9] = (mode & 0001) ? 'x' : '-';
    } else {
        // Дефолтные права, если в файловой системе права не заданы
        if (type == TYPE_DIR) {
            u_strncpy(out + 1, "rwxr-xr-x", 10);
        } else {
            u_strncpy(out + 1, "rw-r--r--", 10);
        }
    }
    out[10] = '\0';
}

// Форматирование размера (в байтах или Human-readable K/M)
static void format_size(uint32_t bytes, int human, char* out, uint32_t max_len) {
    if (!human) {
        u_format_num(bytes, out, max_len);
        return;
    }

    if (bytes < 1024) {
        u_format_num(bytes, out, max_len - 2);
        int l = u_strlen(out);
        out[l] = 'B';
        out[l + 1] = '\0';
    } else if (bytes < 1024 * 1024) {
        u_format_num(bytes / 1024, out, max_len - 2);
        int l = u_strlen(out);
        out[l] = 'K';
        out[l + 1] = '\0';
    } else {
        u_format_num(bytes / (1024 * 1024), out, max_len - 2);
        int l = u_strlen(out);
        out[l] = 'M';
        out[l + 1] = '\0';
    }
}

// Печать размера с выравниванием вправо
static void print_padded(const char* s, int width) {
    int len = u_strlen(s);
    for (int i = 0; i < width - len; i++) {
        print(" ");
    }
    print(s);
    print(" ");
}

// Вывод одного элемента
static void print_entry(const char* name, uint8_t type, uint32_t size, uint32_t mode, 
                        int flag_l, int flag_h, int flag_F, int flag_1) {
    if (flag_l) {
        char perms[12];
        format_perms(mode, type, perms);
        print("\033[37m"); // Темно-серый для прав
        print(perms);
        print("\033[0m ");

        char sbuf[16];
        format_size(size, flag_h, sbuf, sizeof(sbuf));
        print_padded(sbuf, 8);
    }

    // Раскраска: Папки — яркий синий/голубой, Файлы — белый
    if (type == TYPE_DIR) {
        print("\033[1;96m"); // Яркий циан для директорий
        print(name);
        if (flag_F || flag_l) print("/");
        print("\033[0m");
    } else {
        print("\033[97m"); // Обычный белый для файлов
        print(name);
        print("\033[0m");
    }

    if (flag_l || flag_1) {
        print("\n");
    } else {
        print("  ");
    }
}

int main(int argc, char* argv[]) {
    int flag_l = 0;
    int flag_a = 0;
    int flag_A = 0;
    int flag_h = 0;
    int flag_1 = 0;
    int flag_F = 0;

    char target[256];
    target[0] = '\0';

    // 1. Парсинг аргументов и флагов
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                char c = argv[i][j];
                if (c == 'l') flag_l = 1;
                else if (c == 'a') flag_a = 1;
                else if (c == 'A') flag_A = 1;
                else if (c == 'h') flag_h = 1;
                else if (c == '1') flag_1 = 1;
                else if (c == 'F') flag_F = 1;
                else {
                    print("ls: invalid option -- '");
                    putchar(c);
                    print("'\nUsage: ls [-laAh1F] [path]\n");
                    return 1;
                }
            }
        } else {
            if (target[0] == '\0') {
                u_strncpy(target, argv[i], sizeof(target));
            }
        }
    }

    // Если путь не задан — берем текущую директорию (pwd)
    if (target[0] == '\0') {
        if (getcwd(target, sizeof(target)) != 0) {
            u_strncpy(target, "/", sizeof(target));
        }
    }

    // 2. Проверяем, может передан путь к одиночному файлу, а не к папке
    stat_t target_st;
    if (stat(target, &target_st) == 0 && target_st.type == TYPE_FILE) {
        print_entry(target, target_st.type, target_st.size, target_st.permissions, 
                    flag_l, flag_h, flag_F, 1);
        return 0;
    }

    // 3. Открываем директорию
    int fd = open(target, READ);
    if (fd < 0) {
        print("ls: cannot access '");
        print(target);
        print("': No such file or directory\n");
        return 1;
    }

    dirent_t d;
    int idx = 0;
    int count = 0;

    while (readdir(fd, idx++, &d) == 0) {
        if (d.name[0] == '\0') break;

        // Фильтрация скрытых файлов (начинающихся с точки)
        if (!flag_a && !flag_A) {
            if (d.name[0] == '.') continue;
        } else if (flag_A && !flag_a) {
            if (u_strcmp(d.name, ".") == 0 || u_strcmp(d.name, "..") == 0) continue;
        }

        // Если включен подробный режим (-l), запрашиваем права через stat
        uint32_t perms = 0;
        uint32_t fsize = d.size;
        if (flag_l) {
            char full_item_path[256];
            u_path_join(full_item_path, target, d.name, sizeof(full_item_path));
            stat_t item_st;
            if (stat(full_item_path, &item_st) == 0) {
                perms = item_st.permissions;
                fsize = item_st.size;
            }
        }

        print_entry(d.name, d.type, fsize, perms, flag_l, flag_h, flag_F, flag_1);
        count++;
    }

    close(fd);

    // Добавляем финальный перенос строки для обычного режима (в одну строку)
    if (!flag_l && !flag_1 && count > 0) {
        print("\n");
    }

    return 0;
}