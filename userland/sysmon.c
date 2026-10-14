#include "photon.h"
#include "ulibc.h"
#include "libgui.h"

#define WIN_W 260
#define WIN_H 170
#define GRAPH_W 240
#define GRAPH_H 64

static uint8_t history[GRAPH_W];

int main(int argc, char* argv[]) {
    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "System Monitor");
    if (!win) exit(1);

    u_memset(history, 0, sizeof(history));
    uint32_t last_ticks = get_ticks();

    while (1) {
        shm_event_t ev;
        while (gui_poll_event(win, &ev)) {
            if (ev.type == EVT_CLOSE) {
                gui_destroy_window(win);
                exit(0);
            }
        }

        uint32_t now = get_ticks();
        uint32_t delta = now - last_ticks;
        last_ticks = now;

        for (int i = 0; i < GRAPH_W - 1; i++) {
            history[i] = history[i + 1];
        }
        uint32_t bar = (delta * 2 + (now / 100) % 19) % (GRAPH_H - 8) + 6;
        history[GRAPH_W - 1] = (uint8_t)bar;

        gui_clear(win, C_LIGHTGRAY);

        // Информационные строки
        uint8_t hh = 0, mm = 0, ss = 0, dd = 0, mo = 0;
        uint16_t yy = 0;
        get_time(&hh, &mm, &ss);
        get_date(&dd, &mo, &yy);

        char line1[40];
        char line2[40];
        char line3[40];
        snprintf(line1, sizeof(line1), "Uptime: %u.%u sec", now / 1000, (now / 100) % 10);
        snprintf(line2, sizeof(line2), "Clock:  %02u:%02u:%02u (%02u.%02u.%04u)", hh, mm, ss, dd, mo, yy);
        snprintf(line3, sizeof(line3), "PID: %d | UID: %d", getpid(), getuid());

        gui_draw_text(win, 10, 8,  line1, C_BLACK);
        gui_draw_text(win, 10, 26, line2, C_BLUE);
        gui_draw_text(win, 10, 44, line3, C_DARKGRAY);

        // График активности
        int gx = 10, gy = 66;
        gui_draw_panel(win, gx - 2, gy - 2, GRAPH_W + 4, GRAPH_H + 4, C_BLACK, 1);

        for (int x = 0; x < GRAPH_W; x += 2) {
            int h = history[x];
            uint8_t col = (h > 44) ? C_YELLOW : C_LIGHTGREEN;
            gui_fill_rect(win, gx + x, gy + GRAPH_H - h, 2, h, col);
        }

        // Сетка поверх графика
        for (int y = gy + 16; y < gy + GRAPH_H; y += 16) {
            for (int x = gx; x < gx + GRAPH_W; x += 4) {
                gui_set_pixel(win, x, y, C_GREEN);
            }
        }

        gui_sync(win);
        sleep(120);
    }
}
