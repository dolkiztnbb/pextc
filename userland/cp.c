#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

#include "ulibc.h"

// Получение имени файла/папки из пути (basename)
static void get_basename(const char* path, char* out, uint32_t max_len) {
    if (!path || max_len == 0) return;
    int len = u_strlen(path);
    while (len > 1 && path[len - 1] == '/') len--; // Пропускаем завершающие слэши
    
    int start = len - 1;
    while (start >= 0 && path[start] != '/') start--;
    start++; // Сдвиг за слэш

    int k = 0;
    for (int i = start; i < len && k < (int)max_len - 1; i++) {
        out[k++] = path[i];
    }
    out[k] = '\0';
}

// Побайтовое копирование одного файла
static int copy_file(const char* src, const char* dst) {
    int in_fd = open(src, READ | BINARY);
    if (in_fd < 0) {
        print("cp: cannot open '"); print(src); print("'\n");
        return -1;
    }

    int out_fd = open(dst, WRITE | CREATE | TRUNCATE | BINARY);
    if (out_fd < 0) {
        print("cp: cannot create '"); print(dst); print("'\n");
        close(in_fd);
        return -1;
    }

    char buf[512];
    int n;
    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        if (write(out_fd, buf, n) != n) {
            print("cp: write error to '"); print(dst); print("'\n");
            close(in_fd);
            close(out_fd);
            return -1;
        }
    }

    close(in_fd);
    close(out_fd);
    return 0;
}

// Рекурсивное копирование (файлы + папки)
static int copy_recursive(const char* src, const char* dst) {
    stat_t st;
    if (stat(src, &st) != 0) {
        print("cp: cannot stat '"); print(src); print("': No such file or directory\n");
        return -1;
    }

    if (st.type == TYPE_FILE) {
        return copy_file(src, dst);
    }

    if (st.type == TYPE_DIR) {
        // Создаем целевую директорию
        mkdir(dst);

        int fd = open(src, READ);
        if (fd < 0) {
            print("cp: cannot open directory '"); print(src); print("'\n");
            return -1;
        }

        dirent_t d;
        int idx = 0;
        while (readdir(fd, idx++, &d) == 0) {
            if (d.name[0] == '\0') break;
            if (u_strcmp(d.name, ".") == 0 || u_strcmp(d.name, "..") == 0) continue;

            char sub_src[256];
            char sub_dst[256];
            u_path_join(sub_src, src, d.name, sizeof(sub_src));
            u_path_join(sub_dst, dst, d.name, sizeof(sub_dst));

            copy_recursive(sub_src, sub_dst);
        }
        close(fd);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    int flag_r = 0;
    char src[256] = {0};
    char dst[256] = {0};

    // Парсинг аргументов и флагов
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r' || argv[i][j] == 'R') {
                    flag_r = 1;
                } else {
                    print("cp: invalid option -- '"); putchar(argv[i][j]); print("'\n");
                    print("Usage: cp [-r] <source> <dest>\n");
                    return 1;
                }
            }
        } else {
            if (src[0] == '\0') {
                u_strncpy(src, argv[i], sizeof(src));
            } else if (dst[0] == '\0') {
                u_strncpy(dst, argv[i], sizeof(dst));
            }
        }
    }

    if (src[0] == '\0' || dst[0] == '\0') {
        print("Usage: cp [-r] <source> <dest>\n");
        return 1;
    }

    stat_t src_st;
    if (stat(src, &src_st) != 0) {
        print("cp: cannot stat '"); print(src); print("': No such file or directory\n");
        return 1;
    }

    // Защита: нельзя копировать директорию без -r
    if (src_st.type == TYPE_DIR && !flag_r) {
        print("cp: -r not specified; omitting directory '"); print(src); print("'\n");
        return 1;
    }

    // Определение финального пути назначения
    char actual_dst[256];
    stat_t dst_st;

    // Если целевой путь — это уже существующая папка: копируем ВНУТРЬ неё
    if (stat(dst, &dst_st) == 0 && dst_st.type == TYPE_DIR) {
        char base[64];
        get_basename(src, base, sizeof(base));
        u_path_join(actual_dst, dst, base, sizeof(actual_dst));
    } else {
        u_strncpy(actual_dst, dst, sizeof(actual_dst));
    }

    // Защита от копирования самого в себя
    if (u_strcmp(src, actual_dst) == 0) {
        print("cp: '"); print(src); print("' and '"); print(actual_dst); print("' are the same file\n");
        return 1;
    }

    return (copy_recursive(src, actual_dst) == 0) ? 0 : 1;
}