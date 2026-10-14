#include "photon.h"
#include "ulibc.h"
#include "vga_12.h"
#include "gui_shm.h"

#define MAX_WINDOWS     16
#define SCREEN_W        640
#define SCREEN_H        480
#define ROW_BYTES       (SCREEN_W / 8)
#define TOTAL_BYTES     (ROW_BYTES * SCREEN_H)

// SHM-слот 0 — блокировка «WM запущен» (её проверяет gui_create_window)
#define WM_LOCK_SHMID   0
// Столько SHM-слотов есть в ядре (MAX_SHM_REGIONS). Окна ищем среди 1..N-1
#define SHM_SLOT_COUNT  32

// Заголовок окна и клиентская область (сдвиг клиента относительно y окна)
#define TITLE_H         18
#define CLIENT_OFFSET_Y 19
#define CLOSE_BTN_X_OFF 16   // кнопка [X] в правом верхнем углу
#define CLOSE_BTN_Y_OFF 2
#define CLOSE_BTN_SIZE  14

// Панель задач (Taskbar)
#define TASKBAR_H       28
#define TASKBAR_Y       (SCREEN_H - TASKBAR_H)
#define START_BTN_X     4
#define START_BTN_Y     (TASKBAR_Y + 3)
#define START_BTN_W     68
#define START_BTN_H     22
#define TASK_BTN_X0     (START_BTN_X + START_BTN_W + 6)
#define TASK_BTN_W      100
#define TASK_BTN_STEP   104
#define TRAY_W          76
#define TRAY_RESERVED   84   // место справа под трей, кнопки окон туда не лезут

// Меню Пуск (Start Menu)
#define MENU_X          4
#define MENU_W          184
#define MENU_ITEM_H     24
#define MENU_ITEMS      7
#define MENU_H          (MENU_ITEMS * MENU_ITEM_H + 8)
#define MENU_Y          (TASKBAR_Y - MENU_H)
#define MENU_ITEMS_Y    (MENU_Y + 4)

// Ярлыки на рабочем столе
#define DESK_ICON_X     18
#define DESK_ICON_W     44
#define DESK_ICON_H     38
#define DESK_STEP_Y     78
#define DESK_COUNT      5

// Приложения, которые запускаются из меню и с рабочего стола (порядок важен)
#define APP_EXPLORER    0
#define APP_TERM        1
#define APP_NOTEPAD     2
#define APP_CALC        3
#define APP_SYSMON      4
#define APP_COUNT       5

// Пункты меню Start: первые APP_COUNT — программы, затем выход и перезагрузка
#define MENU_EXIT       5
#define MENU_REBOOT     6

#define TOAST_FRAMES    180  // сообщение внизу экрана (~3 c)
#define TOAST_MAX       40

typedef struct {
    window_header_t* header;
    uint8_t*         pixels;
    int32_t          shmid;
} wm_slot_t;

static wm_slot_t clients[MAX_WINDOWS];   // clients[client_count-1] — верхнее окно
static int client_count = 0;

static uint8_t* vga_planes[4];
static volatile uint8_t* vram = (volatile uint8_t*)0xA0000;

static uint8_t wm_font[4096];
static int wm_font_loaded = 0;
static int start_menu_open = 0;
static int wm_should_exit = 0;

static int mx = SCREEN_W / 2;
static int my = SCREEN_H / 2;
static uint8_t last_btn = 0;

// Перетаскивание окна: храним shmid, а не индекс — индекс меняется при смене z-order
static int32_t drag_shmid = -1;
static int drag_dx = 0, drag_dy = 0;

static char toast_text[TOAST_MAX];
static int  toast_ttl = 0;

static const char* const desktop_app_paths[APP_COUNT] = {
    "/bin/explorer",
    "/bin/term",
    "/bin/notepad",
    "/bin/calc",
    "/bin/sysmon"
};

static const char* const desk_labels[DESK_COUNT] = {
    "Explorer",
    "Terminal",
    "Notepad",
    "Calc",
    "SysMon"
};

static const char* const menu_labels[MENU_ITEMS] = {
    "Explorer",
    "Terminal",
    "Notepad",
    "Calculator",
    "System Monitor",
    "Exit to Shell",
    "Reboot System"
};

// ====================================================================
// ШРИФТ И ВОССТАНОВЛЕНИЕ ТЕКСТОВОГО РЕЖИМА
// ====================================================================

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

// Запись шрифта в VGA Plane 2 при возврате в текстовый режим
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

// ====================================================================
// РИСОВАНИЕ
// ====================================================================

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

// Строка не длиннее max_chars символов (защита от строк без '\0' в заголовке окна)
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

static void draw_desktop_icons(void) {
    for (int i = 0; i < DESK_COUNT; i++) {
        int ix = DESK_ICON_X;
        int iy = 14 + i * DESK_STEP_Y;

        // Тень и основа иконки
        fill_rect(ix + 2, iy + 2, DESK_ICON_W, DESK_ICON_H, C_DARKGRAY);
        draw_bevel(ix, iy, DESK_ICON_W, DESK_ICON_H, C_LIGHTGRAY, 0);

        if (i == APP_EXPLORER) {
            // Жёлтая папка
            fill_rect(ix + 6, iy + 7, 14, 4, C_BROWN);
            fill_rect(ix + 6, iy + 11, 32, 20, C_YELLOW);
            fill_rect(ix + 6, iy + 11, 32, 2, C_WHITE);
        } else if (i == APP_TERM) {
            // Чёрный экран с >_
            fill_rect(ix + 5, iy + 5, 34, 5, C_BLUE);
            fill_rect(ix + 5, iy + 10, 34, 22, C_BLACK);
            draw_text(ix + 8, iy + 13, ">_", C_LIGHTGREEN);
        } else if (i == APP_NOTEPAD) {
            // Белый лист с текстом
            fill_rect(ix + 9, iy + 5, 26, 28, C_WHITE);
            fill_rect(ix + 9, iy + 5, 26, 4, C_BLUE);
            for (int r = 0; r < 4; r++) {
                fill_rect(ix + 12, iy + 13 + r * 4, 20, 1, C_DARKGRAY);
            }
        } else if (i == APP_CALC) {
            // Калькулятор с кнопками
            fill_rect(ix + 8, iy + 4, 28, 30, C_DARKGRAY);
            fill_rect(ix + 11, iy + 7, 22, 7, C_LIGHTGREEN);
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < 3; c++)
                    fill_rect(ix + 11 + c * 8, iy + 17 + r * 7, 6, 5, C_WHITE);
        } else if (i == APP_SYSMON) {
            // График
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

static int taskbar_button_x(int i) {
    return TASK_BTN_X0 + i * TASK_BTN_STEP;
}

// Кнопка окна помещается левее трея
static int taskbar_button_visible(int i) {
    return taskbar_button_x(i) + TASK_BTN_STEP <= SCREEN_W - TRAY_RESERVED;
}

static void draw_taskbar(void) {
    // Основа панели задач
    fill_rect(0, TASKBAR_Y, SCREEN_W, TASKBAR_H, C_LIGHTGRAY);
    fill_rect(0, TASKBAR_Y, SCREEN_W, 1, C_WHITE);

    // Кнопка [Start]
    draw_bevel(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, C_LIGHTGRAY, start_menu_open);

    // 4-цветный логотип внутри кнопки Start
    int lx = START_BTN_X + 5;
    int ly = START_BTN_Y + 5;
    fill_rect(lx,     ly,     5, 5, C_LIGHTRED);
    fill_rect(lx + 6, ly,     5, 5, C_LIGHTGREEN);
    fill_rect(lx,     ly + 6, 5, 5, C_LIGHTBLUE);
    fill_rect(lx + 6, ly + 6, 5, 5, C_YELLOW);
    draw_text(START_BTN_X + 20, START_BTN_Y + 3, "Start", C_BLACK);

    // Кнопки открытых окон
    for (int i = 0; i < client_count; i++) {
        window_header_t* h = clients[i].header;
        if (!h || !h->active) continue;
        if (!taskbar_button_visible(i)) break;

        int is_top = (i == client_count - 1);
        draw_bevel(taskbar_button_x(i), START_BTN_Y, TASK_BTN_W, START_BTN_H, C_LIGHTGRAY, is_top);
        draw_text_clipped(taskbar_button_x(i) + 6, START_BTN_Y + 3, h->title, 11, C_BLACK);
    }

    // Трей с часами CMOS
    int tray_x = SCREEN_W - TRAY_W - 4;
    draw_bevel(tray_x, START_BTN_Y, TRAY_W, START_BTN_H, C_LIGHTGRAY, 1);

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

static void draw_toast(void) {
    if (toast_ttl <= 0) return;
    toast_ttl--;

    int len = u_strlen(toast_text);
    int w = len * 8 + 12;
    int x = START_BTN_X;
    int y = TASKBAR_Y - 22;
    fill_rect(x, y, w, 18, C_BLACK);
    draw_text(x + 6, y + 1, toast_text, C_YELLOW);
}

static void draw_start_menu(void) {
    if (!start_menu_open) return;

    draw_bevel(MENU_X, MENU_Y, MENU_W, MENU_H, C_LIGHTGRAY, 0);

    // Боковая вертикальная полоса в стиле Win95
    fill_rect(MENU_X + 3, MENU_Y + 3, 24, MENU_H - 6, C_BLUE);
    const char* banner = "PHOTON";
    for (int i = 0; i < 6; i++) {
        draw_char(MENU_X + 11, MENU_Y + 14 + i * 22, (unsigned char)banner[i], C_WHITE);
    }

    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = MENU_ITEMS_Y + i * MENU_ITEM_H;
        int ix = MENU_X + 30;
        int iw = MENU_W - 34;

        int hovered = (mx >= ix && mx < ix + iw && my >= iy && my < iy + MENU_ITEM_H);
        if (hovered) {
            fill_rect(ix, iy, iw, MENU_ITEM_H, C_BLUE);
        }

        // Разделительная линия перед системными пунктами
        if (i == MENU_EXIT) {
            fill_rect(ix + 2, iy - 1, iw - 4, 1, C_DARKGRAY);
        }

        // Мини-иконка пункта меню
        uint8_t icon_col = (i < MENU_EXIT) ? C_YELLOW : C_LIGHTRED;
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

static void draw_cursor(void) {
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 11; c++) {
            uint8_t px = cursor_bmp[r][c];
            if (px == 1) put_pixel(mx + c, my + r, C_BLACK);
            else if (px == 2) put_pixel(mx + c, my + r, C_WHITE);
        }
    }
}

static void draw_window(int idx) {
    window_header_t* h = clients[idx].header;
    int is_focused = (idx == client_count - 1);
    uint8_t title_bg = is_focused ? C_BLUE : C_DARKGRAY;
    uint8_t title_fg = is_focused ? C_WHITE : C_LIGHTGRAY;

    // Тень и 3D-рамка окна
    fill_rect(h->x + 3, h->y + 3, h->w + 4, h->h + 23, C_DARKGRAY);
    draw_bevel(h->x - 2, h->y - 2, h->w + 4, h->h + 23, C_LIGHTGRAY, 0);

    // Заголовок и кнопка [X]
    fill_rect(h->x, h->y, h->w, TITLE_H, title_bg);
    int max_title = (h->w - 24) / 8;
    if (max_title > (int)sizeof(h->title) - 1) max_title = sizeof(h->title) - 1;
    draw_text_clipped(h->x + 5, h->y + 1, h->title, max_title, title_fg);
    draw_bevel(h->x + h->w - CLOSE_BTN_X_OFF, h->y + CLOSE_BTN_Y_OFF,
               CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, C_LIGHTRED, 0);
    draw_char(h->x + h->w - 13, h->y + 1, 'X', C_WHITE);

    // Клиентский холст
    uint8_t* canvas = clients[idx].pixels;
    int sy = h->y + CLIENT_OFFSET_Y;
    for (int cy = 0; cy < h->h; cy++) {
        for (int cx = 0; cx < h->w; cx++) {
            put_pixel(h->x + cx, sy + cy, canvas[cy * h->w + cx]);
        }
    }
}

// ====================================================================
// УПРАВЛЕНИЕ ОКНАМИ
// ====================================================================

static int is_window_header(const window_header_t* h) {
    return h->active == 1 && !h->closed &&
           h->w > 0 && h->w <= SCREEN_W &&
           h->h > 0 && h->h <= SCREEN_H;
}

static int find_client_by_shmid(int32_t shmid) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].shmid == shmid) return i;
    }
    return -1;
}

// Отключаем SHM клиента и удаляем его из списка окон
static void remove_client(int idx) {
    shm_detach(clients[idx].shmid);
    for (int j = idx; j < client_count - 1; j++) {
        clients[j] = clients[j + 1];
    }
    client_count--;
}

// Окно, которое закрыто (active = 0) или закрывается (closed = 1), больше не показываем
static void cleanup_closed_windows(void) {
    int i = 0;
    while (i < client_count) {
        window_header_t* h = clients[i].header;
        if (!h || !h->active || h->closed) {
            remove_client(i);
        } else {
            i++;
        }
    }
}

// Ищем новые окна: клиент создаёт SHM-сегмент и выставляет active = 1 последним
static void discover_windows(void) {
    for (int32_t id = 1; id < SHM_SLOT_COUNT && client_count < MAX_WINDOWS; id++) {
        if (find_client_by_shmid(id) >= 0) continue;

        window_header_t* hdr = (window_header_t*)shm_attach(id);
        if (!hdr) continue;

        // Не окно (или ещё не инициализировано / уже закрыто) — отключаемся
        if (!is_window_header(hdr)) {
            shm_detach(id);
            continue;
        }

        clients[client_count].header = hdr;
        clients[client_count].pixels = (uint8_t*)(hdr + 1);
        clients[client_count].shmid = id;
        client_count++;
    }
}

// Поднимаем окно наверх (z-order) и возвращаем его новый индекс
static int raise_window(int idx) {
    if (idx < 0 || idx >= client_count) return -1;
    if (idx == client_count - 1) return idx;

    wm_slot_t tmp = clients[idx];
    for (int i = idx; i < client_count - 1; i++) {
        clients[i] = clients[i + 1];
    }
    clients[client_count - 1] = tmp;
    return client_count - 1;
}

static void post_event(window_header_t* h, uint8_t type, int x, int y, uint8_t btn) {
    uint8_t next = (h->evt_head + 1) % MAX_WIN_EVENTS;
    if (next == h->evt_tail) return;   // очередь клиента переполнена — событие теряем

    h->events[h->evt_head] = (shm_event_t){
        .type = type,
        .x = (int16_t)x,
        .y = (int16_t)y,
        .btn = btn
    };
    h->evt_head = next;
}

// Верхнее окно под курсором (с учётом заголовка) или -1
static int window_at(int x, int y) {
    for (int i = client_count - 1; i >= 0; i--) {
        window_header_t* h = clients[i].header;
        if (x >= h->x && x < h->x + h->w &&
            y >= h->y && y < h->y + CLIENT_OFFSET_Y + h->h) {
            return i;
        }
    }
    return -1;
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// ====================================================================
// ЗАПУСК ПРОГРАММ
// ====================================================================

static void set_toast(const char* msg) {
    u_strncpy(toast_text, msg, sizeof(toast_text));
    toast_ttl = TOAST_FRAMES;
}

static void launch_app(const char* path) {
    stat_t st;
    if (stat(path, &st) != 0 || st.type != TYPE_FILE) {
        char msg[TOAST_MAX];
        snprintf(msg, sizeof(msg), "Not found: %s", path);
        set_toast(msg);
        return;
    }

    char* argv[2];
    argv[0] = (char*)path;
    argv[1] = NULL;
    // Окно программы WM найдёт сам (владелец — header->pid); здесь важен только успех exec
    if (exec(path, argv) <= 0) {
        set_toast("Failed to start program");
    }
}

static void activate_menu_item(int idx) {
    start_menu_open = 0;
    if (idx >= 0 && idx < APP_COUNT) {
        launch_app(desktop_app_paths[idx]);
    } else if (idx == MENU_EXIT) {
        wm_should_exit = 1;
    } else if (idx == MENU_REBOOT) {
        reboot();
    }
}

// Корректное завершение сессии WM: окна закрываются сами, зависшие программы
// добиваем, и возвращаем консоль.
static void shutdown_session(void) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].header) clients[i].header->closed = 1;
    }

    // Клиенты сами обрабатывают closed (останавливают задания) и вызывают gui_destroy_window
    for (int t = 0; t < 120; t++) {
        int alive = 0;
        for (int i = 0; i < client_count; i++) {
            if (clients[i].header && clients[i].header->active) alive = 1;
        }
        if (!alive) break;
        sleep(16);
    }

    // Убиваем только владельцев окон, которые не вышли за отведённое время.
    // Не трогаем уже завершившиеся pid: ядро при повторном освобождении ресурсов
    // зомби может сломаться.
    for (int i = 0; i < client_count; i++) {
        window_header_t* h = clients[i].header;
        if (h && h->active && h->pid > 0) send_signal(h->pid, 9);
    }

    for (int i = 0; i < client_count; i++) {
        shm_detach(clients[i].shmid);
    }
    client_count = 0;

    // Перенаправления ввода-вывода в пайпы, которые могли остаться от программ,
    // возвращаем на консоль (на нормальных дескрипторах это no-op)
    close(STDIN);
    close(STDOUT);
    close(STDERR);
}

// ====================================================================
// ОБРАБОТКА ВВОДА
// ====================================================================

static void handle_taskbar_click(int x, int y) {
    for (int i = 0; i < client_count; i++) {
        if (!taskbar_button_visible(i)) break;
        window_header_t* h = clients[i].header;
        if (!h || !h->active) continue;
        if (point_in_rect(x, y, taskbar_button_x(i), START_BTN_Y, TASK_BTN_W, START_BTN_H)) {
            raise_window(i);
            return;
        }
    }
}

static void handle_window_click(int idx, int x, int y) {
    idx = raise_window(idx);
    window_header_t* h = clients[idx].header;

    // Кнопка [X]: просим окно закрыться
    if (point_in_rect(x, y,
                      h->x + h->w - CLOSE_BTN_X_OFF, h->y + CLOSE_BTN_Y_OFF,
                      CLOSE_BTN_SIZE, CLOSE_BTN_SIZE)) {
        h->closed = 1;
        return;
    }

    // Захват заголовка — перетаскивание
    if (y < h->y + TITLE_H) {
        drag_shmid = clients[idx].shmid;
        drag_dx = x - h->x;
        drag_dy = y - h->y;
        return;
    }

    // Клик внутри клиентской области
    int cx = x - h->x;
    int cy = y - (h->y + CLIENT_OFFSET_Y);
    if (cx >= 0 && cx < h->w && cy >= 0 && cy < h->h) {
        post_event(h, EVT_CLICK, cx, cy, 1);
    }
}

static void handle_desktop_click(int x, int y) {
    for (int i = 0; i < APP_COUNT; i++) {
        int ix = DESK_ICON_X;
        int iy = 14 + i * DESK_STEP_Y;
        if (point_in_rect(x, y, ix - 6, iy, DESK_ICON_W + 12, DESK_ICON_H + 20)) {
            launch_app(desktop_app_paths[i]);
            return;
        }
    }
}

static void handle_click(int x, int y) {
    // 1) Кнопка [Start]
    if (point_in_rect(x, y, START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H)) {
        start_menu_open = !start_menu_open;
        return;
    }

    // 2) Открытое меню Start
    if (start_menu_open) {
        if (point_in_rect(x, y, MENU_X, MENU_Y, MENU_W, MENU_H)) {
            if (y >= MENU_ITEMS_Y) {
                int item = (y - MENU_ITEMS_Y) / MENU_ITEM_H;
                if (item >= 0 && item < MENU_ITEMS) activate_menu_item(item);
            }
            return;
        }
        // Клик мимо меню закрывает его
        start_menu_open = 0;
    }

    // 3) Панель задач
    if (y >= TASKBAR_Y) {
        handle_taskbar_click(x, y);
        return;
    }

    // 4) Окна (сверху вниз по z-order)
    int idx = window_at(x, y);
    if (idx >= 0) {
        handle_window_click(idx, x, y);
        return;
    }

    // 5) Ярлыки рабочего стола
    handle_desktop_click(x, y);
}

static void process_mouse(void) {
    mouse_state_t ms;
    if (get_mouse(&ms) != 0) return;

    mx += ms.dx;
    my -= ms.dy;
    if (mx < 0) mx = 0;
    if (mx >= SCREEN_W) mx = SCREEN_W - 1;
    if (my < 0) my = 0;
    if (my >= SCREEN_H) my = SCREEN_H - 1;

    int left_down = ms.buttons & 1;
    int clicked = left_down && !(last_btn & 1);
    if (clicked) handle_click(mx, my);

    if (!left_down) {
        drag_shmid = -1;
    }
    if (drag_shmid >= 0) {
        int idx = find_client_by_shmid(drag_shmid);
        if (idx < 0) {
            drag_shmid = -1;
        } else {
            window_header_t* h = clients[idx].header;
            int nx = mx - drag_dx;
            int ny = my - drag_dy;
            if (ny < 2) ny = 2;
            if (ny > TASKBAR_Y - 24) ny = TASKBAR_Y - 24;
            h->x = nx;
            h->y = ny;
        }
    }

    last_btn = ms.buttons;
}

static void handle_key(char key) {
    if (key == KEY_WIN) {
        start_menu_open = !start_menu_open;
        return;
    }
    if (key == 27 && start_menu_open) {
        start_menu_open = 0;
        return;
    }

    if (client_count <= 0) return;
    window_header_t* h = clients[client_count - 1].header;
    if (!h || !h->active || h->closed) return;
    post_event(h, EVT_KEY, (unsigned char)key, 0, (uint8_t)key);
}

static void process_keyboard(void) {
    // Забираем все накопившиеся за кадр нажатия
    char key;
    while ((key = poll_key()) != 0) {
        handle_key(key);
    }
}

// ====================================================================
// КОМПОЗИЦИЯ КАДРА
// ====================================================================

static void compose_frame(void) {
    clear_desktop(C_CYAN);
    draw_desktop_icons();

    for (int i = 0; i < client_count; i++) {
        if (!clients[i].header || !clients[i].header->active) continue;
        draw_window(i);
    }

    draw_taskbar();
    draw_toast();
    draw_start_menu();
    draw_cursor();
    flip_vga();
}

int main(int argc, char* argv[]) {
    if (getuid() != 0) {
        print("wm: Root permissions required (sudo wm)\n");
        exit(1);
    }

    // Блокировка «WM запущен»: сегмент с фиксированным слотом 0
    int32_t lock_id = shm_create(sizeof(window_header_t));
    window_header_t* lock = (lock_id >= 0) ? (window_header_t*)shm_attach(lock_id) : NULL;
    if (!lock || lock_id != WM_LOCK_SHMID) {
        print("wm: Already running or SHM slot 0 busy\n");
        if (lock) shm_detach(lock_id);   // освобождаем сегмент, который создали зря
        exit(1);
    }
    lock->pid = getpid();
    lock->active = 2;

    for (int p = 0; p < 4; p++) {
        vga_planes[p] = (uint8_t*)malloc(TOTAL_BYTES);
        if (vga_planes[p] == 0) {
            print("wm: Failed to allocate VGA pages\n");
            exit(1);
        }
    }

    load_cp437_file();
    userland_set_mode_640x480();

    int frame = 0;
    while (!wm_should_exit) {
        if ((frame++ & 7) == 0) {
            discover_windows();
        }
        cleanup_closed_windows();

        process_mouse();
        process_keyboard();
        compose_frame();

        sleep(16);
    }

    shutdown_session();

    // Освобождаем блокировку WM
    lock->active = 0;
    shm_detach(lock_id);

    // Возврат в текстовый режим + шрифт /sys/cp437.bm
    userland_set_mode_text();
    restore_vga_font_cp437();
    clear_screen(0x07);
    gotoxy(0, 0);
    exit(0);
    return 0;
}
