#include "photon.h"
#include "ulibc.h"
#include "libgui.h"
#include "pipe_job.h"

#define WIN_W       380
#define WIN_H       240
#define TERM_COLS   45
#define TERM_ROWS   13
#define TEXT_X      6
#define TEXT_Y      22
#define LINE_MAX    (TERM_COLS - 10)

// Кнопки в шапке окна (координаты относительно клиентской области)
#define BTN_Y       1
#define BTN_H       16
#define BTN_W       54
#define BTN_CLEAR_X (WIN_W - 58)
#define BTN_STOP_X  (WIN_W - 116)

#define READ_CHUNK  128
#define READ_ROUNDS 8    // сколько блоков максимум читаем из пайпа за кадр
#define DRAIN_FINAL_ROUNDS 64  // предел при финальном сливе (защита от бесконечного вывода)

static char term_buf[TERM_ROWS][TERM_COLS + 1];
static int  cur_row = 0;
static int  cur_col = 0;
static char input_line[TERM_COLS + 1];
static int  input_len = 0;
static int  ansi_in_esc = 0;   // состояние фильтра ANSI-последовательностей вывода ребёнка

// Консольная программа, запущенная через pwait. Пока job.running, весь
// ввод идёт в её stdin, а её stdout/stderr читается из out_r.
typedef struct {
    int                running;
    int32_t            helper_pid;  // pid /bin/pwait
    int32_t            shmid;       // shm с pipe_job_status_t
    pipe_job_status_t* st;
    int32_t            out_r;       // чтение stdout+stderr ребёнка
    int32_t            in_w;        // запись в stdin ребёнка
} job_t;

static job_t job;

// ====================================================================
// ВЫВОД В БУФЕР ОКНА
// ====================================================================

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

static void term_init(void) {
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

static void term_printn(const char* s) {
    term_print(s);
    term_putc('\n');
}

// Вывод с фильтрацией ANSI-последовательностей (\033[...m). Состояние
// сохраняется между кусками, потому что пайп отдаёт данные по частям.
static void term_print_filtered(const char* buf, int len) {
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (ansi_in_esc) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                ansi_in_esc = 0;
            }
            continue;
        }
        if (c == '\033') {
            ansi_in_esc = 1;
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

// ====================================================================
// КОНСОЛЬНЫЕ ПРОГРАММЫ ЧЕРЕЗ ПАЙПЫ (НЕ БЛОКИРУЮЩИЙ ЗАПУСК)
// ====================================================================

// Сливаем вывод ребёнка в окно. Возвращает число выведенных байт.
static int job_drain(int max_rounds) {
    char buf[READ_CHUNK];
    int total = 0;
    for (int round = 0; round < max_rounds; round++) {
        int32_t n = read(job.out_r, buf, sizeof(buf));
        if (n <= 0) break;   // пайп пуст (read не блокируется)
        term_print_filtered(buf, n);
        total += n;
    }
    return total;
}

// Освобождает все ресурсы задания и возвращает STDIN/STDOUT/STDERR в консоль.
// Не ждёт статуса: вызывается либо когда pwait уже сообщил о завершении, либо
// когда мы сами убили ребёнка.
static void job_finish(void) {
    if (!job.running) return;

    job_drain(DRAIN_FINAL_ROUNDS);

    close(STDOUT);
    close(STDERR);
    close(job.out_r);
    close(STDIN);
    close(job.in_w);

    shm_detach(job.shmid);
    waitpid(job.helper_pid, NULL);   // pwait уже завершается, ждём только его выход

    job.running = 0;
    job.helper_pid = 0;
    job.shmid = -1;
    job.st = NULL;
    job.out_r = -1;
    job.in_w = -1;
}

// Запускает argv (argv[0] — имя команды) через /bin/pwait. Возвращает 0 при
// успехе. Сам ребёнок стартует асинхронно, см. job_poll().
static int job_start(char* const argv[], const char* bin_path) {
    int32_t shmid = shm_create(sizeof(pipe_job_status_t));
    if (shmid < 0) return -1;

    pipe_job_status_t* st = (pipe_job_status_t*)shm_attach(shmid);
    if (!st) {
        shm_detach(shmid);
        return -1;
    }
    st->state = JOB_IDLE;
    st->child_pid = 0;
    st->exit_code = 0;

    int32_t out_p[2] = {-1, -1};
    int32_t in_p[2]  = {-1, -1};
    if (pipe(out_p) != 0) {
        shm_detach(shmid);
        return -1;
    }
    if (pipe(in_p) != 0) {
        close(out_p[0]);
        close(out_p[1]);
        shm_detach(shmid);
        return -1;
    }

    // Дескрипторы глобальные: всё, что сейчас запустится (в том числе pwait и
    // его ребёнок), будет писать в out_p и читать из in_p, пока мы не вернём
    // консоль в job_finish().
    dup2(out_p[1], STDOUT);
    dup2(out_p[1], STDERR);
    dup2(in_p[0], STDIN);
    close(out_p[1]);   // у нас остаётся только копия в STDOUT/STDERR
    close(in_p[0]);    // и копия в STDIN

    char shm_arg[12];
    u_format_num((uint32_t)shmid, shm_arg, sizeof(shm_arg));

    char* helper_argv[20];
    int n = 0;
    helper_argv[n++] = "pwait";
    helper_argv[n++] = shm_arg;
    helper_argv[n++] = (char*)bin_path;
    for (int i = 0; argv[i] && n < 19; i++) {
        helper_argv[n++] = argv[i];
    }
    helper_argv[n] = NULL;

    int32_t helper = exec("/bin/pwait", helper_argv);
    if (helper <= 0) {
        close(STDOUT);
        close(STDERR);
        close(out_p[0]);
        close(STDIN);
        close(in_p[1]);
        shm_detach(shmid);
        return -1;
    }

    job.running = 1;
    job.helper_pid = helper;
    job.shmid = shmid;
    job.st = st;
    job.out_r = out_p[0];
    job.in_w = in_p[1];
    return 0;
}

// Вызывается каждый кадр. Сливает вывод и проверяет, не завершился ли ребёнок.
// *printed получает число выведенных байт. Возвращает 1, если задание
// только что завершилось (в этом случае вывод уже полностью слит).
static int job_poll(int* printed) {
    *printed = 0;
    if (!job.running) return 0;

    // Сначала читаем статус, потом сливаем пайп: ребёнок пишет весь вывод до
    // того, как pwait отметит JOB_DONE, поэтому после DONE данных больше нет.
    int32_t state = job.st->state;
    int finished = (state == JOB_DONE || state == JOB_FAILED);

    *printed = job_drain(finished ? DRAIN_FINAL_ROUNDS : READ_ROUNDS);

    if (finished) {
        if (state == JOB_FAILED) term_printn("Failed to execute program");
        job_finish();
        return 1;
    }
    return 0;
}

// Отправляет строку в stdin ребёнка. Пайп не блокирует запись, при полном
// буфере часть данных может быть отброшена (см. kwrite в ядре).
static void job_send_line(const char* line) {
    int len = u_strlen(line);
    if (len > 0) write(job.in_w, line, (uint32_t)len);
    write(job.in_w, "\n", 1);
}

// Принудительно останавливает задание (кнопка Stop или закрытие окна).
static void job_stop(void) {
    if (!job.running) return;

    if (job.st->state >= JOB_SPAWNED && job.st->child_pid > 0) {
        send_signal(job.st->child_pid, 9);
    }
    // Убиваем pwait всегда: если ребёнка ещё нет, он не успеет его запустить.
    // Для уже завершённого pwait это безопасно (его ресурсы освободятся один раз).
    send_signal(job.helper_pid, 9);

    job_finish();
}

// ====================================================================
// ВСТРОЕННЫЕ КОМАНДЫ И ЗАПУСК
// ====================================================================

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

// Возвращает 1, если нужно закрыть окно терминала
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
        term_init();
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
                // GUI-программы живут в собственных окнах, ждать их не нужно
                int32_t pid = exec(bin_path, argv);
                if (pid > 0) {
                    term_print("Started GUI PID ");
                    char pbuf[12];
                    u_format_num((uint32_t)pid, pbuf, sizeof(pbuf));
                    term_print(pbuf);
                    term_putc('\n');
                } else {
                    term_print("exec failed\n");
                }
            } else {
                // Консольная программа: запускаем через пайпы, терминал не блокируется
                if (job_start(argv, bin_path) != 0) {
                    term_print("Failed to start: ");
                    term_printn(argv[0]);
                }
            }
        } else {
            term_print("Unknown command: ");
            term_print(argv[0]);
            term_putc('\n');
        }
    }
    return 0;
}

// ====================================================================
// ОТРИСОВКА И ВВОД
// ====================================================================

static void render_term(gui_window_t* win) {
    gui_draw_rect(win, 0, 0, WIN_W, WIN_H, C_BLACK);

    gui_draw_rect(win, 0, 0, WIN_W, 18, C_LIGHTGRAY);
    gui_draw_text(win, 6, 1, "Photon GUI Terminal (/bin/term)", C_BLACK);
    gui_draw_button(win, BTN_STOP_X, BTN_Y, BTN_W, BTN_H, "Stop");
    gui_draw_button(win, BTN_CLEAR_X, BTN_Y, BTN_W, BTN_H, "Clear");

    for (int r = 0; r < TERM_ROWS; r++) {
        gui_draw_text(win, TEXT_X, TEXT_Y + r * 16, term_buf[r], C_LIGHTGREEN);
    }

    int cx = TEXT_X + cur_col * 8;
    int cy = TEXT_Y + cur_row * 16 + 13;
    if (cx + 8 < WIN_W && cy + 2 < WIN_H) {
        gui_draw_rect(win, cx, cy, 8, 2, C_WHITE);
    }

    win->header->is_dirty = 1;
}

static int point_in_button(int x, int y, int bx) {
    return x >= bx && x < bx + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

// Строка введена: отправляем её ребёнку или выполняем как команду оболочки
static int submit_line(void) {
    input_line[input_len] = '\0';
    term_putc('\n');

    int should_exit = 0;
    if (job.running) {
        job_send_line(input_line);
    } else {
        should_exit = execute_command(input_line);
    }
    input_len = 0;

    if (!should_exit && !job.running) term_prompt();
    return should_exit;
}

// Возвращает 1, если пользователь ввёл exit и окно нужно закрыть
static int handle_key(char k) {
    if (k == '\n' || k == '\r') {
        return submit_line();
    } else if (k == '\b') {
        if (input_len > 0) {
            input_len--;
            term_putc('\b');
        }
    } else if ((unsigned char)k >= 32 && (unsigned char)k <= 126) {
        if (input_len < LINE_MAX) {
            input_line[input_len++] = k;
            term_putc(k);
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "Photon Terminal");
    if (!win) exit(1);

    job.running = 0;
    job.shmid = -1;
    job.out_r = -1;
    job.in_w = -1;

    term_init();
    term_print("Photon OS GUI Terminal v2.1\n");
    term_print("Type 'help' for commands.\n");
    term_prompt();
    render_term(win);

    shm_event_t ev;
    while (!win->header->closed) {
        int dirty = 0;

        // Вывод и завершение задания, не блокируя цикл событий
        int printed = 0;
        if (job_poll(&printed)) {
            term_prompt();
            dirty = 1;
        } else if (printed > 0) {
            dirty = 1;
        }

        while (gui_poll_event(win, &ev)) {
            if (ev.type == EVT_CLICK) {
                if (point_in_button(ev.x, ev.y, BTN_CLEAR_X)) {
                    input_len = 0;
                    term_init();
                    if (!job.running) term_prompt();
                    dirty = 1;
                } else if (point_in_button(ev.x, ev.y, BTN_STOP_X)) {
                    if (job.running) {
                        job_stop();
                        term_printn("^C");
                        term_prompt();
                        dirty = 1;
                    }
                }
            } else if (ev.type == EVT_KEY) {
                dirty = 1;
                if (handle_key((char)ev.btn)) {
                    win->header->closed = 1;
                    break;
                }
            }
        }

        if (dirty) {
            render_term(win);
        }
        if (win->header->closed) break;
        sleep(16);
    }

    // Окно закрыто: прекращаем задание и возвращаем консоль, прежде чем выйти
    job_stop();
    gui_destroy_window(win);
    exit(0);
}
