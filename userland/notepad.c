#include "photon.h"
#include "ulibc.h"
#include "libgui.h"

#define WIN_W     340
#define WIN_H     240
#define TEXT_COLS 40
#define TEXT_ROWS 11
#define MAX_TEXT  512

static char file_path[64] = "/home/greet.txt";
static char text_buf[MAX_TEXT];
static int  text_len = 0;
static char status[40] = "Opened.";

static void load_file(const char* path) {
    u_strncpy(file_path, path, sizeof(file_path));
    u_memset(text_buf, 0, sizeof(text_buf));
    text_len = 0;

    int fd = open(file_path, READ);
    if (fd >= 0) {
        int n = read(fd, text_buf, MAX_TEXT - 1);
        if (n > 0) text_len = n;
        text_buf[text_len] = '\0';
        close(fd);
        u_strncpy(status, "Loaded file.", sizeof(status));
    } else {
        u_strncpy(status, "New file.", sizeof(status));
    }
}

static void save_file(void) {
    int fd = open(file_path, WRITE | CREATE | TRUNCATE);
    if (fd >= 0) {
        if (text_len > 0) write(fd, text_buf, (uint32_t)text_len);
        close(fd);
        u_strncpy(status, "Saved successfully!", sizeof(status));
    } else {
        u_strncpy(status, "Save failed!", sizeof(status));
    }
}

static void render_notepad(gui_window_t* win) {
    gui_clear(win, C_LIGHTGRAY);

    // Панель кнопок
    gui_draw_button(win, 6, 4, 56, 20, "Save");
    gui_draw_button(win, 66, 4, 64, 20, "Reload");
    gui_draw_button(win, 134, 4, 56, 20, "Clear");
    gui_draw_text(win, 198, 6, file_path, C_BLUE);

    // Текстовое поле
    int tx = 6, ty = 28, tw = WIN_W - 12, th = WIN_H - 50;
    gui_draw_panel(win, tx, ty, tw, th, C_WHITE, 1);

    int r = 0, c = 0;
    for (int i = 0; i < text_len && r < TEXT_ROWS; i++) {
        char ch = text_buf[i];
        if (ch == '\r') continue;
        if (ch == '\n') {
            r++;
            c = 0;
            continue;
        }
        if (c >= TEXT_COLS) {
            r++;
            c = 0;
            if (r >= TEXT_ROWS) break;
        }
        gui_draw_char(win, tx + 4 + c * 8, ty + 4 + r * 16, (unsigned char)ch, C_BLACK);
        c++;
    }

    // Курсор
    if (r < TEXT_ROWS) {
        gui_fill_rect(win, tx + 4 + c * 8, ty + 4 + r * 16 + 14, 8, 2, C_BLUE);
    }

    // Статус-бар
    gui_draw_panel(win, 6, WIN_H - 18, WIN_W - 12, 15, C_LIGHTGRAY, 1);
    gui_draw_text(win, 10, WIN_H - 18, status, C_BLACK);

    gui_sync(win);
}

int main(int argc, char* argv[]) {
    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "Notepad");
    if (!win) exit(1);

    if (argc > 1 && argv[1][0]) {
        load_file(argv[1]);
    } else {
        load_file("/home/greet.txt");
    }
    render_notepad(win);

    while (1) {
        shm_event_t ev;
        int dirty = 0;

        while (gui_poll_event(win, &ev)) {
            if (ev.type == EVT_CLOSE) {
                gui_destroy_window(win);
                exit(0);
            } else if (ev.type == EVT_CLICK) {
                if (ev.y >= 4 && ev.y < 24) {
                    if (ev.x >= 6 && ev.x < 62) {
                        save_file();
                        dirty = 1;
                    } else if (ev.x >= 66 && ev.x < 130) {
                        load_file(file_path);
                        dirty = 1;
                    } else if (ev.x >= 134 && ev.x < 190) {
                        text_len = 0;
                        text_buf[0] = '\0';
                        u_strncpy(status, "Cleared.", sizeof(status));
                        dirty = 1;
                    }
                }
            } else if (ev.type == EVT_KEY) {
                char k = (char)ev.btn;
                if (k == '\b' || k == 127) {
                    if (text_len > 0) {
                        text_buf[--text_len] = '\0';
                        dirty = 1;
                    }
                } else if (k == '\r' || k == '\n') {
                    if (text_len < MAX_TEXT - 1) {
                        text_buf[text_len++] = '\n';
                        text_buf[text_len] = '\0';
                        dirty = 1;
                    }
                } else if ((unsigned char)k >= 32 && (unsigned char)k <= 126) {
                    if (text_len < MAX_TEXT - 1) {
                        text_buf[text_len++] = k;
                        text_buf[text_len] = '\0';
                        dirty = 1;
                    }
                }
            }
        }

        if (dirty) render_notepad(win);
        sleep(20);
    }
}
