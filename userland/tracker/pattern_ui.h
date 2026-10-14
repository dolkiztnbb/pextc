#ifndef __PATTERN_UI_H__
#define __PATTERN_UI_H__

#include "photon.h"
#include "ulibc.h"
#include "defs.h"
#include "ui.h"
#include "tracker.h"

// ????????? ?????
#define CH_S_HLINE    0xC4 // ฤ (196)
#define CH_S_VLINE    0xB3 // ณ (179)
#define CH_S_TL       0xDA // ฺ (218)
#define CH_S_TR       0xBF // ฟ (191)
#define CH_S_BL       0xC0 // ภ (192)
#define CH_S_BR       0xD9 // ู (217)
#define CH_S_T_DOWN   0xC2 // ย (194)
#define CH_S_T_UP     0xC1 // ม (193)
#define CH_S_T_RIGHT  0xC3 // ร (195)
#define CH_S_T_LEFT   0xB4 // ด (180)
#define CH_S_CROSS    0xC5 // ล (197)

// ??????? ?????
#define CH_D_HLINE    0xCD // อ (205)
#define CH_D_VLINE    0xBA // บ (186)
#define CH_D_TL       0xC9 // ษ (201)
#define CH_D_TR       0xBB // ป (187)
#define CH_D_BL       0xC8 // ศ (200)
#define CH_D_BR       0xBC // ผ (188)
#define CH_D_T_RIGHT  0xCC // ฬ (204)
#define CH_D_T_LEFT   0xB9 // น (185)

// ????????? ???????????
#define CH_M_T_DOWN   0xD1 // ั (209: ??????? ???????????, ????????? ???)
#define CH_M_T_UP     0xCF // ฯ (207: ??????? ???????????, ????????? ????)

// 12 ????????? (????? ???????????? ???????? ??????: c = C#, d = D# ? ?.?.)
static const char* pt_note_chars[13] = {
    "..", "C", "c", "D", "d", "E", "F", "f", "G", "g", "A", "a", "B"
};

// ====================================================================
// ????????? ??????? ?????? (???? + ??????? ?????????)
// ====================================================================

static void draw_tracker_top_bar(void) {
    // ????? ?????????
    putc_attr(0, 0, 186, 0x07); // บ
    putc_attr(1, 0, 199, 0x07); // ว
    puts_attr(2, 0, " PhTracker ", 0x0F);
    putc_attr(13, 0, 182, 0x07); // ถ

    for (int i = 14; i < 79; i++) putc_attr(i, 0, 205, 0x08); // อ

    // --- ????? ???? ?????????? (BPM, Speed, Patlen, Step...) ---
    putc_attr(2, 1, 201, 0x08); putc_attr(40, 1, 187, 0x08);
    putc_attr(2, 5, 200, 0x08); putc_attr(40, 5, 188, 0x08);
    for (int i = 3; i < 40; i++) {
        putc_attr(i, 1, 205, 0x08);
        putc_attr(i, 5, 205, 0x08);
    }
    for (int i = 2; i < 5; i++) {
        putc_attr(2, i, 186, 0x08);
        putc_attr(40, i, 186, 0x08);
    }

    // ?????????
    puts_attr(4, 2, "BPM   ", 0x07);
    gotoxy(12, 2); printf("\033[44;93m %3d \033[0m", current_song->bpm);
    puts_attr(20, 2, "Step  ", 0x07);
    gotoxy(27, 2); printf("\033[44;93m %2d \033[0m", current_song->step);

    puts_attr(4, 3, "Speed ", 0x07);
    gotoxy(12, 3); printf("\033[44;93m %3d \033[0m", current_song->speed);
    puts_attr(20, 3, "Octave", 0x07);
    gotoxy(27, 3); printf("\033[44;93m %2d \033[0m", current_song->octave);

    puts_attr(4, 4, "Patlen", 0x07);
    gotoxy(12, 4); printf("\033[44;93m %3d \033[0m", DEFAULT_PATLEN);
    puts_attr(20, 4, "Divis ", 0x07);
    gotoxy(27, 4); printf("\033[44;93m %2d \033[0m", current_song->divis);

    // --- ?????? ????: ??????? ??????? (Order Matrix) ---
    putc_attr(44, 1, 201, 0x08); putc_attr(78, 1, 187, 0x08);
    putc_attr(44, 5, 200, 0x08); putc_attr(78, 5, 188, 0x08);
    for (int i = 45; i < 78; i++) {
        putc_attr(i, 1, 205, 0x08);
        putc_attr(i, 5, 205, 0x08);
    }
    for (int i = 2; i < 5; i++) {
        putc_attr(44, i, 186, 0x08);
        putc_attr(78, i, 186, 0x08);
    }

    // ?????? ?????? ??????? (??????? ????? ?????????)
    for (int o = 0; o < 3; o++) {
        int ord_idx = ctx->play_order + o;
        gotoxy(46, 2 + o);
        for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
            uint8_t p_num = (ord_idx < current_song->orders.order_count) 
                            ? current_song->orders.matrix[ord_idx][ch] : 0;
            if (o == 0) {
                printf("\033[36;40m%02d \033[0m", p_num); // ??????? ???????? ???
            } else {
                printf("\033[90;40m%02d \033[0m", p_num);
            }
        }
    }
}

// ====================================================================
// ????????? ????? ???????? (9 ???????)
// ====================================================================

void draw_pattern_editor(void) {
    draw_tracker_top_bar();

    // 1. ????? ??????? (?????? 6)
    // อ|1|อออ|2|อออ|3|อออ|4|อออ|5|อออ|6|อออ|7|อออ|8|อออ|9|ออ
    int cur_x = 0;
    for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
        char hdr[10];
        hdr[0] = '='; hdr[1] = '|'; hdr[2] = '1' + ch; hdr[3] = '|';
        hdr[4] = '='; hdr[5] = '='; hdr[6] = '='; hdr[7] = '=';
        hdr[8] = '\0';
        puts_attr(cur_x, 6, hdr, (ctx->cur_channel == ch) ? 0x0E : 0x08);
        cur_x += 8;
        if (ch < CHANNELS_COUNT - 1) {
            putc_attr(cur_x, 6, 209, 0x08); // ??????????? ??????? ? ?????
            cur_x += 1;
        }
    }

    // 2. ???????????? ???????? ?????? (?????? 7: Nt In Cmnd)
    cur_x = 0;
    for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
        puts_attr(cur_x,     7, "Nt", 0x0B);   // ???? (Cyan)
        puts_attr(cur_x + 2, 7, "In", 0x0A);   // ?????????? (Green)
        puts_attr(cur_x + 4, 7, "Cmnd", 0x03); // ??????? (Dark Cyan)
        cur_x += 8;
        if (ch < CHANNELS_COUNT - 1) {
            putc_attr(cur_x, 7, 179, 0x08); // 'ณ'
            cur_x += 1;
        }
    }

    pattern_t* pat = &current_song->patterns[0];
    int center_y = 15;

    for (int y = 8; y <= 23; y++) {
        int r = ctx->cur_row + (y - center_y);
        cur_x = 0;

        if (r < 0 || r >= DEFAULT_PATLEN) {
            for (int k = 0; k < 80; k++) putc_attr(k, y, ' ', 0x07);
            continue;
        }

        int is_play_row = (ctx->is_playing && ctx->play_row == r);
        int is_cur_row  = (ctx->cur_row == r);

        for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
            pattern_note* n = &pat->rows[r][ch];
            int is_cell_cursor = (is_cur_row && ctx->cur_channel == ch);

            uint8_t base_bg = 0x00;
            if (is_play_row)     base_bg = 0x20; // ??????? ??? ??? ???????? ??????
            else if (r % 4 == 0) base_bg = 0x10; // ????????? ??? ??? ??????? 4-?? ?????

            // 1. ???? ???? (2 ???????)
            uint8_t nt_attr = base_bg | 0x0B;
            char nt_str[3] = "..";
            if (n->note == NOTE_OFF) {
                nt_str[0] = '='; nt_str[1] = '='; nt_attr = base_bg | 0x0C;
            } else if (n->note == NOTE_HOLD) {
                nt_str[0] = '-'; nt_str[1] = '-'; nt_attr = base_bg | 0x07;
            } else if (n->note >= 1 && n->note <= 12) {
                nt_str[0] = pt_note_chars[n->note][0];
                nt_str[1] = '0' + n->octave;
                nt_attr = base_bg | 0x0F;
            }
            if (is_cell_cursor && ctx->cur_field == 0) nt_attr = 0x70;

            putc_attr(cur_x,     y, nt_str[0], nt_attr);
            putc_attr(cur_x + 1, y, nt_str[1], nt_attr);

            // 2. ???? ??????????? (2 ???????)
            uint8_t in_attr = base_bg | 0x09;
            char in_str[3] = "..";
            if (n->note > 0 && n->note <= 12) {
                in_str[0] = '0' + (n->instrument / 10);
                in_str[1] = '0' + (n->instrument % 10);
                in_attr = base_bg | 0x0E;
            }
            if (is_cell_cursor && ctx->cur_field == 1) in_attr = 0x70;

            putc_attr(cur_x + 2, y, in_str[0], in_attr);
            putc_attr(cur_x + 3, y, in_str[1], in_attr);

            // 3. ???? ??????? (4 ???????)
            uint8_t cmd_attr = base_bg | 0x02;
            char cmd_str[5] = "....";
            if (n->command != 0) {
                snprintf(cmd_str, sizeof(cmd_str), "%04X", n->command);
                cmd_attr = base_bg | 0x0A;
            }
            if (is_cell_cursor && ctx->cur_field == 2) cmd_attr = 0x70;

            for (int k = 0; k < 4; k++) {
                putc_attr(cur_x + 4 + k, y, cmd_str[k], cmd_attr);
            }

            cur_x += 8;
            if (ch < CHANNELS_COUNT - 1) {
                char sep = (is_cur_row) ? CH_D_VLINE : CH_S_VLINE; // บ ?? ?????? ???????, ????? ณ
                putc_attr(cur_x, y, sep, (r % 4 == 0) ? 0x07 : 0x08);
                cur_x += 1;
            }
        }
    }

    // 4. ?????? ??????-??? (?????? 24)
    gotoxy(0, 24);
    printf("\033[30;47m Row %02d/32 ณ Ch %d ณ Ins: \033[44;93m %02d \033[30;47m ณ [Space] Play ณ [Tab] Ins Editor \033[0m",
           ctx->cur_row, ctx->cur_channel + 1, ctx->selected_ins + 1);
}

// ====================================================================
// ????????? ????? ? ????????? ????????
// ====================================================================

static void put_pattern_note(uint8_t note, uint8_t oct) {
    pattern_t* pat = &current_song->patterns[0];
    pattern_note* cell = &pat->rows[ctx->cur_row][ctx->cur_channel];

    cell->note = note;
    cell->octave = oct;
    
    if (note != NOTE_EMPTY && note != NOTE_OFF) {
        // ?????????? ??????? ????????? ?????????? ? ??????!
        cell->instrument = ctx->selected_ins + 1;
        
        // ????????? ????????? ????? ??????????? ? ?????? ? ????????????? ????
        apply_instrument_to_channel(&current_song->instruments[ctx->selected_ins], ctx->cur_channel);
        opl_channel_note_on(ctx->cur_channel, note, oct);
    } else if (note == NOTE_OFF) {
        opl_channel_note_off(ctx->cur_channel);
    }

    // ??????? ???? ?? ???????? Step
    ctx->cur_row += current_song->step;
    if (ctx->cur_row >= DEFAULT_PATLEN) {
        ctx->cur_row = DEFAULT_PATLEN - 1;
    }
    // ??????????
    if (ctx->cur_row >= ctx->scroll_row + 16) {
        ctx->scroll_row = ctx->cur_row - 15;
    }
}


static void put_pattern_cell(uint8_t note, uint8_t oct) {
    pattern_t* pat = &current_song->patterns[0];
    pattern_note* cell = &pat->rows[ctx->cur_row][ctx->cur_channel];

    if (ctx->allow_write) {
        cell->note = note;
        cell->octave = oct;
        if (note >= 1 && note <= 12) {
            cell->instrument = ctx->selected_ins + 1;
        }
    }

    // ??????????????? ????? ? ??????????????? ???????????? ?????????
    if (note >= 1 && note <= 12) {
        apply_instrument_to_channel(&current_song->instruments[ctx->selected_ins], ctx->cur_channel);
        opl_channel_note_on(ctx->cur_channel, note, oct);
    } else if (note == NOTE_OFF) {
        opl_channel_note_off(ctx->cur_channel);
    }

    if (ctx->allow_write) {
        ctx->cur_row = (ctx->cur_row + current_song->step) % DEFAULT_PATLEN;
    }
}

void handle_pattern_input(char key) {
    // 1. ????? ??????????? [ ? ]
    if (key == '[' || key == '-') {
        if (ctx->selected_ins > 0) ctx->selected_ins--;
        return;
    }
    if (key == ']' || key == '=') {
        if (ctx->selected_ins < MAX_INSTRUMENTS - 1) ctx->selected_ins++;
        return;
    }

    // 2. ????????? ?????????? ???????? ?????? ????
    if (key == 17) current_song->bpm = (current_song->bpm >= 200) ? 120 : current_song->bpm + 10;
    if (key == 'A' || key == 'a') current_song->bpm = (current_song->bpm >= 220) ? 100 : current_song->bpm + 10;
    if (key == 'F' || key == 'f') current_song->speed = (current_song->speed >= 12) ? 3 : current_song->speed + 1;
    if (key == 'K' || key == 'k') current_song->octave = (current_song->octave >= 7) ? 2 : current_song->octave + 1;
    if (key == '\'') current_song->step = (current_song->step >= 4) ? 1 : current_song->step + 1;

    // 3. ??????? (?????????)
    if (key == KEY_UP) {
        if (ctx->cur_row > 0) ctx->cur_row--;
        else ctx->cur_row = DEFAULT_PATLEN - 1;
    } else if (key == KEY_DOWN) {
        ctx->cur_row = (ctx->cur_row + 1) % DEFAULT_PATLEN;
    } else if (key == KEY_LEFT) {
        if (ctx->cur_field > 0) {
            ctx->cur_field--;
        } else if (ctx->cur_channel > 0) {
            ctx->cur_channel--;
            ctx->cur_field = 2;
        }
    } else if (key == KEY_RIGHT) {
        if (ctx->cur_field < 2) {
            ctx->cur_field++;
        } else if (ctx->cur_channel < CHANNELS_COUNT - 1) {
            ctx->cur_channel++;
            ctx->cur_field = 0;
        }
    }

    // 4. ??????? / ???? / ?????????
    else if (key == BACKSPACE || key == '\b' || key == '.') {
        pattern_t* pat = &current_song->patterns[0];
        pat->rows[ctx->cur_row][ctx->cur_channel].note = NOTE_EMPTY;
        pat->rows[ctx->cur_row][ctx->cur_channel].instrument = 0;
        pat->rows[ctx->cur_row][ctx->cur_channel].command = 0;
        opl_channel_note_off(ctx->cur_channel);
    } 
    else if (key == '\\' || key == '`') {
        put_pattern_cell(NOTE_OFF, 0); // === KeyOff
    }
    else if (key == '=' || key == '_') {
        put_pattern_cell(NOTE_HOLD, 0); // --- Sustain
    }

    // 5. ???? ??? (2 ?????? ???????)
    else if (ctx->cur_field == 0) {
        uint8_t oct = current_song->octave;

        // ?????? ??? (?????? oct)
        if (key == 'z' || key == 'Z')      put_pattern_cell(1, oct);
        else if (key == 's' || key == 'S') put_pattern_cell(2, oct);
        else if (key == 'x' || key == 'X') put_pattern_cell(3, oct);
        else if (key == 'd' || key == 'D') put_pattern_cell(4, oct);
        else if (key == 'c' || key == 'C') put_pattern_cell(5, oct);
        else if (key == 'v' || key == 'V') put_pattern_cell(6, oct);
        else if (key == 'g' || key == 'G') put_pattern_cell(7, oct);
        else if (key == 'b' || key == 'B') put_pattern_cell(8, oct);
        else if (key == 'h' || key == 'H') put_pattern_cell(9, oct);
        else if (key == 'n' || key == 'N') put_pattern_cell(10, oct);
        else if (key == 'j' || key == 'J') put_pattern_cell(11, oct);
        else if (key == 'm' || key == 'M') put_pattern_cell(12, oct);
        else if (key == ',')               put_pattern_cell(1, oct + 1);
        else if (key == 'l' || key == 'L') put_pattern_cell(2, oct + 1);
        else if (key == '.'              ) put_pattern_cell(3, oct + 1);
        else if (key == ';')               put_pattern_cell(4, oct + 1);
        else if (key == '/')               put_pattern_cell(5, oct + 1);

        // ??????? ??? (?????? oct + 1)
        else if (key == 'q' || key == 'Q') put_pattern_cell(1, oct + 1);
        else if (key == '2')               put_pattern_cell(2, oct + 1);
        else if (key == 'w' || key == 'W') put_pattern_cell(3, oct + 1);
        else if (key == '3')               put_pattern_cell(4, oct + 1);
        else if (key == 'e' || key == 'E') put_pattern_cell(5, oct + 1);
        else if (key == 'r' || key == 'R') put_pattern_cell(6, oct + 1);
        else if (key == '5')               put_pattern_cell(7, oct + 1);
        else if (key == 't' || key == 'T') put_pattern_cell(8, oct + 1);
        else if (key == '6')               put_pattern_cell(9, oct + 1);
        else if (key == 'y' || key == 'Y') put_pattern_cell(10, oct + 1);
        else if (key == '7')               put_pattern_cell(11, oct + 1);
        else if (key == 'u' || key == 'U') put_pattern_cell(12, oct + 1);
        else if (key == 'i' || key == 'I') put_pattern_cell(1, oct + 2);
        else if (key == '9')               put_pattern_cell(2, oct + 2);
        else if (key == 'o' || key == 'O') put_pattern_cell(3, oct + 2);
        else if (key == '0')               put_pattern_cell(4, oct + 2);
        else if (key == 'p' || key == 'P') put_pattern_cell(5, oct + 2);
        else if (key == '-')               put_pattern_cell(6, oct + 2);
    }
}

#endif