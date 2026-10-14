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
    while (len > 1 && path[len - 1] == '/') len--;
    
    int start = len - 1;
    while (start >= 0 && path[start] != '/') start--;
    start++;

    int k = 0;
    for (int i = start; i < len && k < (int)max_len - 1; i++) {
        out[k++] = path[i];
    }
    out[k] = '\0';
}

// Побайтовое копирование
static int copy_file(const char* src, const char* dst) {
    int in_fd = open(src, READ | BINARY);
    if (in_fd < 0) return -1;

    int out_fd = open(dst, WRITE | CREATE | TRUNCATE | BINARY);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    char buf[512];
    int n;
    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        if (write(out_fd, buf, n) != n) {
            close(in_fd);
            close(out_fd);
            return -1;
        }
    }

    close(in_fd);
    close(out_fd);
    return 0;
}

// Рекурсивное копирование дерева
static int copy_recursive(const char* src, const char* dst) {
    stat_t st;
    if (stat(src, &st) != 0) return -1;

    if (st.type == TYPE_FILE) {
        return copy_file(src, dst);
    }

    if (st.type == TYPE_DIR) {
        mkdir(dst);
        int fd = open(src, READ);
        if (fd < 0) return -1;

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

// Рекурсивное удаление источника после успешного копирования
static int remove_recursive(const char* path) {
    stat_t st;
    if (stat(path, &st) != 0) return -1;

    if (st.type == TYPE_FILE) {
        return unlink(path);
    }

    if (st.type == TYPE_DIR) {
        int fd = open(path, READ);
        if (fd >= 0) {
            dirent_t d;
            int idx = 0;
            while (readdir(fd, idx++, &d) == 0) {
                if (d.name[0] == '\0') break;
                if (u_strcmp(d.name, ".") == 0 || u_strcmp(d.name, "..") == 0) continue;

                char sub[256];
                u_path_join(sub, path, d.name, sizeof(sub));
                remove_recursive(sub);
            }
            close(fd);
        }
        // Удаляем саму директорию, когда она уже опустела
        return unlink(path);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print("Usage: mv <source> <dest>\n");
        return 1;
    }

    const char* src = argv[1];
    const char* dst = argv[2];

    stat_t src_st;
    if (stat(src, &src_st) != 0) {
        print("mv: cannot stat '"); print(src); print("': No such file or directory\n");
        return 1;
    }

    char actual_dst[256];
    stat_t dst_st;

    // Если назначение — существующая папка, перемещаем внутрь
    if (stat(dst, &dst_st) == 0 && dst_st.type == TYPE_DIR) {
        char base[64];
        get_basename(src, base, sizeof(base));
        u_path_join(actual_dst, dst, base, sizeof(actual_dst));
    } else {
        u_strncpy(actual_dst, dst, sizeof(actual_dst));
    }

    if (u_strcmp(src, actual_dst) == 0) {
        print("mv: '"); print(src); print("' and '"); print(actual_dst); print("' are the same file\n");
        return 1;
    }

    // 1. Копируем всё дерево на новое место
    if (copy_recursive(src, actual_dst) != 0) {
        print("mv: failed to copy '"); print(src); print("' to '"); print(actual_dst); print("'\n");
        return 1;
    }

    // 2. Безвозвратно удаляем старый оригинал
    if (remove_recursive(src) != 0) {
        print("mv: failed to remove original '"); print(src); print("'\n");
        return 1;
    }

    return 0;
}