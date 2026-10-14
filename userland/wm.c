#include "photon.h"
#include "ulibc.h"
#include "vga_12.h"
#include "gui_shm.h"

#define MAX_WINDOWS   16
#define SCREEN_W      640
#define SCREEN_H      480
#define ROW_BYTES     (SCREEN_W / 8)
#define TOTAL_BYTES   (ROW_BYTES * SCREEN_H)

// Панель задач (Taskbar)
#define TASKBAR_H     28
#define TASKBAR_Y     (SCREEN_H - TASKBAR_H)
#define START_BTN_X   4
#define START_BTN_Y   (TASKBAR_Y + 3)
#define START_BTN_W   68
#define START_BTN_H   22

// Меню Пуск (Start Menu)
#define MENU_X        4
#define MENU_W        184
#define MENU_ITEM_H   24
#define MENU_ITEMS    7
#define MENU_H        (MENU_ITEMS * MENU_ITEM_H + 8)
#define MENU_Y        (TASKBAR_Y - MENU_H)

// Ярлыки на рабочем столе
#define DESK_ICON_X   18
#define DESK_ICON_W   44
#define DESK_ICON_H   38
#define DESK_STEP_Y   78
#define DESK_COUNT    5

// Встроенный fallback-терминал (на случай если /bin/term ещё не собран)
#define TERM_COLS     46
#define TERM_ROWS     13
#define TERM_W        (TERM_COLS * 8 + 8)
#define TERM_H        (TERM_ROWS * 16 + 8)

typedef struct {
    window_header_t* header;
    uint8_t*         pixels;
    int32_t          shmid;
} wm_slot_t;

static wm_slot_t clients[MAX_WINDOWS];
static int client_count = 0;

static uint8_t* vga_planes[4];
static volatile uint8_t* vram = (volatile uint8_t*)0xA0000;

static uint8_t wm_font[4096];
static int wm_font_loaded = 0;
static int start_menu_open = 0;
static int wm_should_exit = 0;

// Загрузка шрифта /sys/cp437.bm в оперативную память
static void load_cp437_file(void) {
    if (wm_font_loaded) return;
    int fd = open("/sys/cp437.bm", READ | BINARY);
    if (fd < 0) return;

    int total = 0;
    while (total < 4096) {
        int n = read(fd, (char*)(wm_font + total), 4096 - total);
        if (n <= 0) break;
        total += n;
    }
    close(fd);
    if (total == 4096) {
        wm_font_loaded = 1;
    }
}

// Запись шрифта /sys/cp437.bm в VGA Plane 2 при возврате в текстовый режим
static void restore_vga_font_cp437(void) {
    load_cp437_file();
    if (!wm_font_loaded) return;

    asm volatile("cli");

    vga_outw(0x3C4, 0x0402); // Enable Plane 2 only
    vga_outw(0x3C4, 0x0003); // Character Map Select 0
    vga_outw(0x3C4, 0x0604); // Extended Memory, Odd/Even OFF

    vga_outw(0x3CE, 0x0005); // Write Mode 0
    vga_outw(0x3CE, 0x0C06); // Map to 0xB8000

    volatile uint8_t* font_vram = (volatile uint8_t*)0xB8000;
    for (int ch = 0; ch < 256; ch++) {
        for (int row = 0; row < 16; row++) {
            font_vram[ch * 32 + row] = wm_font[ch * 16 + row];
        }
        for (int row = 16; row < 32; row++) {
            font_vram[ch * 32 + row] = 0x00;
        }
    }

    vga_outw(0x3C4, 0x0302); // Enable Plane 0 & 1
    vga_outw(0x3C4, 0x0204); // Odd/Even ON
    vga_outw(0x3CE, 0x1005); // Odd/Even GC
    vga_outw(0x3CE, 0x0E06); // Map 0xB8000 Text Mode

    volatile uint16_t* text_vram = (volatile uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        text_vram[i] = 0x0720;
    }

    asm volatile("sti");
}

static inline void put_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    int idx = y * ROW_BYTES + (x / 8);
    uint8_t mask = 0x80 >> (x % 8);
    for (int p = 0; p < 4; p++) {
        if (color & (1 << p)) vga_planes[p][idx] |= mask;
        else                  vga_planes[p][idx] &= ~mask;
    }
}

static void fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    for (int cy = y; cy < y + h; cy++)
        for (int cx = x; cx < x + w; cx++)
            put_pixel(cx, cy, color);
}

static void draw_bevel(int x, int y, int w, int h, uint8_t bg, int sunken) {
    uint8_t tl = sunken ? C_DARKGRAY : C_WHITE;
    uint8_t br = sunken ? C_WHITE    : C_BLACK;
    uint8_t in = sunken ? C_BLACK    : C_DARKGRAY;

    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, w, 1, tl);
    fill_rect(x, y, 1, h, tl);
    fill_rect(x, y + h - 1, w, 1, br);
    fill_rect(x + w - 1, y, 1, h, br);
    if (!sunken && w > 2 && h > 2) {
        fill_rect(x + 1, y + h - 2, w - 2, 1, in);
        fill_rect(x + w - 2, y + 1, 1, h - 2, in);
    }
}

static void draw_char(int x, int y, unsigned char ch, uint8_t color) {
    if (!wm_font_loaded) return;
    const uint8_t* glyph = &wm_font[ch * 16];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char* str, uint8_t color) {
    if (!str) return;
    while (*str) {
        draw_char(x, y, (unsigned char)*str++, color);
        x += 8;
    }
}

static void draw_text_clipped(int x, int y, const char* str, int max_chars, uint8_t color) {
    if (!str) return;
    int count = 0;
    while (*str && count < max_chars) {
        draw_char(x, y, (unsigned char)*str++, color);
        x += 8;
        count++;
    }
}

static void clear_desktop(uint8_t color) {
    for (int p = 0; p < 4; p++) {
        uint8_t mask = (color & (1 << p)) ? 0xFF : 0x00;
        u_memset(vga_planes[p], mask, TOTAL_BYTES);
    }
}

static void flip_vga(void) {
    for (int p = 0; p < 4; p++) {
        vga_outw(0x3C4, (uint16_t)((1 << (p + 8)) | 0x02));
        u_memcpy((void*)vram, vga_planes[p], TOTAL_BYTES);
    }
}

// ====================================================================
// ВСТРОЕННЫЙ FALLBACK-ТЕРМИНАЛ (ЕСЛИ /bin/term ОТСУТСТВУЕТ НА ДИСКЕ)
// ====================================================================
static int  term_open = 0;
static int  term_x = 140, term_y = 70;
static int  term_dragging = 0, term_drag_dx = 0, term_drag_dy = 0;
static char term_buf[TERM_ROWS][TERM_COLS + 1];
static int  term_cur_row = 0;
static int  term_cur_col = 0;
static char term_cmd[TERM_COLS];
static int  term_cmd_len = 0;

static void term_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        u_memset(term_buf[r], ' ', TERM_COLS);
        term_buf[r][TERM_COLS] = '\0';
    }
    term_cur_row = 0;
    term_cur_col = 0;
}

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        u_memcpy(term_buf[r], term_buf[r + 1], TERM_COLS);
    }
    u_memset(term_buf[TERM_ROWS - 1], ' ', TERM_COLS);
    term_cur_row = TERM_ROWS - 1;
    term_cur_col = 0;
}

static void term_putc(char c) {
    if (c == '\r') return;
    if (c == '\n') {
        term_cur_col = 0;
        term_cur_row++;
        if (term_cur_row >= TERM_ROWS) term_scroll();
        return;
    }
    if (c == '\b') {
        if (term_cur_col > 0) {
            term_cur_col--;
            term_buf[term_cur_row][term_cur_col] = ' ';
        }
        return;
    }
    if (term_cur_col >= TERM_COLS) {
        term_cur_col = 0;
        term_cur_row++;
        if (term_cur_row >= TERM_ROWS) term_scroll();
    }
    term_buf[term_cur_row][term_cur_col++] = c;
}

static void term_print(const char* s) {
    if (!s) return;
    while (*s) term_putc(*s++);
}

static void term_printn(const char* s) {
    term_print(s);
    term_putc('\n');
}

static void term_prompt(void) {
    char cwd[64];
    if (getcwd(cwd, sizeof(cwd)) != 0) u_strncpy(cwd, "/", sizeof(cwd));
    term_print(getuid() == 0 ? "root@" : "user@");
    term_print(cwd);
    term_print("$ ");
}

static void term_open_builtin(void) {
    if (!term_open) {
        term_open = 1;
        term_clear();
        term_cmd_len = 0;
        term_cmd[0] = '\0';
        term_printn("Photon Desktop Terminal v1.0");
        term_printn("Commands: help ls cd pwd cat mkdir rm exit");
        term_prompt();
    }
}

static void term_exec_command(char* line) {
    char* argv[16];
    int argc = 0;
    char* p = line;

    while (*p && argc < 15) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    if (argc == 0) return;

    const char* cmd = argv[0];

    if (u_strcmp(cmd, "help") == 0) {
        term_printn("Built-in: help clear ls cd pwd cat echo");
        term_printn("          mkdir touch rm whoami exit");
    } else if (u_strcmp(cmd, "clear") == 0 || u_strcmp(cmd, "cls") == 0) {
        term_clear();
    } else if (u_strcmp(cmd, "exit") == 0) {
        term_open = 0;
        return;
    } else if (u_strcmp(cmd, "pwd") == 0) {
        char cwd[64];
        if (getcwd(cwd, sizeof(cwd)) != 0) u_strncpy(cwd, "/", sizeof(cwd));
        term_printn(cwd);
    } else if (u_strcmp(cmd, "whoami") == 0) {
        term_printn(getuid() == 0 ? "root" : "user");
    } else if (u_strcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) term_putc(' ');
            term_print(argv[i]);
        }
        term_putc('\n');
    } else if (u_strcmp(cmd, "cd") == 0) {
        const char* target = (argc > 1) ? argv[1] : "/home";
        if (chdir(target) < 0) {
            term_print("cd: not found: ");
            term_printn(target);
        }
    } else if (u_strcmp(cmd, "ls") == 0) {
        char path[64];
        if (argc > 1) u_strncpy(path, argv[1], sizeof(path));
        else if (getcwd(path, sizeof(path)) != 0) u_strncpy(path, "/", sizeof(path));

        int fd = open(path, READ);
        if (fd < 0) {
            term_printn("ls: cannot open dir");
        } else {
            dirent_t d;
            uint32_t idx = 0;
            while (readdir(fd, idx++, &d) == 0) {
                if (d.name[0] == '\0') break;
                term_print(d.name);
                if (d.type == TYPE_DIR) term_putc('/');
                term_putc(' ');
            }
            term_putc('\n');
            close(fd);
        }
    } else if (u_strcmp(cmd, "cat") == 0) {
        if (argc < 2) {
            term_printn("usage: cat <file>");
        } else {
            int fd = open(argv[1], READ);
            if (fd < 0) {
                term_printn("cat: file not found");
            } else {
                char buf[65];
                int n;
                while ((n = read(fd, buf, 64)) > 0) {
                    for (int i = 0; i < n; i++) term_putc(buf[i]);
                }
                term_putc('\n');
                close(fd);
            }
        }
    } else if (u_strcmp(cmd, "mkdir") == 0) {
        if (argc < 2 || mkdir(argv[1]) < 0) term_printn("mkdir failed");
    } else if (u_strcmp(cmd, "touch") == 0) {
        if (argc < 2) term_printn("usage: touch <file>");
        else {
            int fd = open(argv[1], WRITE | CREATE);
            if (fd >= 0) close(fd);
            else term_printn("touch failed");
        }
    } else if (u_strcmp(cmd, "rm") == 0) {
        if (argc < 2 || unlink(argv[1]) < 0) term_printn("rm failed");
    } else {
        char exec_path[64];
        if (cmd[0] == '/') u_strncpy(exec_path, cmd, sizeof(exec_path));
        else u_path_join(exec_path, "/bin", cmd, sizeof(exec_path));

        stat_t st;
        if (stat(exec_path, &st) == 0 && st.type == TYPE_FILE) {
            int is_gui = (u_strcmp(cmd, "calc") == 0 ||
                          u_strcmp(cmd, "explorer") == 0 ||
                          u_strcmp(cmd, "notepad") == 0 ||
                          u_strcmp(cmd, "sysmon") == 0 ||
                          u_strcmp(cmd, "term") == 0 ||
                          u_strcmp(cmd, "pc") == 0 ||
                          u_strcmp(cmd, "pt") == 0);
            if (is_gui) {
                int pid = exec(exec_path, argv);
                if (pid > 0) {
                    char pbuf[16];
                    u_format_num((uint32_t)pid, pbuf, sizeof(pbuf));
                    term_print("Started PID ");
                    term_printn(pbuf);
                } else {
                    term_printn("exec failed");
                }
            } else {
                int32_t out_pipe[2] = {-1, -1};
                int32_t in_pipe[2]  = {-1, -1};
                int has_out = (pipe(out_pipe) == 0);
                int has_in  = (pipe(in_pipe) == 0);
                if (has_in) {
                    close(in_pipe[1]);
                    dup2(in_pipe[0], STDIN);
                }
                if (has_out) {
                    dup2(out_pipe[1], STDOUT);
                    dup2(out_pipe[1], STDERR);
                }

                int pid = exec(exec_path, argv);

                if (has_out) {
                    close(STDOUT);
                    close(STDERR);
                    close(out_pipe[1]);
                }
                if (has_in) {
                    close(STDIN);
                    close(in_pipe[0]);
                }

                if (pid > 0) {
                    int status = 0;
                    waitpid(pid, &status);
                    if (has_out) {
                        char rbuf[128];
                        int in_esc = 0;
                        while (1) {
                            int n = read(out_pipe[0], rbuf, sizeof(rbuf));
                            if (n <= 0) break;
                            for (int k = 0; k < n; k++) {
                                char ch = rbuf[k];
                                if (in_esc) {
                                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) in_esc = 0;
                                    continue;
                                }
                                if (ch == '\033') { in_esc = 1; continue; }
                                term_putc(ch);
                            }
                        }
                    }
                } else {
                    term_printn("exec failed");
                }
                if (has_out) close(out_pipe[0]);
            }
        } else {
            term_print("Unknown: ");
            term_printn(cmd);
        }
    }
}

static void term_handle_key(char key) {
    if (key == '\r' || key == '\n') {
        term_putc('\n');
        term_cmd[term_cmd_len] = '\0';
        term_exec_command(term_cmd);
        term_cmd_len = 0;
        term_cmd[0] = '\0';
        if (term_open) term_prompt();
    } else if (key == '\b' || key == 127) {
        if (term_cmd_len > 0) {
            term_cmd_len--;
            term_cmd[term_cmd_len] = '\0';
            term_putc('\b');
        }
    } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126) {
        if (term_cmd_len < TERM_COLS - 1) {
            term_cmd[term_cmd_len++] = key;
            term_cmd[term_cmd_len] = '\0';
            term_putc(key);
        }
    }
}

static void draw_term_window(void) {
    if (!term_open) return;

    fill_rect(term_x + 3, term_y + 3, TERM_W, TERM_H + 19, C_DARKGRAY);
    draw_bevel(term_x - 2, term_y - 2, TERM_W + 4, TERM_H + 23, C_LIGHTGRAY, 0);

    fill_rect(term_x, term_y, TERM_W, 18, C_BLUE);
    draw_text(term_x + 6, term_y + 1, "Terminal (Built-in)", C_WHITE);

    draw_bevel(term_x + TERM_W - 16, term_y + 2, 14, 14, C_LIGHTRED, 0);
    draw_char(term_x + TERM_W - 13, term_y + 1, 'X', C_WHITE);

    int sy = term_y + 19;
    fill_rect(term_x, sy, TERM_W, TERM_H, C_BLACK);

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = term_buf[r][c];
            if (ch != ' ' && ch != '\0') {
                draw_char(term_x + 4 + c * 8, sy + 4 + r * 16, (unsigned char)ch, C_LIGHTGREEN);
            }
        }
    }

    int cur_x = term_x + 4 + term_cur_col * 8;
    int cur_y = sy + 4 + term_cur_row * 16;
    fill_rect(cur_x, cur_y + 13, 8, 2, C_WHITE);
}

// ====================================================================
// ЗАПУСК ПРИЛОЖЕНИЙ РАБОЧЕГО СТОЛА И МЕНЮ START
// ====================================================================
static void launch_app(const char* bin_path, int fallback_term) {
    stat_t st;
    if (stat(bin_path, &st) == 0 && st.type == TYPE_FILE) {
        char* argv[2];
        argv[0] = (char*)bin_path;
        argv[1] = NULL;
        exec(bin_path, argv);
    } else if (fallback_term) {
        term_open_builtin();
    }
}

static void activate_menu_item(int idx) {
    start_menu_open = 0;
    switch (idx) {
        case 0: launch_app("/bin/explorer", 0); break;
        case 1: launch_app("/bin/term", 1);     break;
        case 2: launch_app("/bin/notepad", 0);  break;
        case 3: launch_app("/bin/calc", 0);     break;
        case 4: launch_app("/bin/sysmon", 0);   break;
        case 5: wm_should_exit = 1;             break;
        case 6: reboot();                       break;
    }
}

// ====================================================================
// УПРАВЛЕНИЕ ОКНАМИ (ПОИСК, ФОКУС, Z-ORDER, ОЧИСТКА)
// ====================================================================
static void focus_window(int idx) {
    if (idx < 0 || idx >= client_count) return;
    if (idx == client_count - 1) return;

    wm_slot_t tmp = clients[idx];
    for (int i = idx; i < client_count - 1; i++) {
        clients[i] = clients[i + 1];
    }
    clients[client_count - 1] = tmp;
}

static void cleanup_closed_windows(void) {
    int i = 0;
    while (i < client_count) {
        window_header_t* h = clients[i].header;
        if (!h || !h->active) {
            shm_detach(clients[i].shmid);
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
        } else {
            i++;
        }
    }
}

static void check_new_windows(void) {
    // 1. Прямое обнаружение новых SHM-окон в памяти (слот 0 зарезервирован под lock самого WM)
    for (int id = 1; id < 32; id++) {
        if (client_count >= MAX_WINDOWS) break;

        int already_attached = 0;
        for (int i = 0; i < client_count; i++) {
            if (clients[i].shmid == id) {
                already_attached = 1;
                break;
            }
        }
        if (already_attached) continue;

        window_header_t* hdr = (window_header_t*)shm_attach(id);
        if (hdr) {
            // Если клиент ещё не закончил инициализацию заголовка или окно уже закрыто — отключаемся
            if (hdr->active != 1 || hdr->closed || hdr->w <= 0 || hdr->h <= 0) {
                shm_detach(id);
                continue;
            }
            clients[client_count].header = hdr;
            clients[client_count].pixels = (uint8_t*)(hdr + 1);
            clients[client_count].shmid = id;
            client_count++;
        }
    }
}

static void send_key_to_focused_window(char key) {
    if (term_open) {
        term_handle_key(key);
        return;
    }
    if (client_count <= 0) return;
    window_header_t* h = clients[client_count - 1].header;
    if (!h || !h->active) return;

    uint8_t next = (h->evt_head + 1) % MAX_WIN_EVENTS;
    if (next != h->evt_tail) {
        h->events[h->evt_head] = (shm_event_t){
            .type = EVT_KEY,
            .x = (int16_t)(unsigned char)key,
            .y = 0,
            .btn = (uint8_t)key
        };
        h->evt_head = next;
    }
}

// ====================================================================
// ОТРИСОВКА ЯРЛЫКОВ, ПАНЕЛИ ЗАДАЧ, МЕНЮ START И КУРСОРА
// ====================================================================
static const char* desk_labels[DESK_COUNT] = {
    "Explorer",
    "Terminal",
    "Notepad",
    "Calc",
    "SysMon"
};

static void draw_desktop_icons(void) {
    for (int i = 0; i < DESK_COUNT; i++) {
        int ix = DESK_ICON_X;
        int iy = 14 + i * DESK_STEP_Y;

        // Тень и основа иконки
        fill_rect(ix + 2, iy + 2, DESK_ICON_W, DESK_ICON_H, C_DARKGRAY);
        draw_bevel(ix, iy, DESK_ICON_W, DESK_ICON_H, C_LIGHTGRAY, 0);

        if (i == 0) {
            // Explorer: жёлтая папка
            fill_rect(ix + 6, iy + 7, 14, 4, C_BROWN);
            fill_rect(ix + 6, iy + 11, 32, 20, C_YELLOW);
            fill_rect(ix + 6, iy + 11, 32, 2, C_WHITE);
        } else if (i == 1) {
            // Terminal: чёрный экран с >_
            fill_rect(ix + 5, iy + 5, 34, 5, C_BLUE);
            fill_rect(ix + 5, iy + 10, 34, 22, C_BLACK);
            draw_text(ix + 8, iy + 13, ">_", C_LIGHTGREEN);
        } else if (i == 2) {
            // Notepad: белый лист с текстом
            fill_rect(ix + 9, iy + 5, 26, 28, C_WHITE);
            fill_rect(ix + 9, iy + 5, 26, 4, C_BLUE);
            for (int r = 0; r < 4; r++) {
                fill_rect(ix + 12, iy + 13 + r * 4, 20, 1, C_DARKGRAY);
            }
        } else if (i == 3) {
            // Calc: калькулятор с кнопками
            fill_rect(ix + 8, iy + 4, 28, 30, C_DARKGRAY);
            fill_rect(ix + 11, iy + 7, 22, 7, C_LIGHTGREEN);
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < 3; c++)
                    fill_rect(ix + 11 + c * 8, iy + 17 + r * 7, 6, 5, C_WHITE);
        } else if (i == 4) {
            // SysMon: график
            fill_rect(ix + 5, iy + 6, 34, 26, C_BLACK);
            fill_rect(ix + 9, iy + 22, 5, 8, C_LIGHTGREEN);
            fill_rect(ix + 17, iy + 15, 5, 15, C_LIGHTGREEN);
            fill_rect(ix + 25, iy + 10, 5, 20, C_YELLOW);
            fill_rect(ix + 33, iy + 18, 4, 12, C_LIGHTRED);
        }

        // Подпись под иконкой с тенью
        int len = u_strlen(desk_labels[i]);
        int tx = ix + (DESK_ICON_W - len * 8) / 2;
        int ty = iy + DESK_ICON_H + 4;
        draw_text(tx + 1, ty + 1, desk_labels[i], C_BLACK);
        draw_text(tx, ty, desk_labels[i], C_WHITE);
    }
}

static void draw_taskbar(void) {
    // Основа панели задач
    fill_rect(0, TASKBAR_Y, SCREEN_W, TASKBAR_H, C_LIGHTGRAY);
    fill_rect(0, TASKBAR_Y, SCREEN_W, 1, C_WHITE);

    // Кнопка [Start]
    draw_bevel(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, C_LIGHTGRAY, start_menu_open);

    // 4-цветный логотип Windows/Photon внутри кнопки Start
    int lx = START_BTN_X + 5;
    int ly = START_BTN_Y + 5;
    fill_rect(lx,     ly,     5, 5, C_LIGHTRED);
    fill_rect(lx + 6, ly,     5, 5, C_LIGHTGREEN);
    fill_rect(lx,     ly + 6, 5, 5, C_LIGHTBLUE);
    fill_rect(lx + 6, ly + 6, 5, 5, C_YELLOW);
    draw_text(START_BTN_X + 20, START_BTN_Y + 3, "Start", C_BLACK);

    // Кнопки открытых окон на панели задач
    int bx = START_BTN_X + START_BTN_W + 6;
    for (int i = 0; i < client_count; i++) {
        window_header_t* h = clients[i].header;
        if (!h || !h->active) continue;
        if (bx + 104 > SCREEN_W - 84) break;

        int is_top = (i == client_count - 1 && !term_open);
        draw_bevel(bx, START_BTN_Y, 100, START_BTN_H, C_LIGHTGRAY, is_top);
        draw_text_clipped(bx + 6, START_BTN_Y + 3, h->title, 11, C_BLACK);
        bx += 104;
    }

    // Системный трей с часами CMOS справа
    int tray_w = 76;
    int tray_x = SCREEN_W - tray_w - 4;
    draw_bevel(tray_x, START_BTN_Y, tray_w, START_BTN_H, C_LIGHTGRAY, 1);

    uint8_t hh = 0, mm = 0, ss = 0;
    get_time(&hh, &mm, &ss);
    char time_str[9];
    time_str[0] = '0' + (hh / 10) % 10;
    time_str[1] = '0' + (hh % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (mm / 10) % 10;
    time_str[4] = '0' + (mm % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (ss / 10) % 10;
    time_str[7] = '0' + (ss % 10);
    time_str[8] = '\0';
    draw_text(tray_x + 6, START_BTN_Y + 3, time_str, C_BLACK);
}

static const char* menu_labels[MENU_ITEMS] = {
    "Explorer",
    "Terminal",
    "Notepad",
    "Calculator",
    "System Monitor",
    "Exit to Shell",
    "Reboot System"
};

static void draw_start_menu(int mx, int my) {
    if (!start_menu_open) return;

    draw_bevel(MENU_X, MENU_Y, MENU_W, MENU_H, C_LIGHTGRAY, 0);

    // Боковая вертикальная полоса в стиле Win95
    fill_rect(MENU_X + 3, MENU_Y + 3, 24, MENU_H - 6, C_BLUE);
    const char* banner = "PHOTON";
    for (int i = 0; i < 6; i++) {
        draw_char(MENU_X + 11, MENU_Y + 14 + i * 22, (unsigned char)banner[i], C_WHITE);
    }

    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = MENU_Y + 4 + i * MENU_ITEM_H;
        int ix = MENU_X + 30;
        int iw = MENU_W - 34;

        int hovered = (mx >= ix && mx < ix + iw && my >= iy && my < iy + MENU_ITEM_H);
        if (hovered) {
            fill_rect(ix, iy, iw, MENU_ITEM_H, C_BLUE);
        }

        // Разделительная линия перед системными пунктами
        if (i == 5) {
            fill_rect(ix + 2, iy - 1, iw - 4, 1, C_DARKGRAY);
        }

        // Мини-иконка пункта меню
        uint8_t icon_col = (i < 5) ? C_YELLOW : C_LIGHTRED;
        fill_rect(ix + 6, iy + 6, 10, 10, icon_col);
        draw_text(ix + 22, iy + 4, menu_labels[i], hovered ? C_WHITE : C_BLACK);
    }
}

// Классический стрелочный курсор 11x16 (1 = чёрная граница, 2 = белая заливка)
static const uint8_t cursor_bmp[16][11] = {
    {1,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,1,2,2,1,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,1,1,1,0,0,0}
};

static void draw_cursor(int mx, int my) {
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 11; c++) {
            uint8_t px = cursor_bmp[r][c];
            if (px == 1) put_pixel(mx + c, my + r, C_BLACK);
            else if (px == 2) put_pixel(mx + c, my + r, C_WHITE);
        }
    }
}

int main(int argc, char* argv[]) {
    if (getuid() != 0) {
        print("wm: Root permissions required (sudo wm)\n");
        exit(1);
    }

    // Удаляем старый скрытый файл /sys/.wm с диска, если он остался с прошлых запусков
    unlink("/sys/.wm");

    // Резервируем SHM-сегмент 0 в ОЗУ как индикатор запущенного WM (active = 2)
    int wm_lock_shmid = shm_create(sizeof(window_header_t));
    window_header_t* wm_lock_hdr = (wm_lock_shmid >= 0) ? (window_header_t*)shm_attach(wm_lock_shmid) : NULL;
    if (!wm_lock_hdr || wm_lock_shmid != 0) {
        print("wm: Already running or SHM slot 0 busy\n");
        if (wm_lock_hdr) shm_detach(wm_lock_shmid);
        exit(1);
    }
    wm_lock_hdr->pid = getpid();
    wm_lock_hdr->active = 2;

    for (int p = 0; p < 4; p++) {
        vga_planes[p] = (uint8_t*)malloc(TOTAL_BYTES);
        if (vga_planes[p] == 0) {
            print("wm: Failed to allocate VGA pages\n");
            exit(1);
        }
    }

    load_cp437_file();
    mkdir(WM_ANNOUNCE_DIR);
    userland_set_mode_640x480();

    int mx = SCREEN_W / 2, my = SCREEN_H / 2;
    uint8_t last_btn = 0;
    int drag_idx = -1, drag_dx = 0, drag_dy = 0;
    int frame = 0;

    while (!wm_should_exit) {
        if ((frame++ & 7) == 0) {
            cleanup_closed_windows();
            check_new_windows();
        }

        // 1. Опрос аппаратной мыши
        mouse_state_t ms;
        if (get_mouse(&ms) == 0) {
            mx += ms.dx;
            my -= ms.dy;

            if (mx < 0) mx = 0;
            if (mx >= SCREEN_W) mx = SCREEN_W - 1;
            if (my < 0) my = 0;
            if (my >= SCREEN_H) my = SCREEN_H - 1;

            int left_down = ms.buttons & 1;
            int clicked = left_down && !(last_btn & 1);
            int handled = 0;

            if (clicked) {
                // 1) Проверка клика по кнопке [Start]
                if (mx >= START_BTN_X && mx < START_BTN_X + START_BTN_W &&
                    my >= START_BTN_Y && my < START_BTN_Y + START_BTN_H) {
                    start_menu_open = !start_menu_open;
                    handled = 1;
                }
                // 2) Проверка клика по открытому меню Start
                else if (start_menu_open &&
                         mx >= MENU_X && mx < MENU_X + MENU_W &&
                         my >= MENU_Y && my < MENU_Y + MENU_H) {
                    int rel_y = my - (MENU_Y + 4);
                    if (rel_y >= 0) {
                        int item = rel_y / MENU_ITEM_H;
                        if (item >= 0 && item < MENU_ITEMS) {
                            activate_menu_item(item);
                        }
                    }
                    handled = 1;
                }
                // Любой клик мимо меню закрывает Start Menu
                else if (start_menu_open) {
                    start_menu_open = 0;
                }

                // 3) Клик по кнопкам окон на панели задач
                if (!handled && my >= TASKBAR_Y) {
                    int bx = START_BTN_X + START_BTN_W + 6;
                    for (int i = 0; i < client_count; i++) {
                        window_header_t* h = clients[i].header;
                        if (!h || !h->active) continue;
                        if (mx >= bx && mx < bx + 100 &&
                            my >= START_BTN_Y && my < START_BTN_Y + START_BTN_H) {
                            focus_window(i);
                            handled = 1;
                            break;
                        }
                        bx += 104;
                    }
                    handled = 1;
                }

                // 4) Клик по встроенному терминалу (если он открыт)
                if (!handled && term_open) {
                    if (mx >= term_x + TERM_W - 16 && mx <= term_x + TERM_W &&
                        my >= term_y && my <= term_y + 16) {
                        term_open = 0;
                        handled = 1;
                    } else if (mx >= term_x && mx <= term_x + TERM_W &&
                               my >= term_y && my <= term_y + 18) {
                        term_dragging = 1;
                        term_drag_dx = mx - term_x;
                        term_drag_dy = my - term_y;
                        handled = 1;
                    } else if (mx >= term_x && mx <= term_x + TERM_W &&
                               my >= term_y && my <= term_y + 19 + TERM_H) {
                        handled = 1;
                    }
                }

                // 5) Клик по SHM-окнам (сверху вниз по Z-order)
                for (int i = client_count - 1; !handled && i >= 0; i--) {
                    window_header_t* h = clients[i].header;
                    if (!h || !h->active) continue;

                    if (mx >= h->x && mx <= h->x + h->w &&
                        my >= h->y && my <= h->y + 19 + h->h) {
                        // Поднимаем окно наверх
                        focus_window(i);
                        int top_idx = client_count - 1;
                        h = clients[top_idx].header;

                        // Клик по кнопке [X]
                        if (mx >= h->x + h->w - 16 && mx <= h->x + h->w &&
                            my >= h->y && my <= h->y + 16) {
                            h->closed = 1;
                        }
                        // Захват за заголовок окна
                        else if (my >= h->y && my <= h->y + 18) {
                            drag_idx = top_idx;
                            drag_dx = mx - h->x;
                            drag_dy = my - h->y;
                        }
                        // Клик внутри клиентской области
                        else {
                            int cx = mx - h->x;
                            int cy = my - (h->y + 19);
                            if (cx >= 0 && cx < h->w && cy >= 0 && cy < h->h) {
                                uint8_t next = (h->evt_head + 1) % MAX_WIN_EVENTS;
                                if (next != h->evt_tail) {
                                    h->events[h->evt_head] = (shm_event_t){
                                        .type = EVT_CLICK,
                                        .x = (int16_t)cx,
                                        .y = (int16_t)cy,
                                        .btn = 1
                                    };
                                    h->evt_head = next;
                                }
                            }
                        }
                        handled = 1;
                        break;
                    }
                }

                // 6) Клик по ярлыкам рабочего стола
                if (!handled) {
                    for (int i = 0; i < DESK_COUNT; i++) {
                        int ix = DESK_ICON_X;
                        int iy = 14 + i * DESK_STEP_Y;
                        if (mx >= ix - 6 && mx <= ix + DESK_ICON_W + 6 &&
                            my >= iy && my <= iy + DESK_ICON_H + 20) {
                            activate_menu_item(i);
                            break;
                        }
                    }
                }
            }

            if (!left_down) {
                drag_idx = -1;
                term_dragging = 0;
            }
            if (term_dragging) {
                term_x = mx - term_drag_dx;
                term_y = my - term_drag_dy;
                if (term_y < 2) term_y = 2;
                if (term_y > TASKBAR_Y - 24) term_y = TASKBAR_Y - 24;
            } else if (drag_idx >= 0 && drag_idx < client_count) {
                int nx = mx - drag_dx;
                int ny = my - drag_dy;
                if (ny < 2) ny = 2;
                if (ny > TASKBAR_Y - 24) ny = TASKBAR_Y - 24;
                clients[drag_idx].header->x = nx;
                clients[drag_idx].header->y = ny;
            }
            last_btn = ms.buttons;
        }

        // 2. Опрос клавиатуры
        char key = poll_key();
        if (key != 0) {
            if (key == KEY_WIN) {
                // Клавиша Win открывает/закрывает меню Start
                start_menu_open = !start_menu_open;
            } else if (key == 27) {
                // ESC закрывает меню Start или выходит из WM
                if (start_menu_open) start_menu_open = 0;
                else break;
            } else {
                send_key_to_focused_window(key);
            }
        }

        // 3. Композитинг сцены
        clear_desktop(C_CYAN);
        draw_desktop_icons();

        // Отрисовка клиентских SHM-окон
        for (int i = 0; i < client_count; i++) {
            window_header_t* h = clients[i].header;
            if (!h || !h->active) continue;

            int is_focused = (i == client_count - 1 && !term_open);
            uint8_t title_bg = is_focused ? C_BLUE : C_DARKGRAY;
            uint8_t title_fg = is_focused ? C_WHITE : C_LIGHTGRAY;

            // Тень и 3D-рамка окна
            fill_rect(h->x + 3, h->y + 3, h->w + 4, h->h + 23, C_DARKGRAY);
            draw_bevel(h->x - 2, h->y - 2, h->w + 4, h->h + 23, C_LIGHTGRAY, 0);

            // Заголовок и кнопка [X]
            fill_rect(h->x, h->y, h->w, 18, title_bg);
            draw_text_clipped(h->x + 5, h->y + 1, h->title, (h->w - 24) / 8, title_fg);
            draw_bevel(h->x + h->w - 16, h->y + 2, 14, 14, C_LIGHTRED, 0);
            draw_char(h->x + h->w - 13, h->y + 1, 'X', C_WHITE);

            // Блит пикселей клиентского холста
            uint8_t* canvas = clients[i].pixels;
            int sy = h->y + 19;
            for (int cy = 0; cy < h->h; cy++) {
                for (int cx = 0; cx < h->w; cx++) {
                    put_pixel(h->x + cx, sy + cy, canvas[cy * h->w + cx]);
                }
            }
        }

        // Отрисовка встроенного терминала, панели задач, меню Start и курсора
        draw_term_window();
        draw_taskbar();
        draw_start_menu(mx, my);
        draw_cursor(mx, my);

        flip_vga();
        sleep(16);
    }

    // При выходе закрываем клиентские SHM-сегменты и завершаем все процессы сессии WM
    int wm_pid = getpid();
    for (int i = 0; i < client_count; i++) {
        if (clients[i].header) {
            clients[i].header->closed = 1;
            clients[i].header->active = 0;
        }
        shm_detach(clients[i].shmid);
    }
    client_count = 0;

    // Освобождаем SHM-сегмент 0 (индикатор запущенного WM)
    wm_lock_hdr->active = 0;
    shm_detach(wm_lock_shmid);

    // Принудительно останавливаем все процессы, порождённые в течение сессии WM (PID > wm_pid)
    for (int p = wm_pid + 1; p < wm_pid + 64; p++) {
        send_signal(p, 9);
    }

    // Возврат в текстовый режим + запись шрифта /sys/cp437.bm в Plane 2
    userland_set_mode_text();
    restore_vga_font_cp437();
    clear_screen(0x07);
    gotoxy(0, 0);
    exit(0);
}
