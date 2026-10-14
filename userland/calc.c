#include "photon.h"
#include "ulibc.h"
#include "libgui.h"

#define WIN_W 188
#define WIN_H 210

static int32_t acc = 0;
static int32_t cur = 0;
static char    op = 0;
static int     entering = 1;

static const char* btn_labels[4][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "C", "0", "=", "+" }
};

static void apply_op(void) {
    if (op == '+') acc += cur;
    else if (op == '-') acc -= cur;
    else if (op == '*') acc *= cur;
    else if (op == '/') {
        if (cur != 0) acc /= cur;
        else acc = 0;
    } else {
        acc = cur;
    }
}

static void press_key(char k) {
    if (k >= '0' && k <= '9') {
        if (!entering) {
            cur = 0;
            entering = 1;
        }
        cur = cur * 10 + (k - '0');
    } else if (k == 'C' || k == 'c') {
        acc = 0;
        cur = 0;
        op = 0;
        entering = 1;
    } else if (k == '=') {
        apply_op();
        cur = acc;
        op = 0;
        entering = 0;
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (entering) {
            apply_op();
            cur = acc;
            entering = 0;
        }
        op = k;
    }
}

static void render_calc(gui_window_t* win) {
    gui_clear(win, C_LIGHTGRAY);

    // Табло калькулятора
    gui_draw_panel(win, 10, 10, WIN_W - 20, 30, C_BLACK, 1);

    char disp[24];
    int32_t val = entering ? cur : acc;
    snprintf(disp, sizeof(disp), "%d", val);
    int len = u_strlen(disp);
    gui_draw_text(win, WIN_W - 18 - len * 8, 17, disp, C_LIGHTGREEN);

    if (op) {
        gui_draw_char(win, 16, 17, (unsigned char)op, C_YELLOW);
    }

    // Сетка кнопок 4x4
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = 10 + c * 43;
            int by = 48 + r * 38;
            gui_draw_button(win, bx, by, 39, 34, btn_labels[r][c]);
        }
    }

    gui_sync(win);
}

int main(int argc, char* argv[]) {
    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "Calculator");
    if (!win) exit(1);

    render_calc(win);

    while (1) {
        shm_event_t ev;
        int dirty = 0;

        while (gui_poll_event(win, &ev)) {
            if (ev.type == EVT_CLOSE) {
                gui_destroy_window(win);
                exit(0);
            } else if (ev.type == EVT_CLICK) {
                for (int r = 0; r < 4; r++) {
                    for (int c = 0; c < 4; c++) {
                        int bx = 10 + c * 43;
                        int by = 48 + r * 38;
                        if (ev.x >= bx && ev.x < bx + 39 && ev.y >= by && ev.y < by + 34) {
                            press_key(btn_labels[r][c][0]);
                            dirty = 1;
                        }
                    }
                }
            } else if (ev.type == EVT_KEY) {
                char k = (char)ev.btn;
                if (k == '\r' || k == '\n') k = '=';
                press_key(k);
                dirty = 1;
            }
        }

        if (dirty) render_calc(win);
        sleep(20);
    }
}
