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
static uint8_t term_fg[TERM_ROWS][TERM_COLS];
static uint8_t term_bg[TERM_ROWS][TERM_COLS];
static int  cur_row = 0;
static int  cur_col = 0;
static int  input_len = 0;
static char input_line[TERM_COLS + 1];
static uint8_t term_current_fg = C_LIGHTGREEN;
static uint8_t term_current_bg = C_BLACK;
static int term_bold = 0;
static int ansi_state = 0; // 0: текст, 1: ESC, 2: CSI
static int ansi_params[8];
static int ansi_param_count = 0;
static int ansi_param_value = 0;
static int ansi_have_param = 0;
static int ansi_private = 0;
static int saved_row = 0;
static int saved_col = 0;

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

static void term_clear_buffer(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            term_buf[r][c] = ' ';
            term_fg[r][c] = term_current_fg;
            term_bg[r][c] = term_current_bg;
        }
        term_buf[r][TERM_COLS] = '\0';
    }
}

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        for (int c = 0; c <= TERM_COLS; c++) {
            term_buf[r][c] = term_buf[r + 1][c];
        }
        for (int c = 0; c < TERM_COLS; c++) {
            term_fg[r][c] = term_fg[r + 1][c];
            term_bg[r][c] = term_bg[r + 1][c];
        }
    }
    for (int c = 0; c < TERM_COLS; c++) {
        term_buf[TERM_ROWS - 1][c] = ' ';
        term_fg[TERM_ROWS - 1][c] = term_current_fg;
        term_bg[TERM_ROWS - 1][c] = term_current_bg;
    }
    term_buf[TERM_ROWS - 1][TERM_COLS] = '\0';
}

static void term_init(void) {
    term_current_fg = C_LIGHTGREEN;
    term_current_bg = C_BLACK;
    term_bold = 0;
    ansi_state = 0;
    ansi_param_count = 0;
    ansi_param_value = 0;
    ansi_have_param = 0;
    ansi_private = 0;
    cur_row = 0;
    cur_col = 0;
    term_clear_buffer();
}

static void term_blank_cell(int row, int col) {
    if (row < 0 || row >= TERM_ROWS || col < 0 || col >= TERM_COLS) return;
    term_buf[row][col] = ' ';
    term_fg[row][col] = term_current_fg;
    term_bg[row][col] = term_current_bg;
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
            term_blank_cell(cur_row, cur_col);
        }
        return;
    }
    if (ch == '\t') {
        int spaces = 4 - (cur_col & 3);
        while (spaces-- > 0) term_putc(' ');
        return;
    }
    if ((unsigned char)ch < 32) return;

    term_buf[cur_row][cur_col] = ch;
    term_fg[cur_row][cur_col] = term_current_fg;
    term_bg[cur_row][cur_col] = term_current_bg;
    cur_col++;
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

static uint8_t ansi_color(int index, int bright) {
    if (bright) {
        switch (index) {
            case 0: return C_DARKGRAY;
            case 1: return C_LIGHTRED;
            case 2: return C_LIGHTGREEN;
            case 3: return C_YELLOW;
            case 4: return C_LIGHTBLUE;
            case 5: return C_LIGHTMAGENTA;
            case 6: return C_LIGHTCYAN;
            default: return C_WHITE;
        }
    }
    switch (index) {
        case 0: return C_BLACK;
        case 1: return C_RED;
        case 2: return C_GREEN;
        case 3: return C_BROWN;
        case 4: return C_BLUE;
        case 5: return C_MAGENTA;
        case 6: return C_CYAN;
        default: return C_LIGHTGRAY;
    }
}

static void ansi_push_param(void) {
    if (ansi_param_count < (int)(sizeof(ansi_params) / sizeof(ansi_params[0]))) {
        ansi_params[ansi_param_count++] = ansi_have_param ? ansi_param_value : 0;
    }
    ansi_param_value = 0;
    ansi_have_param = 0;
}

static int ansi_param(int index) {
    return (index >= 0 && index < ansi_param_count) ? ansi_params[index] : 0;
}

static int ansi_param_or_one(int index) {
    int value = ansi_param(index);
    return value > 0 ? value : 1;
}

static void ansi_apply_sgr(void) {
    for (int i = 0; i < ansi_param_count; i++) {
        int code = ansi_params[i];
        if (code == 0) {
            term_current_fg = C_LIGHTGREEN;
            term_current_bg = C_BLACK;
            term_bold = 0;
        } else if (code == 1) {
            term_bold = 1;
        } else if (code == 22) {
            term_bold = 0;
        } else if (code == 39) {
            term_current_fg = C_LIGHTGREEN;
        } else if (code == 49) {
            term_current_bg = C_BLACK;
        } else if (code >= 30 && code <= 37) {
            term_current_fg = ansi_color(code - 30, term_bold);
        } else if (code >= 40 && code <= 47) {
            term_current_bg = ansi_color(code - 40, 0);
        } else if (code >= 90 && code <= 97) {
            term_current_fg = ansi_color(code - 90, 1);
        } else if (code >= 100 && code <= 107) {
            term_current_bg = ansi_color(code - 100, 1);
        }
    }
}

static void ansi_erase_line(int mode) {
    int first = (mode == 1) ? 0 : cur_col;
    int last = (mode == 0) ? TERM_COLS - 1 : cur_col;
    if (mode == 2) {
        first = 0;
        last = TERM_COLS - 1;
    }
    for (int c = first; c <= last; c++) term_blank_cell(cur_row, c);
}

static void ansi_erase_display(int mode) {
    int cursor = cur_row * TERM_COLS + cur_col;
    int first = (mode == 1) ? 0 : cursor;
    int last = (mode == 0) ? TERM_ROWS * TERM_COLS - 1 : cursor;
    if (mode == 2) {
        first = 0;
        last = TERM_ROWS * TERM_COLS - 1;
    }
    for (int pos = first; pos <= last; pos++) {
        term_blank_cell(pos / TERM_COLS, pos % TERM_COLS);
    }
}

static void ansi_handle_csi(char final) {
    if (ansi_private) return;

    switch (final) {
        case 'm':
            ansi_apply_sgr();
            break;
        case 'J':
            ansi_erase_display(ansi_param(0));
            break;
        case 'K':
            ansi_erase_line(ansi_param(0));
            break;
        case 'H':
        case 'f': {
            int row = ansi_param_or_one(0) - 1;
            int col = ansi_param_or_one(1) - 1;
            if (row < 0) row = 0;
            if (row >= TERM_ROWS) row = TERM_ROWS - 1;
            if (col < 0) col = 0;
            if (col >= TERM_COLS) col = TERM_COLS - 1;
            cur_row = row;
            cur_col = col;
            break;
        }
        case 'A': {
            int n = ansi_param_or_one(0);
            cur_row = (n > cur_row) ? 0 : cur_row - n;
            break;
        }
        case 'B': {
            int n = ansi_param_or_one(0);
            cur_row = (cur_row + n >= TERM_ROWS) ? TERM_ROWS - 1 : cur_row + n;
            break;
        }
        case 'C': {
            int n = ansi_param_or_one(0);
            cur_col = (cur_col + n >= TERM_COLS) ? TERM_COLS - 1 : cur_col + n;
            break;
        }
        case 'D': {
            int n = ansi_param_or_one(0);
            cur_col = (n > cur_col) ? 0 : cur_col - n;
            break;
        }
        case 's':
            saved_row = cur_row;
            saved_col = cur_col;
            break;
        case 'u':
            cur_row = saved_row;
            cur_col = saved_col;
            break;
        default:
            break;
    }
}

// Переводит распространённые ANSI CSI-последовательности в действия терминала.
// Состояние парсера сохраняется между чтениями из пайпа.
static void term_process_output(const char* buf, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (ansi_state == 1) {
            if (c == '[') {
                ansi_state = 2;
                ansi_param_count = 0;
                ansi_param_value = 0;
                ansi_have_param = 0;
                ansi_private = 0;
            } else {
                ansi_state = 0;
            }
            continue;
        }
        if (ansi_state == 2) {
            if (c == '?' && ansi_param_count == 0 && !ansi_have_param) {
                ansi_private = 1;
            } else if (c >= '0' && c <= '9') {
                ansi_have_param = 1;
                ansi_param_value = ansi_param_value * 10 + (c - '0');
                if (ansi_param_value > 999) ansi_param_value = 999;
            } else if (c == ';') {
                ansi_push_param();
            } else if (c >= 0x40 && c <= 0x7e) {
                ansi_push_param();
                ansi_handle_csi((char)c);
                ansi_state = 0;
            }
            continue;
        }
        if (c == 0x1b) {
            ansi_state = 1;
            continue;
        }
        term_putc((char)c);
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
        term_process_output(buf, n);
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
    ansi_state = 0;
    ansi_param_count = 0;
    ansi_have_param = 0;
    ansi_private = 0;
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

static int command_has_slash(const char* command) {
    for (int i = 0; command && command[i]; i++) {
        if (command[i] == '/') return 1;
    }
    return 0;
}

static void resolve_command_path(const char* command, char* path, uint32_t max_len) {
    if (command[0] == '/' || command_has_slash(command)) {
        u_strncpy(path, command, max_len);
    } else {
        u_path_join(path, "/bin", command, max_len);
    }
}

static const char* command_basename(const char* path) {
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

static int is_gui_program(const char* path) {
    const char* base = command_basename(path);
    return u_strcmp(base, "calc") == 0 ||
           u_strcmp(base, "explorer") == 0 ||
           u_strcmp(base, "notepad") == 0 ||
           u_strcmp(base, "sysmon") == 0 ||
           u_strcmp(base, "term") == 0 ||
           u_strcmp(base, "wm") == 0;
}

// Эти приложения работают с VGA/клавиатурой напрямую, а не через stdio.
static int requires_native_console(const char* path) {
    const char* base = command_basename(path);
    return u_strcmp(base, "edit") == 0 ||
           u_strcmp(base, "pc") == 0 ||
           u_strcmp(base, "pt") == 0 ||
           u_strcmp(base, "sudo") == 0;
}

// Если терминал запущен с аргументом-программой (например, WM запускает
// так /bin/psh), запускаем её как консольное задание внутри этого окна.
static int start_initial_command(int argc, char* argv[]) {
    if (argc < 2 || !argv || !argv[1] || !argv[1][0]) return 0;

    char bin_path[96];
    resolve_command_path(argv[1], bin_path, sizeof(bin_path));

    stat_t st;
    if (stat(bin_path, &st) != 0 || st.type != TYPE_FILE) {
        term_print("Unknown command: ");
        term_printn(argv[1]);
        return -1;
    }
    if (requires_native_console(bin_path)) {
        term_print("Requires native keyboard/VGA console: ");
        term_printn(argv[1]);
        return -1;
    }
    if (is_gui_program(bin_path)) {
        if (exec(bin_path, &argv[1]) <= 0) {
            term_print("Failed to start: ");
            term_printn(argv[1]);
        }
        return 0;
    }

    if (job_start(&argv[1], bin_path) != 0) {
        term_print("Failed to start: ");
        term_printn(argv[1]);
        return -1;
    }
    return 1;
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
    char input[TERM_COLS + 1];
    int len = u_strlen(line);
    if (len > TERM_COLS) len = TERM_COLS;
    for (int i = 0; i < len; i++) input[i] = line[i];
    input[len++] = '\n';
    write(job.in_w, input, (uint32_t)len);
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

// Возвращает 1, если нужно закрыть окно терминала.
static int execute_command(const char* raw_line) {
    char line[128];
    u_strncpy(line, raw_line, sizeof(line));

    char* argv[16];
    int argc = 0;
    char* p = line;
    while (*p && argc < 15) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    if (argc == 0) return 0;

    if (u_strcmp(argv[0], "help") == 0) {
        term_print("Built-ins: cd, exit, help\n");
        term_print("Console programs from /bin run here; GUI apps open their own windows.\n");
        term_print("Raw VGA apps require the native text console.\n");
    } else if (u_strcmp(argv[0], "exit") == 0) {
        return 1;
    } else if (u_strcmp(argv[0], "cd") == 0) {
        const char* target = (argc > 1) ? argv[1] : "/";
        if (chdir(target) != 0) {
            term_print("cd: directory not found: ");
            term_printn(target);
        }
    } else {
        char bin_path[96];
        resolve_command_path(argv[0], bin_path, sizeof(bin_path));

        stat_t st;
        if (stat(bin_path, &st) != 0 || st.type != TYPE_FILE) {
            term_print("Unknown command: ");
            term_printn(argv[0]);
        } else if (requires_native_console(bin_path)) {
            term_print("Requires native keyboard/VGA console: ");
            term_printn(argv[0]);
        } else if (is_gui_program(bin_path)) {
            // GUI-программы создают собственные окна, их stdout не принадлежит term.
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
        } else if (job_start(argv, bin_path) != 0) {
            term_print("Failed to start: ");
            term_printn(argv[0]);
        }
    }
    return 0;
}

// ====================================================================
// ОТРИСОВКА И ВВОД
// ====================================================================

static void render_term(gui_window_t* win) {
    gui_clear(win, C_BLACK);
    gui_draw_rect(win, 0, 0, WIN_W, WIN_H, C_BLACK);

    gui_draw_rect(win, 0, 0, WIN_W, 18, C_LIGHTGRAY);
    gui_draw_text(win, 6, 1, "Photon GUI Terminal (/bin/term)", C_BLACK);
    gui_draw_button(win, BTN_STOP_X, BTN_Y, BTN_W, BTN_H, "Stop");
    gui_draw_button(win, BTN_CLEAR_X, BTN_Y, BTN_W, BTN_H, "Clear");

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            int x = TEXT_X + c * 8;
            int y = TEXT_Y + r * 16;
            if (term_bg[r][c] != C_BLACK) {
                gui_fill_rect(win, x, y, 8, 16, term_bg[r][c]);
            }
            gui_draw_char(win, x, y, (unsigned char)term_buf[r][c], term_fg[r][c]);
        }
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
    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "Photon Terminal");
    if (!win) exit(1);

    job.running = 0;
    job.shmid = -1;
    job.out_r = -1;
    job.in_w = -1;

    term_init();
    term_print("Photon OS GUI Terminal v2.1\n");
    if (start_initial_command(argc, argv) != 1) {
        term_print("Type 'help' for commands.\n");
        term_prompt();
    }
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
