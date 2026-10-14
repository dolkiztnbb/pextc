#ifndef LIBGUI_H
#define LIBGUI_H

#include "gui_shm.h"
#include "photon.h"
#include "ulibc.h"

typedef struct {
    window_header_t* header;
    uint8_t*         canvas; // Прямой указатель на пиксели
    int32_t          shmid;
} gui_window_t;

// Буфер шрифта 8x16 (/sys/cp437.bm, 4096 байт = 256 символов * 16 байт)
static uint8_t font_cp437[4096];
static int font_loaded = 0;

static inline void gui_load_font(void) {
    if (font_loaded) return;

    int fd = open("/sys/cp437.bm", READ | BINARY);
    if (fd < 0) return;

    int total_read = 0;
    while (total_read < 4096) {
        int n = read(fd, (char*)(font_cp437 + total_read), 4096 - total_read);
        if (n <= 0) break;
        total_read += n;
    }
    close(fd);

    if (total_read == 4096) {
        font_loaded = 1;
    }
}

static inline gui_window_t* gui_create_window(int w, int h, const char* title) {
    // Проверяем в ОЗУ (через SHM-слот 0), запущен ли сейчас оконный менеджер wm.
    // Файл на диске не используется: после аварийного завершения он остался бы в ФС.
    window_header_t* wm_lock = (window_header_t*)shm_attach(0);
    if (!wm_lock || wm_lock->active != 2) {
        if (wm_lock) shm_detach(0);
        print("gui_create_window: WM is not running\n");
        return NULL;
    }
    shm_detach(0);

    gui_load_font();

    uint32_t total_size = sizeof(window_header_t) + (uint32_t)(w * h);

    int32_t shmid = shm_create(total_size);
    if (shmid < 0) return NULL;

    window_header_t* hdr = (window_header_t*)shm_attach(shmid);
    if (!hdr) {
        shm_detach(shmid);
        return NULL;
    }

    hdr->pid = getpid();
    hdr->shmid = shmid;
    hdr->x = 70 + ((hdr->pid * 29) % 160);
    hdr->y = 36 + ((hdr->pid * 19) % 90);
    hdr->w = w;
    hdr->h = h;
    hdr->is_dirty = 1;
    hdr->closed = 0;
    hdr->evt_head = 0;
    hdr->evt_tail = 0;
    u_strncpy(hdr->title, title, 31);

    gui_window_t* win = (gui_window_t*)malloc(sizeof(gui_window_t));
    if (!win) {
        shm_detach(shmid);
        return NULL;
    }
    win->header = hdr;
    win->shmid = shmid;
    win->canvas = (uint8_t*)(hdr + 1);

    // Барьер памяти: выставляем active = 1 самым последним, когда заголовок полностью готов.
    // Оконный менеджер находит сегмент перебором SHM-слотов (см. discover_windows в wm.c).
    asm volatile("" ::: "memory");
    hdr->active = 1;

    return win;
}

static inline void gui_destroy_window(gui_window_t* win) {
    if (!win) return;
    if (win->header) {
        win->header->active = 0;
    }
    shm_detach(win->shmid);
    free(win);
}

static inline void gui_set_pixel(gui_window_t* win, int x, int y, uint8_t color) {
    if (x < 0 || x >= win->header->w || y < 0 || y >= win->header->h) return;
    win->canvas[y * win->header->w + x] = color;
}

static inline void gui_clear(gui_window_t* win, uint8_t color) {
    u_memset(win->canvas, color, win->header->w * win->header->h);
}

static inline void gui_fill_rect(gui_window_t* win, int x, int y, int w, int h, uint8_t color) {
    for (int cy = y; cy < y + h; cy++)
        for (int cx = x; cx < x + w; cx++)
            gui_set_pixel(win, cx, cy, color);
}

static inline void gui_draw_rect(gui_window_t* win, int x, int y, int w, int h, uint8_t color) {
    for (int i = 0; i < w; i++) {
        gui_set_pixel(win, x + i, y, color);
        gui_set_pixel(win, x + i, y + h - 1, color);
    }
    for (int i = 0; i < h; i++) {
        gui_set_pixel(win, x, y + i, color);
        gui_set_pixel(win, x + w - 1, y + i, color);
    }
}

// 3D-панель в стиле Win95: sunken = 0 (выпуклая), sunken = 1 (вдавленная)
static inline void gui_draw_panel(gui_window_t* win, int x, int y, int w, int h, uint8_t bg, int sunken) {
    uint8_t tl = sunken ? C_DARKGRAY : C_WHITE;
    uint8_t br = sunken ? C_WHITE    : C_DARKGRAY;
    gui_fill_rect(win, x, y, w, h, bg);
    for (int i = 0; i < w; i++) gui_set_pixel(win, x + i, y, tl);
    for (int i = 0; i < h; i++) gui_set_pixel(win, x, y + i, tl);
    for (int i = 0; i < w; i++) gui_set_pixel(win, x + i, y + h - 1, br);
    for (int i = 0; i < h; i++) gui_set_pixel(win, x + w - 1, y + i, br);
}

static inline void gui_draw_char(gui_window_t* win, int x, int y, unsigned char uc, uint8_t color) {
    if (!font_loaded) gui_load_font();
    if (!font_loaded) return;

    const uint8_t* glyph = &font_cp437[uc * 16];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                gui_set_pixel(win, x + col, y + row, color);
            }
        }
    }
}

static inline void gui_draw_text(gui_window_t* win, int x, int y, const char* text, uint8_t color) {
    if (!text) return;
    while (*text) {
        gui_draw_char(win, x, y, (unsigned char)*text++, color);
        x += 8;
    }
}

static inline void gui_draw_button(gui_window_t* win, int x, int y, int w, int h, const char* text) {
    gui_draw_panel(win, x, y, w, h, C_LIGHTGRAY, 0);
    int len = u_strlen(text);
    int text_x = x + (w - len * 8) / 2;
    if (text_x < x + 2) text_x = x + 2;
    int text_y = y + (h / 2) - 8;
    gui_draw_text(win, text_x, text_y, text, C_BLACK);
}

static inline void gui_sync(gui_window_t* win) {
    win->header->is_dirty = 1;
}

static inline int gui_poll_event(gui_window_t* win, shm_event_t* ev) {
    if (!win || !win->header) return 0;
    if (win->header->closed) {
        ev->type = EVT_CLOSE;
        return 1;
    }
    if (win->header->evt_head == win->header->evt_tail) return 0;

    *ev = win->header->events[win->header->evt_tail];
    win->header->evt_tail = (win->header->evt_tail + 1) % MAX_WIN_EVENTS;
    return 1;
}

#endif
