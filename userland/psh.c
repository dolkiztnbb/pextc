#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

#include "ulibc.h"

// Ищет программу в папках, указанных в $PATH
static void find_executable(const char* cmd, char* out_path, uint32_t max_len) {
    int is_direct = 0;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '/') { is_direct = 1; break; }
    }

    if (is_direct) {
        stat_t st;
        if (stat(cmd, &st) == 0 && st.type == TYPE_FILE) u_strncpy(out_path, cmd, max_len);
        else out_path[0] = '\0';
        return;
    }

    const char* path_var = u_getenv("PATH");
    if (!path_var) { out_path[0] = '\0'; return; }

    char pbuf[256];
    u_strncpy(pbuf, path_var, 256);
    char* dir = pbuf;

    while (*dir) {
        char* next = dir;
        while (*next && *next != ':') next++;
        if (*next == ':') { *next = '\0'; next++; }

        char test_path[128];
        u_path_join(test_path, dir, cmd, sizeof(test_path));

        stat_t st;
        if (stat(test_path, &st) == 0 && st.type == TYPE_FILE) {
            u_strncpy(out_path, test_path, max_len);
            return;
        }
        dir = next;
    }
    out_path[0] = '\0';
}

static void execute_line(char* line) {
    char* argv[32];
    int argc = 0;
    int in_q = 0;
    for (char* ptr = line; *ptr; ptr++) {
        if (*ptr == '"') in_q = !in_q;
        if (*ptr == '#' && !in_q) { *ptr = '\0'; break; }
    }

    char* ptr = line;
    while (*ptr && argc < 31) {
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') ptr++;
        if (!*ptr) break;

        if (*ptr == '"') {
            ptr++;
            argv[argc++] = ptr;
            while (*ptr && *ptr != '"') ptr++;
            if (*ptr == '"') { *ptr = '\0'; ptr++; }
        } else {
            argv[argc++] = ptr;
            while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') ptr++;
            if (*ptr != '\0') { *ptr = '\0'; ptr++; }
        }
    }
    argv[argc] = NULL;
    if (argc == 0) return;

    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '$') {
            const char* val = u_getenv(argv[i] + 1);
            argv[i] = val ? (char*)val : "";
        }
    }

    char* cmd = argv[0];

    if (u_strcmp(cmd, "exit") == 0) {
        exit(0);
    } 
    else if (u_strcmp(cmd, "cd") == 0) {
        const char* target = (argc > 1) ? argv[1] : u_getenv("HOME");
        if (!target) target = "/";
        if (chdir(target) < 0) {
            print("psh: cd: "); print(target); printn(": No such directory");
        }
        return;
    }
    else if (u_strcmp(cmd, "export") == 0) {
        if (argc > 1) {
            char* eq = argv[1];
            while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                *eq = '\0';
                u_setenv(argv[1], eq + 1);
            }
        }
        return;
    }

    char resolved_path[128];
    find_executable(cmd, resolved_path, sizeof(resolved_path));

    if (resolved_path[0] == '\0') {
        print("psh: command not found: "); printn(cmd);
        return;
    }

    int pid = exec(resolved_path, argv);
    if (pid >= 0) {
        int status;
        waitpid(pid, &status);
    } else {
        print("psh: execution failed for "); printn(resolved_path);
    }
}

static void run_script(const char* path) {
    int fd = open(path, READ);
    if (fd < 0) {
        print("psh: cannot open script: "); printn(path);
        exit(1);
    }
    stat_t st;
    stat(path, &st);
    if (st.size == 0) { close(fd); return; }

    char* buf = (char*)malloc(st.size + 1);
    if (!buf) { close(fd); return; }

    int n = read(fd, buf, st.size);
    if (n > 0) {
        buf[n] = '\0';
        char* line = buf;
        while (*line) {
            char* next = line;
            while (*next && *next != '\n') next++;
            if (*next == '\n') { *next = '\0'; next++; }
            execute_line(line);
            line = next;
        }
    }
    free(buf);
    close(fd);
}

int main(int argc, char* argv[]) {
    u_setenv("PATH", "/bin:/usr/bin");
    u_setenv("HOME", "/home");

    if (getuid() == 0) {
        u_setenv("USER", "root");
    } else {
        u_setenv("USER", "user");
    }

    char cwd[64];
    char input_buf[128];

    print("Photon Shell (psh) v1.0\n");

    int prompt_pending = 1;
    while (1) {
        if (prompt_pending) {
            if (getcwd(cwd, sizeof(cwd)) != 0) u_strncpy(cwd, "/", 64);

            set_io_color(0x0,0xf,0x0,0x7);
            print(u_getenv("USER"));
            print("@"); print(cwd);
            print(" $ ");
            prompt_pending = 0;
        }

        int n = read(STDIN, input_buf, sizeof(input_buf) - 1);
        if (n <= 0) {
            // В GUI-терминале чтение из пустого пайпа возвращается сразу.
            // Не повторяем приглашение и уступаем процессор до появления ввода.
            yield();
            continue;
        }

        input_buf[n] = '\0';
        if (input_buf[n-1] == '\n' || input_buf[n-1] == '\r') {
            input_buf[n-1] = '\0';
        }
        print("\n");
        execute_line(input_buf);
        prompt_pending = 1;
    }
    return 0;
}
