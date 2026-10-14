#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

#include "ulibc.h"

#define MAX_ENTRIES 32
#define PANEL_WIDTH 39
#define PANEL_HEIGHT 20

typedef struct {
    char current_path[128];
    dirent_t entries[MAX_ENTRIES];
    uint32_t count;
    int selected;
    int scroll;
} Panel;

static Panel* left_panel = NULL;
static Panel* right_panel = NULL;
static int active_panel_idx = 0;

static void load_panel(Panel* p, const char* path) {
    if (!p || !path) return;
    u_memset(p, 0, sizeof(Panel));
    u_strncpy(p->current_path, path, sizeof(p->current_path));
    p->count = 0;
    p->selected = 0;
    p->scroll = 0;

    if (u_strcmp(path, "/") != 0) {
        u_strncpy(p->entries[p->count].name, "..", 32);
        p->entries[p->count].type = TYPE_DIR;
        p->entries[p->count].size = 0;
        p->count++;
    }

    int fd = open(path, READ);
    if (fd >= 0) {
        dirent_t d;
        uint32_t idx = 0;
        while (p->count < MAX_ENTRIES) {
            u_memset(&d, 0, sizeof(dirent_t));

            if (readdir(fd, idx++, &d) != 0) break;
            if (d.name[0] == '\0') break;
            if (u_strcmp(d.name, ".") == 0) continue;

            u_strncpy(p->entries[p->count].name, d.name, 32);
            p->entries[p->count].type = d.type;
            p->entries[p->count].size = d.size;
            p->count++;
        }
        close(fd);
    }
}

static void draw_box(int start_x, int start_y, int w, int h, const char* title, int is_active) {
    char tl = is_active ? '\xC9' : '\xDA'; 
    char tr = is_active ? '\xBB' : '\xBF'; 
    char bl = is_active ? '\xC8' : '\xC0'; 
    char br = is_active ? '\xBC' : '\xD9'; 
    char hl = is_active ? '\xCD' : '\xC4'; 
    char vl = is_active ? '\xBA' : '\xB3'; 

    const char* frame_color = is_active ? "\033[97;44m" : "\033[37;44m";
    const char* title_color = is_active ? "\033[93;44m" : "\033[37;44m";

    gotoxy(start_x, start_y);
    print(frame_color); putchar(tl);
    for (int i = 0; i < w - 2; i++) putchar(hl);
    putchar(tr);

    gotoxy(start_x + 2, start_y);
    print(title_color); putchar(' '); print(title); putchar(' ');

    for (int y = 1; y < h - 1; y++) {
        gotoxy(start_x, start_y + y);
        print(frame_color); putchar(vl);
        for (int x = 0; x < w - 2; x++) putchar(' ');
        putchar(vl);
    }

    gotoxy(start_x, start_y + h - 1);
    print(frame_color); putchar(bl);
    for (int i = 0; i < w - 2; i++) putchar(hl);
    putchar(br);
    print("\033[0m");
}

static void render_panel(Panel* p, int start_x, int is_active) {
    if (!p) return;
    draw_box(start_x, 1, PANEL_WIDTH, PANEL_HEIGHT, p->current_path, is_active);

    for (int i = 0; i < PANEL_HEIGHT - 3; i++) {
        int idx = p->scroll + i;
        gotoxy(start_x + 1, 2 + i);

        if (idx < (int)p->count) {
            int sel = (is_active && idx == p->selected);
            if (sel) print("\033[30;46m"); 
            else if (p->entries[idx].type == TYPE_DIR) print("\033[97;44m");
            else print("\033[36;44m");

            char line_buf[40];
            u_memset(line_buf, ' ', 37);
            line_buf[37] = '\0';

            line_buf[0] = (p->entries[idx].type == TYPE_DIR) ? '/' : ' ';

            int nlen = u_strlen(p->entries[idx].name);
            if (nlen > 20) nlen = 20;
            for (int k = 0; k < nlen; k++) line_buf[1 + k] = p->entries[idx].name[k];

            if (p->entries[idx].type == TYPE_DIR) {
                line_buf[30] = '<'; line_buf[31] = 'D'; line_buf[32] = 'I';
                line_buf[33] = 'R'; line_buf[34] = '>';
            } else {
                char sbuf[12];
                u_format_num(p->entries[idx].size, sbuf, sizeof(sbuf));
                int slen = u_strlen(sbuf);
                if (slen > 8) slen = 8;
                for (int k = 0; k < slen; k++) line_buf[35 - slen + k] = sbuf[k];
                line_buf[35] = 'B';
            }

            print(line_buf); print("\033[0m");
        } else {
            print("\033[37;44m");
            for (int k = 0; k < PANEL_WIDTH - 2; k++) print(" ");
            print("\033[0m");
        }
    }
}

static void draw_ui(void) {
    gotoxy(0, 0);
    print("\033[97;40m Photon Commander v1.0 | [Tab] Switch | [Enter] Open | [D] Delete | [Q] Quit\033[0m");

    render_panel(left_panel, 0, active_panel_idx == 0);
    render_panel(right_panel, 40, active_panel_idx == 1);

    gotoxy(0, 22);
    Panel* cur = (active_panel_idx == 0) ? left_panel : right_panel;
    print("\033[90;47m Selected: ");
    if (cur && cur->count > 0) print(cur->entries[cur->selected].name);
    else print("(empty)");
    for (int i = 0; i < 40; i++) print(" ");
    print("\033[0m");
}

static void view_file(const char* fullpath) {
    int fd = open(fullpath, READ);
    if (fd < 0) return;

    clear_screen(0x07);
    gotoxy(0, 0);
    print("\033[97;45m === File View: "); print(fullpath); print(" ===\033[0m\n\n");

    char buf[256];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0'; print(buf);
    }
    close(fd);
    getchar_blocking();
}

static void handle_action(void) {
    Panel* p = (active_panel_idx == 0) ? left_panel : right_panel;
    if (!p || p->count == 0) return;

    dirent_t* cur = &p->entries[p->selected];
    char new_path[128];

    if (cur->type == TYPE_DIR) {
        if (u_strcmp(cur->name, "..") == 0) {
            u_strncpy(new_path, p->current_path, sizeof(new_path));
            int len = u_strlen(new_path);
            while (len > 1 && new_path[len - 1] != '/') len--;
            if (len > 1) new_path[len - 1] = '\0';
            else { new_path[0] = '/'; new_path[1] = '\0'; }
        } else {
            u_path_join(new_path, p->current_path, cur->name, sizeof(new_path));
        }
        load_panel(p, new_path);
    } else {
        u_path_join(new_path, p->current_path, cur->name, sizeof(new_path));
        view_file(new_path);
    }
}

int main(int argc, char* argv[]) {
    set_io_flags(0); clear_screen(0x07);

    left_panel = (Panel*)malloc(sizeof(Panel));
    right_panel = (Panel*)malloc(sizeof(Panel));

    if (!left_panel || !right_panel) { print("Failed to malloc!\n"); exit(1); }

    const char* left_path = (argc > 1 && argv[1][0]) ? argv[1] : "/";
    const char* right_path = (argc > 2 && argv[2][0]) ? argv[2] : "/home";

    load_panel(left_panel, left_path);
    load_panel(right_panel, right_path);

    while (1) {
        draw_ui();
        char key = getchar_blocking();
        Panel* cur = (active_panel_idx == 0) ? left_panel : right_panel;

        if (key == '\t') active_panel_idx = 1 - active_panel_idx;
        else if (key == 'w' || key == '8') {
            if (cur->selected > 0) { cur->selected--; if (cur->selected < cur->scroll) cur->scroll = cur->selected; }
        } else if (key == 's' || key == '2') {
            if (cur->selected < (int)cur->count - 1) {
                cur->selected++;
                if (cur->selected >= cur->scroll + (PANEL_HEIGHT - 3)) cur->scroll++;
            }
        } else if (key == '\n' || key == '\r') {
            handle_action();
        } else if (key == 'q' || key == 'Q') break;
    }

    clear_screen(0x07); gotoxy(0, 0);
    print("Photon Commander exited.\n");
    exit(0);
}