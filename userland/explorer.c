#include "photon.h"
#include "ulibc.h"
#include "libgui.h"

#define WIN_W       380
#define WIN_H       260
#define MAX_ITEMS   48
#define VISIBLE_ROWS 9
#define LIST_X      8
#define LIST_Y      58
#define LIST_W      340
#define LIST_H      (VISIBLE_ROWS * 20 + 4)

typedef struct {
    char     name[32];
    uint8_t  type;
    uint32_t size;
} file_item_t;

static char current_dir[128] = "/";
static file_item_t items[MAX_ITEMS];
static int item_count = 0;
static int selected_idx = 0;
static int scroll_offset = 0;
static int create_counter = 1;
static char status_msg[48] = "Ready.";

static void load_directory(const char* path) {
    int fd = open(path, READ);
    if (fd < 0) {
        u_strncpy(status_msg, "Cannot open directory!", sizeof(status_msg));
        return;
    }

    u_strncpy(current_dir, path, sizeof(current_dir));
    item_count = 0;
    selected_idx = 0;
    scroll_offset = 0;

    if (u_strcmp(current_dir, "/") != 0) {
        u_strncpy(items[item_count].name, "..", 32);
        items[item_count].type = TYPE_DIR;
        items[item_count].size = 0;
        item_count++;
    }

    dirent_t d;
    uint32_t idx = 0;
    while (item_count < MAX_ITEMS) {
        u_memset(&d, 0, sizeof(dirent_t));
        if (readdir(fd, idx++, &d) != 0) break;
        if (d.name[0] == '\0') break;
        if (u_strcmp(d.name, ".") == 0) continue;

        u_strncpy(items[item_count].name, d.name, 32);
        items[item_count].type = d.type;
        items[item_count].size = d.size;
        item_count++;
    }
    close(fd);

    char num[12];
    u_format_num((uint32_t)item_count, num, sizeof(num));
    u_strncpy(status_msg, "Items: ", sizeof(status_msg));
    u_strncpy(status_msg + u_strlen(status_msg), num, 16);
}

static void go_up(void) {
    if (u_strcmp(current_dir, "/") == 0) return;
    char parent[128];
    u_strncpy(parent, current_dir, sizeof(parent));
    int len = u_strlen(parent);
    while (len > 1 && parent[len - 1] != '/') len--;
    if (len > 1) parent[len - 1] = '\0';
    else { parent[0] = '/'; parent[1] = '\0'; }
    load_directory(parent);
}

static void open_selected(void) {
    if (item_count <= 0 || selected_idx < 0 || selected_idx >= item_count) return;
    file_item_t* it = &items[selected_idx];

    if (it->type == TYPE_DIR) {
        if (u_strcmp(it->name, "..") == 0) {
            go_up();
        } else {
            char next_path[128];
            u_path_join(next_path, current_dir, it->name, sizeof(next_path));
            load_directory(next_path);
        }
        return;
    }

    char full_path[128];
    u_path_join(full_path, current_dir, it->name, sizeof(full_path));

    // Если файл находится в /bin — запускаем его как программу
    if (u_strncmp(current_dir, "/bin", 4) == 0) {
        char* argv[2] = { full_path, NULL };
        int pid = exec(full_path, argv);
        if (pid > 0) u_strncpy(status_msg, "Launched program.", sizeof(status_msg));
        else         u_strncpy(status_msg, "Failed to launch.", sizeof(status_msg));
        return;
    }

    // Иначе пробуем открыть файл в /bin/notepad
    stat_t st;
    if (stat("/bin/notepad", &st) == 0 && st.type == TYPE_FILE) {
        char* argv[3] = { "/bin/notepad", full_path, NULL };
        exec("/bin/notepad", argv);
        u_strncpy(status_msg, "Opened in Notepad.", sizeof(status_msg));
    } else {
        u_strncpy(status_msg, full_path, sizeof(status_msg));
    }
}

static void create_new_item(int is_dir) {
    char name[32];
    char num[12];
    u_format_num((uint32_t)(create_counter++), num, sizeof(num));
    u_strncpy(name, is_dir ? "folder_" : "note_", sizeof(name));
    u_strncpy(name + u_strlen(name), num, 12);
    if (!is_dir) u_strncpy(name + u_strlen(name), ".txt", 8);

    char full_path[128];
    u_path_join(full_path, current_dir, name, sizeof(full_path));

    if (is_dir) {
        if (mkdir(full_path) == 0) u_strncpy(status_msg, "Created folder.", sizeof(status_msg));
        else u_strncpy(status_msg, "mkdir failed!", sizeof(status_msg));
    } else {
        int fd = open(full_path, WRITE | CREATE);
        if (fd >= 0) {
            write(fd, "Hello from Photon OS!\n", 22);
            close(fd);
            u_strncpy(status_msg, "Created file.", sizeof(status_msg));
        } else {
            u_strncpy(status_msg, "touch failed!", sizeof(status_msg));
        }
    }
    load_directory(current_dir);
}

static void delete_selected(void) {
    if (item_count <= 0 || selected_idx < 0 || selected_idx >= item_count) return;
    file_item_t* it = &items[selected_idx];
    if (u_strcmp(it->name, "..") == 0) return;

    char full_path[128];
    u_path_join(full_path, current_dir, it->name, sizeof(full_path));
    if (unlink(full_path) == 0) {
        u_strncpy(status_msg, "Deleted.", sizeof(status_msg));
        load_directory(current_dir);
    } else {
        u_strncpy(status_msg, "Delete failed (non-empty?)", sizeof(status_msg));
    }
}

static void render_explorer(gui_window_t* win) {
    gui_clear(win, C_LIGHTGRAY);

    // 1. Адресная строка и кнопка [Up]
    gui_draw_button(win, 8, 6, 44, 22, "Up");
    gui_draw_panel(win, 56, 6, WIN_W - 64, 22, C_WHITE, 1);
    gui_draw_text(win, 62, 9, current_dir, C_BLACK);

    // 2. Панель инструментов (Toolbar)
    gui_draw_button(win, 8,   32, 64, 22, "Open");
    gui_draw_button(win, 76,  32, 74, 22, "+Folder");
    gui_draw_button(win, 154, 32, 66, 22, "+File");
    gui_draw_button(win, 224, 32, 66, 22, "Delete");
    gui_draw_button(win, 294, 32, 78, 22, "Refresh");

    // 3. Поле списка файлов
    gui_draw_panel(win, LIST_X, LIST_Y, LIST_W, LIST_H, C_WHITE, 1);

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int idx = scroll_offset + row;
        if (idx >= item_count) break;

        int ry = LIST_Y + 2 + row * 20;
        int is_sel = (idx == selected_idx);

        if (is_sel) {
            gui_fill_rect(win, LIST_X + 2, ry, LIST_W - 4, 19, C_BLUE);
        }

        // Иконка элемента (папка или файл)
        if (items[idx].type == TYPE_DIR) {
            gui_fill_rect(win, LIST_X + 6, ry + 3, 14, 12, C_YELLOW);
            gui_draw_rect(win, LIST_X + 6, ry + 3, 14, 12, C_BROWN);
        } else {
            gui_fill_rect(win, LIST_X + 7, ry + 2, 12, 14, C_WHITE);
            gui_draw_rect(win, LIST_X + 7, ry + 2, 12, 14, C_DARKGRAY);
        }

        uint8_t fg = is_sel ? C_WHITE : C_BLACK;
        gui_draw_text(win, LIST_X + 26, ry + 2, items[idx].name, fg);

        if (items[idx].type == TYPE_DIR) {
            gui_draw_text(win, LIST_X + LIST_W - 56, ry + 2, "<DIR>", is_sel ? C_YELLOW : C_DARKGRAY);
        } else {
            char sbuf[16];
            u_format_num(items[idx].size, sbuf, sizeof(sbuf));
            int slen = u_strlen(sbuf);
            gui_draw_text(win, LIST_X + LIST_W - 16 - slen * 8, ry + 2, sbuf, fg);
            gui_draw_char(win, LIST_X + LIST_W - 14, ry + 2, 'B', fg);
        }
    }

    // 4. Кнопки прокрутки справа от списка
    int sb_x = LIST_X + LIST_W + 4;
    gui_draw_button(win, sb_x, LIST_Y, 20, 24, "^");
    gui_draw_panel(win, sb_x, LIST_Y + 26, 20, LIST_H - 52, C_LIGHTGRAY, 1);
    gui_draw_button(win, sb_x, LIST_Y + LIST_H - 24, 20, 24, "v");

    // 5. Строка состояния (Status Bar)
    gui_draw_panel(win, 8, WIN_H - 20, WIN_W - 16, 16, C_LIGHTGRAY, 1);
    gui_draw_text(win, 12, WIN_H - 20, status_msg, C_BLACK);

    gui_sync(win);
}

int main(int argc, char* argv[]) {
    gui_window_t* win = gui_create_window(WIN_W, WIN_H, "Photon Explorer");
    if (!win) exit(1);

    const char* start_path = (argc > 1 && argv[1][0]) ? argv[1] : "/";
    load_directory(start_path);
    render_explorer(win);

    while (1) {
        shm_event_t ev;
        int dirty = 0;

        while (gui_poll_event(win, &ev)) {
            if (ev.type == EVT_CLOSE) {
                gui_destroy_window(win);
                exit(0);
            } else if (ev.type == EVT_CLICK) {
                int mx = ev.x;
                int my = ev.y;

                // Кнопка [Up]
                if (mx >= 8 && mx < 52 && my >= 6 && my < 28) {
                    go_up();
                    dirty = 1;
                }
                // Панель инструментов
                else if (my >= 32 && my < 54) {
                    if (mx >= 8 && mx < 72)        open_selected();
                    else if (mx >= 76 && mx < 150)  create_new_item(1);
                    else if (mx >= 154 && mx < 220) create_new_item(0);
                    else if (mx >= 224 && mx < 290) delete_selected();
                    else if (mx >= 294 && mx < 372) load_directory(current_dir);
                    dirty = 1;
                }
                // Клик по списку файлов
                else if (mx >= LIST_X && mx < LIST_X + LIST_W &&
                         my >= LIST_Y && my < LIST_Y + LIST_H) {
                    int row = (my - (LIST_Y + 2)) / 20;
                    int clicked_idx = scroll_offset + row;
                    if (clicked_idx >= 0 && clicked_idx < item_count) {
                        if (selected_idx == clicked_idx) {
                            open_selected();
                        } else {
                            selected_idx = clicked_idx;
                        }
                        dirty = 1;
                    }
                }
                // Кнопки прокрутки [^] и [v]
                else if (mx >= LIST_X + LIST_W + 4 && mx < LIST_X + LIST_W + 24) {
                    if (my >= LIST_Y && my < LIST_Y + 24) {
                        if (scroll_offset > 0) scroll_offset--;
                        dirty = 1;
                    } else if (my >= LIST_Y + LIST_H - 24 && my < LIST_Y + LIST_H) {
                        if (scroll_offset + VISIBLE_ROWS < item_count) scroll_offset++;
                        dirty = 1;
                    }
                }
            } else if (ev.type == EVT_KEY) {
                char k = (char)ev.btn;
                if (k == KEY_UP || k == 'w') {
                    if (selected_idx > 0) {
                        selected_idx--;
                        if (selected_idx < scroll_offset) scroll_offset = selected_idx;
                        dirty = 1;
                    }
                } else if (k == KEY_DOWN || k == 's') {
                    if (selected_idx < item_count - 1) {
                        selected_idx++;
                        if (selected_idx >= scroll_offset + VISIBLE_ROWS) scroll_offset++;
                        dirty = 1;
                    }
                } else if (k == '\n' || k == '\r') {
                    open_selected();
                    dirty = 1;
                } else if (k == '\b') {
                    go_up();
                    dirty = 1;
                }
            }
        }

        if (dirty) {
            render_explorer(win);
        }
        sleep(20);
    }
}
