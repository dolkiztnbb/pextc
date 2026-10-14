#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

#include "ulibc.h"

// Однобайтовые коды стрелок
#ifndef KEY_UP
#define KEY_UP    ((char)0x80)
#define KEY_DOWN  ((char)0x81)
#define KEY_LEFT  ((char)0x82)
#define KEY_RIGHT ((char)0x83)
#endif

#define MAX_UNDO 8

#define CTRL_Q 17
#define CTRL_S 19
#define CTRL_Z 26
#define BACKSPACE 8
#define ESC 27

// ====================================================================
// СТРУКТУРЫ ДАННЫХ
// ====================================================================

typedef struct {
    char* text;
    int length;
    int capacity;
} Line;

typedef struct {
    Line* lines;
    int num_lines;
    int capacity;
    int cx, cy;
    int sx, sy;
    int is_modified;
    char filepath[128];
} Editor;

// Снимок хранится единым плоским буфером (без фрагментации кучи)
typedef struct {
    char* text;
    int cx, cy;
} UndoState;

#define MAX_DIR_ENTRIES 128
typedef struct {
    char current_path[128];
    dirent_t entries[MAX_DIR_ENTRIES];
    uint32_t count;
    int selected;
    int scroll;
} Picker;

Editor ed;
UndoState undo_stack[MAX_UNDO];
int undo_head = 0;
int undo_count = 0;
Picker picker;

// ====================================================================
// ДИНАМИЧЕСКАЯ ПАМЯТЬ СТРОК
// ====================================================================

static void free_line(Line* l) {
    if (l->text) free(l->text);
    l->text = NULL;
    l->length = 0;
    l->capacity = 0;
}

static void ensure_line_capacity(Line* l, int min_cap) {
    if (l->capacity >= min_cap) return;
    int new_cap = l->capacity == 0 ? 32 : l->capacity * 2;
    if (new_cap < min_cap) new_cap = min_cap;

    char* new_text = (char*)malloc(new_cap);
    if (!new_text) return;

    if (l->text) {
        u_memcpy(new_text, l->text, l->length + 1);
        free(l->text);
    } else {
        new_text[0] = '\0';
    }
    
    l->text = new_text;
    l->capacity = new_cap;
}

static void ensure_lines_capacity(Editor* e, int min_lines) {
    if (e->capacity >= min_lines) return;
    int new_cap = e->capacity == 0 ? 64 : e->capacity * 2;
    if (new_cap < min_lines) new_cap = min_lines;

    Line* new_lines = (Line*)malloc(new_cap * sizeof(Line));
    if (!new_lines) return;

    if (e->lines) {
        u_memcpy(new_lines, e->lines, e->num_lines * sizeof(Line));
        free(e->lines);
    }
    
    for (int i = e->num_lines; i < new_cap; i++) {
        new_lines[i].text = NULL;
        new_lines[i].length = 0;
        new_lines[i].capacity = 0;
    }
    
    e->lines = new_lines;
    e->capacity = new_cap;
}

// ====================================================================
// СИСТЕМА UNDO (СТАБИЛЬНАЯ СЕРИАЛИЗАЦИЯ)
// ====================================================================

static void free_undo_state(UndoState* u) {
    if (u->text) {
        free(u->text);
        u->text = NULL;
    }
    u->cx = 0;
    u->cy = 0;
}

static void save_undo(void) {
    uint32_t total_len = 0;
    for (int i = 0; i < ed.num_lines; i++) {
        total_len += ed.lines[i].length + 1;
    }

    char* snap = (char*)malloc(total_len + 1);
    if (!snap) return;

    uint32_t pos = 0;
    for (int i = 0; i < ed.num_lines; i++) {
        if (ed.lines[i].length > 0 && ed.lines[i].text) {
            u_memcpy(snap + pos, ed.lines[i].text, ed.lines[i].length);
            pos += ed.lines[i].length;
        }
        if (i < ed.num_lines - 1) {
            snap[pos++] = '\n';
        }
    }
    snap[pos] = '\0';

    UndoState* u = &undo_stack[undo_head];
    free_undo_state(u);

    u->text = snap;
    u->cx = ed.cx;
    u->cy = ed.cy;

    undo_head = (undo_head + 1) % MAX_UNDO;
    if (undo_count < MAX_UNDO) undo_count++;
}

static void pop_undo(void) {
    if (undo_count == 0) return;

    undo_head = (undo_head - 1 + MAX_UNDO) % MAX_UNDO;
    UndoState* u = &undo_stack[undo_head];
    if (!u->text) {
        undo_count--;
        return;
    }

    // Очищаем текущие строки
    for (int i = 0; i < ed.num_lines; i++) free_line(&ed.lines[i]);
    if (ed.lines) free(ed.lines);

    ed.lines = NULL;
    ed.capacity = 0;
    ed.num_lines = 1;
    ensure_lines_capacity(&ed, 1);
    ensure_line_capacity(&ed.lines[0], 32);
    ed.lines[0].length = 0;
    ed.lines[0].text[0] = '\0';

    // Восстанавливаем из снимка
    char* p = u->text;
    while (*p) {
        if (*p == '\n') {
            ed.num_lines++;
            ensure_lines_capacity(&ed, ed.num_lines);
            ensure_line_capacity(&ed.lines[ed.num_lines - 1], 32);
            ed.lines[ed.num_lines - 1].length = 0;
            ed.lines[ed.num_lines - 1].text[0] = '\0';
        } else if (*p != '\r') {
            Line* cur = &ed.lines[ed.num_lines - 1];
            ensure_line_capacity(cur, cur->length + 2);
            cur->text[cur->length++] = *p;
            cur->text[cur->length] = '\0';
        }
        p++;
    }

    ed.cx = u->cx;
    ed.cy = u->cy;
    if (ed.cy >= ed.num_lines) ed.cy = ed.num_lines - 1;
    if (ed.cy < 0) ed.cy = 0;
    if (ed.cx > ed.lines[ed.cy].length) ed.cx = ed.lines[ed.cy].length;
    if (ed.cx < 0) ed.cx = 0;

    free_undo_state(u);
    undo_count--;
    ed.is_modified = 1;
}

// ====================================================================
// ФАЙЛОВЫЕ ОПЕРАЦИИ
// ====================================================================

static void load_file(const char* path) {
    for (int i = 0; i < ed.num_lines; i++) free_line(&ed.lines[i]);
    if (ed.lines) free(ed.lines);
    for (int i = 0; i < MAX_UNDO; i++) free_undo_state(&undo_stack[i]);

    ed.lines = NULL;
    ed.capacity = 0;
    ed.num_lines = 1;
    ensure_lines_capacity(&ed, 1);
    ensure_line_capacity(&ed.lines[0], 32);
    ed.lines[0].length = 0;
    ed.lines[0].text[0] = '\0';

    u_strncpy(ed.filepath, path, sizeof(ed.filepath));
    ed.cx = 0; ed.cy = 0;
    ed.sx = 0; ed.sy = 0;
    ed.is_modified = 0;
    undo_count = 0; undo_head = 0;

    int fd = open(path, READ);
    if (fd < 0) return;

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\r') continue;
            if (buf[i] == '\n') {
                ed.num_lines++;
                ensure_lines_capacity(&ed, ed.num_lines);
                ensure_line_capacity(&ed.lines[ed.num_lines - 1], 32);
                ed.lines[ed.num_lines - 1].length = 0;
                ed.lines[ed.num_lines - 1].text[0] = '\0';
            } else {
                Line* cur = &ed.lines[ed.num_lines - 1];
                ensure_line_capacity(cur, cur->length + 2);
                cur->text[cur->length++] = buf[i];
                cur->text[cur->length] = '\0';
            }
        }
    }
    close(fd);
}

static void save_file(void) {
    int fd = open(ed.filepath, WRITE | TRUNCATE | CREATE);
    if (fd < 0) return;

    for (int i = 0; i < ed.num_lines; i++) {
        if (ed.lines[i].length > 0 && ed.lines[i].text) {
            write(fd, ed.lines[i].text, ed.lines[i].length);
        }
        char nl = '\n';
        write(fd, &nl, 1);
    }
    close(fd);
    ed.is_modified = 0;
}

// ====================================================================
// ЛОГИКА РЕДАКТОРА
// ====================================================================

static void draw_editor(void) {
    int cols = 80, rows = 25;
    get_screen_size(&cols, &rows);

    clear_screen(0x07);

    gotoxy(0, 0);
    print("\033[97;40mFile: "); 
    print(ed.filepath);
    if (ed.is_modified) print(" [*]");
    for (int i = 0; i < cols - 30 - u_strlen(ed.filepath) - ed.is_modified*4; i++) print(" ");
    print("\033[0m");

    for (int i = 0; i < rows - 2; i++) {
        int l = ed.sy + i;
        gotoxy(0, i + 1);
        if (l < ed.num_lines) {
            int len = ed.lines[l].length;
            if (len > ed.sx && ed.lines[l].text) {
                int print_len = len - ed.sx;
                if (print_len > cols) print_len = cols;
                char buf[256]; 
                u_memcpy(buf, &ed.lines[l].text[ed.sx], print_len);
                buf[print_len] = '\0';
                print(buf);
            }
        } else {
            print("\033[90m~\033[0m");
        }
    }

    gotoxy(0, rows - 1);
    print("\033[30;47m"); 
    for(int i = 0; i < 80; i++) print(" "); 
    gotoxy(0, rows - 1);
    print("\033[30;47m ^S Save | ^Q Close File | ^Z Undo | Arrows Navigate \033[0m");

    gotoxy(ed.cx - ed.sx, ed.cy - ed.sy + 1);
}

static void editor_move_cursor(int dx, int dy) {
    ed.cy += dy;
    if (ed.cy < 0) ed.cy = 0;
    if (ed.cy >= ed.num_lines) ed.cy = ed.num_lines - 1;

    ed.cx += dx;
    int len = ed.lines[ed.cy].length;
    if (ed.cx < 0) ed.cx = 0;
    if (ed.cx > len) ed.cx = len;

    int cols = 80, rows = 25;
    get_screen_size(&cols, &rows);

    if (ed.cy < ed.sy) ed.sy = ed.cy;
    if (ed.cy >= ed.sy + (rows - 2)) ed.sy = ed.cy - (rows - 3);

    if (ed.cx < ed.sx) ed.sx = ed.cx;
    if (ed.cx >= ed.sx + cols) ed.sx = ed.cx - cols + 1;
}

static void run_editor(void) {
    while (1) {
        editor_move_cursor(0, 0);
        draw_editor();
        char c = getchar_blocking();

        if (c == CTRL_Q) {
            break;
        } else if (c == CTRL_S) {
            save_file();
        } else if (c == CTRL_Z) {
            pop_undo();
        } else if (c == KEY_UP) {
            editor_move_cursor(0, -1);
        } else if (c == KEY_DOWN) {
            editor_move_cursor(0, 1);
        } else if (c == KEY_LEFT) {
            editor_move_cursor(-1, 0);
        } else if (c == KEY_RIGHT) {
            editor_move_cursor(1, 0);
        } else if (c == BACKSPACE) {
            save_undo();
            if (ed.cx > 0) {
                for (int i = ed.cx; i <= ed.lines[ed.cy].length; i++) {
                    ed.lines[ed.cy].text[i - 1] = ed.lines[ed.cy].text[i];
                }
                ed.lines[ed.cy].length--;
                ed.cx--;
                ed.is_modified = 1;
            } else if (ed.cy > 0) {
                int prev_len = ed.lines[ed.cy - 1].length;
                int cur_len = ed.lines[ed.cy].length;
                
                ensure_line_capacity(&ed.lines[ed.cy - 1], prev_len + cur_len + 1);
                if (cur_len > 0) {
                    u_memcpy(ed.lines[ed.cy - 1].text + prev_len, ed.lines[ed.cy].text, cur_len + 1);
                }
                ed.lines[ed.cy - 1].length = prev_len + cur_len;
                
                free_line(&ed.lines[ed.cy]);
                for (int i = ed.cy; i < ed.num_lines - 1; i++) {
                    ed.lines[i] = ed.lines[i + 1];
                }
                ed.num_lines--;
                ed.cy--;
                ed.cx = prev_len;
                ed.is_modified = 1;
            }
        } else if (c == '\n' || c == '\r') {
            save_undo();
            
            ensure_lines_capacity(&ed, ed.num_lines + 1);
            for (int i = ed.num_lines; i > ed.cy + 1; i--) {
                ed.lines[i] = ed.lines[i - 1];
            }
            
            int rem_len = ed.lines[ed.cy].length - ed.cx;
            ed.lines[ed.cy + 1].text = NULL;
            ed.lines[ed.cy + 1].length = 0;
            ed.lines[ed.cy + 1].capacity = 0;
            ensure_line_capacity(&ed.lines[ed.cy + 1], rem_len + 1);
            
            if (rem_len > 0) {
                u_memcpy(ed.lines[ed.cy + 1].text, ed.lines[ed.cy].text + ed.cx, rem_len + 1);
            } else {
                ed.lines[ed.cy + 1].text[0] = '\0';
            }
            ed.lines[ed.cy + 1].length = rem_len;
            
            ed.lines[ed.cy].text[ed.cx] = '\0';
            ed.lines[ed.cy].length = ed.cx;
            
            ed.num_lines++;
            ed.cy++;
            ed.cx = 0;
            ed.is_modified = 1;
        } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
            save_undo();
            ensure_line_capacity(&ed.lines[ed.cy], ed.lines[ed.cy].length + 2);
            for (int i = ed.lines[ed.cy].length; i >= ed.cx; i--) {
                ed.lines[ed.cy].text[i + 1] = ed.lines[ed.cy].text[i];
            }
            ed.lines[ed.cy].text[ed.cx] = c;
            ed.lines[ed.cy].length++;
            ed.cx++;
            ed.is_modified = 1;
        } else if (c == '\t') {
            save_undo();
            for (int k = 0; k < 4; k++) {
                ensure_line_capacity(&ed.lines[ed.cy], ed.lines[ed.cy].length + 2);
                for (int i = ed.lines[ed.cy].length; i >= ed.cx; i--) {
                    ed.lines[ed.cy].text[i + 1] = ed.lines[ed.cy].text[i];
                }
                ed.lines[ed.cy].text[ed.cx] = ' ';
                ed.lines[ed.cy].length++;
                ed.cx++;
            }
            ed.is_modified = 1;
        }
    }
}

// ====================================================================
// ФАЙЛОВЫЙ ПРОВОДНИК
// ====================================================================

static void load_dir(const char* path) {
    u_memset(&picker, 0, sizeof(Picker));
    u_strncpy(picker.current_path, path, sizeof(picker.current_path));
    
    if (u_strcmp(path, "/") != 0) {
        u_strncpy(picker.entries[picker.count].name, "..", 32);
        picker.entries[picker.count].type = TYPE_DIR;
        picker.count++;
    }

    int fd = open(path, READ);
    if (fd >= 0) {
        dirent_t d;
        uint32_t idx = 0;
        while (picker.count < MAX_DIR_ENTRIES) {
            u_memset(&d, 0, sizeof(dirent_t));
            if (readdir(fd, idx++, &d) != 0) break;
            if (d.name[0] == '\0') break;
            if (u_strcmp(d.name, ".") == 0) continue;

            u_strncpy(picker.entries[picker.count].name, d.name, 32);
            picker.entries[picker.count].type = d.type;
            picker.entries[picker.count].size = d.size;
            picker.count++;
        }
        close(fd);
    }
}

static void draw_picker(void) {
    clear_screen(0x07);

    gotoxy(0, 0);
    print("\033[97;44m"); 
    for(int i = 0; i < 80; i++) print(" "); 
    gotoxy(0, 0);
    print("Select file to edit | Path: "); 

    print(picker.current_path); print("\033[0m\n\n");

    for (int i = 0; i < 20; i++) {
        int idx = picker.scroll + i;
        if (idx < (int)picker.count) {
            if (idx == picker.selected) print("\033[30;46m > ");
            else print("   ");

            if (picker.entries[idx].type == TYPE_DIR) print("\033[93m[DIR] ");
            else print("\033[97m      ");

            print(picker.entries[idx].name);
            
            for (int s = 0; s < 30 - u_strlen(picker.entries[idx].name); s++) print(" ");
            if (picker.entries[idx].type == TYPE_FILE) {
                char sbuf[16];
                u_format_num(picker.entries[idx].size, sbuf, sizeof(sbuf));
                print(sbuf); print(" B");
            }
            print("\033[0m\n");
        }
    }
    
    gotoxy(0, 24);
    print("\033[97;47m"); 
    for(int i = 0; i < 80; i++) print(" "); 
    gotoxy(0, 24);
    print("\033[90;47m [W/S/Arrows] Nav | [Enter] Open | [N] New File | [Q] Exit \033[0m");
}

static int run_picker(char* out_path) {
    if (picker.current_path[0] == '\0') {
        load_dir("/");
    } else {
        load_dir(picker.current_path);
    }

    while (1) {
        draw_picker();
        char key = getchar_blocking();

        if (key == 'q' || key == 'Q' || key == CTRL_Q) {
            return 0; 
        } else if (key == 'n' || key == 'N') {
            // Создание нового файла
            char name_buf[64];
            int nlen = 0;
            name_buf[0] = '\0';

            gotoxy(0, 24);
            print("\033[30;47m New file name: ");
            for (int k = 0; k < 60; k++) print(" ");
            gotoxy(16, 24);

            while (1) {
                char ch = getchar_blocking();
                if (ch == '\n' || ch == '\r') {
                    break;
                } else if (ch == BACKSPACE) {
                    if (nlen > 0) {
                        nlen--;
                        name_buf[nlen] = '\0';
                        gotoxy(16 + nlen, 24);
                        putchar(' ');
                        gotoxy(16 + nlen, 24);
                    }
                } else if (ch == ESC || ch == CTRL_Q) {
                    nlen = 0;
                    break;
                } else if ((unsigned char)ch >= 32 && (unsigned char)ch <= 126 && nlen < 60) {
                    name_buf[nlen++] = ch;
                    name_buf[nlen] = '\0';
                    putchar(ch);
                }
            }

            if (nlen > 0) {
                u_path_join(out_path, picker.current_path, name_buf, 128);
                // Создаем файл на диске
                int fd = open(out_path, WRITE | CREATE | TRUNCATE);
                if (fd >= 0) close(fd);
                return 1;
            }
        } else if (key == 'w' || key == '8' || key == KEY_UP) {
            if (picker.selected > 0) {
                picker.selected--;
                if (picker.selected < picker.scroll) picker.scroll = picker.selected;
            }
        } else if (key == 's' || key == '2' || key == KEY_DOWN) {
            if (picker.selected < (int)picker.count - 1) {
                picker.selected++;
                if (picker.selected >= picker.scroll + 20) picker.scroll++;
            }
        } else if (key == '\n' || key == '\r') {
            if (picker.count == 0) continue;
            dirent_t* cur = &picker.entries[picker.selected];

            if (cur->type == TYPE_DIR) {
                char new_path[128];
                if (u_strcmp(cur->name, "..") == 0) {
                    u_strncpy(new_path, picker.current_path, sizeof(new_path));
                    int len = u_strlen(new_path);
                    while (len > 1 && new_path[len - 1] != '/') len--;
                    if (len > 1) new_path[len - 1] = '\0';
                    else { new_path[0] = '/'; new_path[1] = '\0'; }
                } else {
                    u_path_join(new_path, picker.current_path, cur->name, sizeof(new_path));
                }
                load_dir(new_path);
            } else {
                u_path_join(out_path, picker.current_path, cur->name, 128);
                return 1;
            }
        }
    }
    return 0;
}

// ====================================================================
// ТОЧКА ВХОДА
// ====================================================================

int main(int argc, char* argv[]) {
    set_io_flags(0); 

    char target_path[128];
    target_path[0] = '\0';
    picker.current_path[0] = '\0';

    if (argc > 1 && argv[1][0] != '\0') {
        u_strncpy(target_path, argv[1], sizeof(target_path));
        // Если файла нет при запуске "edit <name>", сразу создаем его
        int test_fd = open(target_path, READ);
        if (test_fd < 0) {
            test_fd = open(target_path, WRITE | CREATE | TRUNCATE);
        }
        if (test_fd >= 0) close(test_fd);

        load_file(target_path);
        run_editor();
    }

    while (1) {
        if (!run_picker(target_path)) {
            break;
        }
        
        load_file(target_path);
        run_editor(); 
    }

    clear_screen(0x07);
    gotoxy(0, 0);
    exit(0);
}