#include "photon.h"
#include "ulibc.h"
#include "libgui.h"

#define WIN_W     380
#define WIN_H     240
#define TERM_COLS 45
#define TERM_ROWS 13

static char term_buf[TERM_ROWS][TERM_COLS + 1];
static int  cur_row = 0;
static int  cur_col = 0;
static char input_line[TERM_COLS + 1];
static int  input_len = 0;
static int  child_pids[32];
static int  child_count = 0;

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        for (int c = 0; c <= TERM_COLS; c++) {
            term_buf[r][c] = term_buf[r + 1][c];
        }
    }
    for (int c = 0; c <= TERM_COLS; c++) {
        term_buf[TERM_ROWS - 1][c] = ' ';
    }
    term_buf[TERM_ROWS - 1][TERM_COLS] = '\0';
}

static void term_Init(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            term_buf[r][c] = ' ';
        }
        term_buf[r][TERM_COLS] = '\0';
    }
    cur_row = 0;
    cur_col = 0;
}

static void term_putc(char ch) {
    if (ch == '\r') return;
    if (ch == '\n') {
        cur_col = 0;
        if (cur_row < TERM_ROWS - 1) cur_row++;
        else term_scroll();
        return;
    }
    if (ch == '\b') {
        if (cur_col > 0) {
            cur_col--;
            term_buf[cur_row][cur_col] = ' ';
        }
        return;
    }
    if ((unsigned char)ch < 32) return;

    term_buf[cur_row][cur_col++] = ch;
    if (cur_col >= TERM_COLS) {
        cur_col = 0;
        if (cur_row < TERM_ROWS - 1) cur_row++;
        else term_scroll();
    }
}

static void term_print(const char* s) {
    while (*s) term_putc(*s++);
}

// Вывод текста с фильтрацией ANSI-последовательностей (\033[...m)
static void term_print_ansi_filtered(const char* buf, int len) {
    int in_esc = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (in_esc) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                in_esc = 0;
            }
            continue;
        }
        if (c == '\033') {
            in_esc = 1;
            continue;
        }
        term_putc(c);
    }
}

static void term_prompt(void) {
    char cwd[64];
    if (getcwd(cwd, sizeof(cwd)) != 0) u_strncpy(cwd, "/", sizeof(cwd));
    term_print(cwd);
    term_print("> ");
}

static int is_gui_program(const char* name) {
    const char* base = name;
    for (const char* p = name; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    if (u_strcmp(base, "calc") == 0 ||
        u_strcmp(base, "explorer") == 0 ||
        u_strcmp(base, "notepad") == 0 ||
        u_strcmp(base, "sysmon") == 0 ||
        u_strcmp(base, "term") == 0 ||
        u_strcmp(base, "wm") == 0 ||
        u_strcmp(base, "pc") == 0 ||
        u_strcmp(base, "pt") == 0) {
        return 1;
    }
    return 0;
}

static void run_captured_cli(const char* bin_path, char* argv[]) {
    int32_t out_pipe[2] = {-1, -1};
    int32_t in_pipe[2]  = {-1, -1};

    int has_out_pipe = (pipe(out_pipe) == 0);
    int has_in_pipe  = (pipe(in_pipe) == 0);

    if (has_in_pipe) {
        // Закрываем конец записи входного пайпа, чтобы read(STDIN) сразу получал EOF, а не ждал getch()
        close(in_pipe[1]);
        dup2(in_pipe[0], STDIN);
    }
    if (has_out_pipe) {
        dup2(out_pipe[1], STDOUT);
        dup2(out_pipe[1], STDERR);
    }

    int pid = exec(bin_path, argv);

    // Мгновенно возвращаем STDIN/STDOUT/STDERR обратно к консоли
    if (has_out_pipe) {
        close(STDOUT);
        close(STDERR);
        close(out_pipe[1]);
    }
    if (has_in_pipe) {
        close(STDIN);
        close(in_pipe[0]);
    }

    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status);

        if (has_out_pipe) {
            char rbuf[128];
            while (1) {
                int n = read(out_pipe[0], rbuf, sizeof(rbuf));
                if (n <= 0) break;
                term_print_ansi_filtered(rbuf, n);
            }
        }
    } else {
        term_print("Failed to execute: ");
        term_print(bin_path);
        term_putc('\n');
    }

    if (has_out_pipe) {
        close(out_pipe[0]);
    }
}

static int execute_command(const char* raw_line) {
    char line[64];
    u_strncpy(line, raw_line, sizeof(line));

    char* argv[8];
    int argc = 0;
    char* p = line;
    while (*p && argc < 7) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    if (argc == 0) return 0;

    if (u_strcmp(argv[0], "help") == 0) {
        term_print("Built-ins: ls cd pwd cat echo\n");
        term_print("mkdir touch rm clear whoami exit\n");
        term_print("Or run any /bin/<program>\n");
    } else if (u_strcmp(argv[0], "clear") == 0) {
        term_Init();
    } else if (u_strcmp(argv[0], "exit") == 0) {
        return 1;
    } else if (u_strcmp(argv[0], "pwd") == 0) {
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd)) == 0) {
            term_print(cwd);
            term_putc('\n');
        }
    } else if (u_strcmp(argv[0], "whoami") == 0) {
        char num[12];
        u_format_num((uint32_t)getuid(), num, sizeof(num));
        term_print(getuid() == 0 ? "root (uid=0)\n" : "user (uid=");
        if (getuid() != 0) {
            term_print(num);
            term_print(")\n");
        }
    } else if (u_strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            term_print(argv[i]);
            if (i + 1 < argc) term_putc(' ');
        }
        term_putc('\n');
    } else if (u_strcmp(argv[0], "cd") == 0) {
        const char* target = (argc > 1) ? argv[1] : "/";
        if (chdir(target) != 0) term_print("cd: directory not found\n");
    } else if (u_strcmp(argv[0], "mkdir") == 0 && argc > 1) {
        if (mkdir(argv[1]) != 0) term_print("mkdir: failed\n");
    } else if (u_strcmp(argv[0], "touch") == 0 && argc > 1) {
        int fd = open(argv[1], WRITE | CREATE);
        if (fd >= 0) close(fd);
        else term_print("touch: failed\n");
    } else if (u_strcmp(argv[0], "rm") == 0 && argc > 1) {
        if (unlink(argv[1]) != 0) term_print("rm: failed\n");
    } else if (u_strcmp(argv[0], "ls") == 0 && argc == 1) {
        char target[128];
        if (getcwd(target, sizeof(target)) != 0) u_strncpy(target, "/", sizeof(target));
        int fd = open(target, READ);
        if (fd < 0) {
            term_print("ls: cannot open dir\n");
        } else {
            dirent_t d;
            int idx = 0;
            while (readdir(fd, idx++, &d) == 0) {
                if (d.name[0] == '\0') break;
                term_print(d.name);
                if (d.type == TYPE_DIR) term_putc('/');
                term_print("  ");
            }
            close(fd);
            term_putc('\n');
        }
    } else if (u_strcmp(argv[0], "cat") == 0 && argc > 1) {
        int fd = open(argv[1], READ);
        if (fd < 0) {
            term_print("cat: file not found\n");
        } else {
            char buf[64];
            int n = read(fd, buf, sizeof(buf) - 1);
            while (n > 0) {
                buf[n] = '\0';
                term_print(buf);
                n = read(fd, buf, sizeof(buf) - 1);
            }
            close(fd);
            term_putc('\n');
        }
    } else {
        char bin_path[64];
        if (argv[0][0] == '/') u_strncpy(bin_path, argv[0], sizeof(bin_path));
        else u_path_join(bin_path, "/bin", argv[0], sizeof(bin_path));

        stat_t st;
        if (stat(bin_path, &st) == 0 && st.type == TYPE_FILE) {
            if (is_gui_program(bin_path)) {
                int pid = exec(bin_path, argv);
                if (pid > 0) {
                    if (child_count < 32) child_pids[child_count++] = pid;
                    term_print("Started GUI PID ");
                    char pbuf[12];
                    u_format_num((uint32_t)pid, pbuf, sizeof(pbuf));
                    term_print(pbuf);
                    term_putc('\n');
                } else {
                    term_print("exec failed\n");
                }
            } else {
                // Консольная утилита (/bin/ls -la, /bin/pexdump, /bin/cp и т.д.):
                // перехватываем её STDOUT через пайп и выводим внутри окна терминала!
                run_captured_cli(bin_path, argv);
            }
        } else {
            term_print("Unknown command: ");
            term_print(argv[0]);
            term_putc('\n');
        }
    }
    return 0;
}

static void render_term(gui_window_t* win) {
    gui_draw_rect(win, 0, 0, WIN_W, WIN_H, C_BLACK);

    gui_draw_rect(win, 0, 0, WIN_W, 18, C_LIGHTGRAY);
    gui_draw_text(win, 6, 1, "Photon GUI Terminal (/bin/term)", C_BLACK);
    gui_draw_button(win, WIN_W - 58, 1, 54, 16, "Clear");

    int base_y = 22;
    for (int r = 0; r < TERM_ROWS; r++) {
        gui_draw_text(win, 6, base_y + r * 16, term_buf[r], C_LIGHTGREEN);
    }

    int cx = 6 + cur_col * 8;
    int cy = base_y + cur_row * 16 + 13;
    if (cx + 8 < WIN_W && cy + 2 < WIN_H) {
        gui_draw_rect(win, cx, cy, 8, 2, C_WHITE);
    }

    win->header->is_dirty = 1;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "Photon Terminal");
    if (!win) exit(1);

    term_Init();
    term_print("Photon OS GUI Terminal v2.1\n");
    term_print("Type 'help' for commands.\n");
    term_prompt();
    render_term(win);

    shm_event_t ev;
    while (!win->header->closed) {
        int dirty = 0;
        while (gui_poll_event(win, &ev)) {
            if (ev.type == EVT_CLICK) {
                if (ev.x >= WIN_W - 58 && ev.x < WIN_W - 4 && ev.y >= 1 && ev.y < 17) {
                    input_len = 0;
                    term_Init();
                    term_prompt();
                    dirty = 1;
                }
            } else if (ev.type == EVT_KEY) {
                char k = (char)ev.btn;
                if (k == '\n' || k == '\r') {
                    input_line[input_len] = '\0';
                    term_putc('\n');
                    int should_exit = execute_command(input_line);
                    input_len = 0;
                    if (should_exit) {
                        win->header->closed = 1;
                        break;
                    }
                    term_prompt();
                    dirty = 1;
                } else if (k == '\b') {
                    if (input_len > 0) {
                        input_len--;
                        term_putc('\b');
                        dirty = 1;
                    }
                } else if ((unsigned char)k >= 32 && (unsigned char)k <= 126) {
                    if (input_len < TERM_COLS - 10) {
                        input_line[input_len++] = k;
                        term_putc(k);
                        dirty = 1;
                    }
                }
            }
        }

        if (dirty) {
            render_term(win);
        }
        if (win->header->closed) break;
        sleep(16);
    }

    gui_destroy_window(win);
    exit(0);
}
