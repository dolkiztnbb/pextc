#include "photon.h"
#include "ulibc.h"
#include "defs.h"
#include "ui.h"

// Символы для отрисовки рамок
#define C_TL '\xC9' // ╔
#define C_TR '\xBB' // ╗
#define C_BL '\xC8' // ╚
#define C_BR '\xBC' // ╝
#define C_H  '\xCD' // ═
#define C_V  '\xBA' // ║
#define C_T_DOWN '\xCB' // ╦
#define C_T_UP   '\xCA' // ╩

// ====================================================================
// СИСТЕМА ФОКУСА (НАВИГАЦИЯ ПО ЭКРАНУ)
// ====================================================================

typedef enum {
    ZONE_TOP_PARAMS,    // BPM, Speed...
    ZONE_ORDER_MATRIX,  // 00 01 02...
    ZONE_INS_SELECT,    // Selected instrument
    ZONE_FB_SLIDER,     // FB ползунок
    ZONE_MOD1_SLIDERS,  // At, Dc, Su... (Mod1)
    ZONE_MOD1_TOGGLES,  // Ampl.Mod, Vibrato... (Mod1)
    ZONE_MOD1_WAVE,     // Wf (Mod1)
    ZONE_MOD2_SLIDERS,  // (Mod2)
    ZONE_MOD2_TOGGLES,  // (Mod2)
    ZONE_MOD2_WAVE,     // (Mod2)
    ZONE_ALGORITHM      // Algr (A-M, F-M)
} ui_zone_t;

static ui_zone_t current_zone = ZONE_MOD1_SLIDERS;
static int sub_index = 0; // Позиция внутри текущей зоны

// ====================================================================
// ОТРИСОВКА БАЗОВЫХ РАМОК
// ====================================================================

static void draw_box(int x, int y, int w, int h, const char* footer, uint8_t color) {
    putc_attr(x, y, C_TL, color);
    putc_attr(x + w - 1, y, C_TR, color);
    putc_attr(x, y + h - 1, C_BL, color);
    putc_attr(x + w - 1, y + h - 1, C_BR, color);

    for (int i = 1; i < w - 1; i++) {
        putc_attr(x + i, y, C_H, color);
        putc_attr(x + i, y + h - 1, C_H, color);
    }
    for (int i = 1; i < h - 1; i++) {
        putc_attr(x, y + i, C_V, color);
        putc_attr(x + w - 1, y + i, C_V, color);
    }

    if (footer) {
        int flen = u_strlen(footer);
        int fx = x + (w - flen - 4) / 2;
        putc_attr(fx, y + h - 1, 180, color); // ┤
        puts_attr(fx + 1, y + h - 1, " ", color);
        puts_attr(fx + 2, y + h - 1, footer, 0x07); // Текст футера
        puts_attr(fx + 2 + flen, y + h - 1, " ", color);
        putc_attr(fx + 3 + flen, y + h - 1, 195, color); // ├
    }
}

// ====================================================================
// ОТРИСОВКА ВТОРОГО ЭКРАНА (INSTRUMENT EDITOR)
// ====================================================================

// Рисует блок одного оператора (Mod1 или Mod2)
static void draw_operator_box(int x, int y, opl_operator_t* op, const char* name, int is_mod1) {
    draw_box(x, y, 33, 16, name, 0x08);

    // 1. Слайдеры (At, Dc, Su, Rl, Mu, LS, TL)
    const char* sl_names[] = {"At", "Dc", "Su", "Rl", "Mu", "LS", "TL"};
    uint8_t sl_colors[] = {0x07, 0x07, 0x07, 0x07, 0x0B, 0x0B, 0x0D}; // Белый, Циан, Пурпурный
    uint8_t* sl_vals[] = {&op->attack, &op->decay, &op->sustain, &op->release, &op->mult, &op->ksl, &op->total_level};
    int sl_max[] = {15, 15, 15, 15, 15, 3, 63};

    int is_active_sliders = (current_zone == (is_mod1 ? ZONE_MOD1_SLIDERS : ZONE_MOD2_SLIDERS));

    for (int i = 0; i < 7; i++) {
        puts_attr(x + 2, y + 2 + i, sl_names[i], sl_colors[i]);
        
        slider s = {0, *sl_vals[i], sl_max[i]};
        int selected = (is_active_sliders && sub_index == i);
        paint_slider(s, x + 5, y + 2 + i, selected);
    }

    // 2. Тумблеры (Ampl.Mod, Vibrato, Scale, Sustain)
    int is_active_tgl = (current_zone == (is_mod1 ? ZONE_MOD1_TOGGLES : ZONE_MOD2_TOGGLES));
    
    button b_am = {13, "Ampl. Mod", op->tremolo};
    button b_vb = {13, "Vibrato", op->vibrato};
    button b_sc = {13, "Scale", op->ksr};
    button b_su = {13, "Sustain", op->eg_type};

    paint_button(b_am, x + 3, y + 10, is_active_tgl && sub_index == 0);
    paint_button(b_vb, x + 3, y + 11, is_active_tgl && sub_index == 1);
    paint_button(b_sc, x + 19, y + 10, is_active_tgl && sub_index == 2);
    paint_button(b_su, x + 19, y + 11, is_active_tgl && sub_index == 3);

    // 3. Форма волны
    int is_active_wf = (current_zone == (is_mod1 ? ZONE_MOD1_WAVE : ZONE_MOD2_WAVE));
    puts_attr(x + 2, y + 13, "Wf", 0x0F);

    button b_wf[4] = {
        {6, "Sine", op->waveform == 0},
        {6, "Half", op->waveform == 1},
        {6, "Abs",  op->waveform == 2},
        {6, "Qrt",  op->waveform == 3}
    };

    for(int i = 0; i < 4; i++) {
        paint_button(b_wf[i], x + 5 + (i * 6), y + 13, is_active_wf && sub_index == i);
    }
}

void draw_instrument_editor() {
    if (!current_song || !current_song->instruments || !ctx) return;

    if (ctx->selected_ins < 0 || ctx->selected_ins >= MAX_INSTRUMENTS) {
        ctx->selected_ins = 0;
    }

    instrument_t* ins = &current_song->instruments[ctx->selected_ins];

    // =========================================================================
    // СТРОКА 9: "Selected instrument" (число $) слева и "FB" (слайдер .) справа
    // =========================================================================
    
    // 1. Левая часть: "Selected instrument" + плашка с числом (НЕ слайдер!)
    puts_attr(2, 7, "Selected instrument", 0x07);
    
    int is_ins_sel = (current_zone == ZONE_INS_SELECT);
    gotoxy(23, 7);
    // Бейдж с номером текущего инструмента [01]
    if (is_ins_sel) {
        printf("\033[44;93m %02d \033[0m", ctx->selected_ins + 1); // Подсветка желтым при выборе
    } else {
        printf("\033[40;34m%c\033[44;37m%02d\033[40;34m%c\033[0m", SLIDER_WINDOW_L, ctx->selected_ins + 1, SLIDER_WINDOW_R);
    }

    puts_attr(42, 7, "FB", 0x07);
    slider s_fb = {0, ins->feedback, 7};
    paint_slider(s_fb, 45, 7, current_zone == ZONE_FB_SLIDER);

    // Левый блок (Mod1)
    draw_operator_box(1, 8, &ins->mod, "Mod1", 1);
    
    // Правый блок (Mod2)
    draw_operator_box(36, 8, &ins->car, "Mod2", 0);

    // Блок Алгоритма
    draw_box(73, 8, 7, 16, NULL, 0x08);
    puts_attr(74, 13, "Algr", 0x0F);
    
    button b_am = {5, "A-M", ins->connection == 1};
    button b_fm = {5, "F-M", ins->connection == 0};
    
    int is_alg = (current_zone == ZONE_ALGORITHM);
    paint_button(b_am, 76, 15, is_alg && sub_index == 0);
    paint_button(b_fm, 76, 17, is_alg && sub_index == 1);

    // Нижняя инфо-строка (@)
    gotoxy(0, 24);
    printf("\033[30;47m Col %d/32 Pat %d │ %-45s │ Ins Editor [Tab] \033[0m", 
           ctx->cur_row, ctx->play_order, "Use Arrows to move, Space to toggle");
}

// ====================================================================
// ОБРАБОТЧИК КЛАВИШ (НАВИГАЦИЯ И ВВОД)
// ====================================================================
void handle_instrument_input(char key) {
    instrument_t* ins = &current_song->instruments[ctx->selected_ins];

    if (key == '\\') {
        audition_note_press();
        return;
    }

    // ================================================================
    // НАВИГАЦИЯ ВВЕРХ
    // ================================================================
    if (key == KEY_UP || key == '8') {
        if (current_zone == ZONE_MOD1_SLIDERS || current_zone == ZONE_MOD2_SLIDERS) {
            if (sub_index > 0) {
                sub_index--;
            } else {
                // Прыжок со слайдеров вверх на шапку
                current_zone = (current_zone == ZONE_MOD1_SLIDERS) ? ZONE_INS_SELECT : ZONE_FB_SLIDER;
                sub_index = 0;
            }
        } 
        else if (current_zone == ZONE_MOD1_TOGGLES || current_zone == ZONE_MOD2_TOGGLES) {
            // Если мы на нижнем ряду кнопок (1: Vibrato или 3: Sustain) -> шаг ВВЕРХ на 1 (-1)
            if (sub_index == 1 || sub_index == 3) {
                sub_index--; // 1 -> 0 (Ampl.Mod), 3 -> 2 (Scale)
            } else {
                // Если мы на верхнем ряду (0 или 2) -> выходим ВВЕРХ на слайдеры (на TL)
                current_zone = (current_zone == ZONE_MOD1_TOGGLES) ? ZONE_MOD1_SLIDERS : ZONE_MOD2_SLIDERS;
                sub_index = 6; // Последний слайдер (TL)
            }
        }
        else if (current_zone == ZONE_MOD1_WAVE || current_zone == ZONE_MOD2_WAVE) {
            // Из блока форм волн выходим ВВЕРХ на нижний ряд кнопок
            current_zone = (current_zone == ZONE_MOD1_WAVE) ? ZONE_MOD1_TOGGLES : ZONE_MOD2_TOGGLES;
            sub_index = (sub_index < 2) ? 1 : 3; // К левой или правой кнопке ближе
        }
        else if (current_zone == ZONE_ALGORITHM) {
            if (sub_index > 0) sub_index--;
            else current_zone = ZONE_FB_SLIDER;
        }
    }

    // ================================================================
    // НАВИГАЦИЯ ВНИЗ
    // ================================================================
    else if (key == KEY_DOWN || key == '2') {
        if (current_zone == ZONE_INS_SELECT) { 
            current_zone = ZONE_MOD1_SLIDERS; sub_index = 0; 
        }
        else if (current_zone == ZONE_FB_SLIDER) { 
            current_zone = ZONE_MOD2_SLIDERS; sub_index = 0; 
        }
        else if (current_zone == ZONE_MOD1_SLIDERS || current_zone == ZONE_MOD2_SLIDERS) {
            if (sub_index < 6) {
                sub_index++;
            } else {
                // С последнего слайдера (TL) переходим ВНИЗ на верхний ряд кнопок
                current_zone = (current_zone == ZONE_MOD1_SLIDERS) ? ZONE_MOD1_TOGGLES : ZONE_MOD2_TOGGLES;
                sub_index = 0; // На 0 (Ampl.Mod)
            }
        }
        else if (current_zone == ZONE_MOD1_TOGGLES || current_zone == ZONE_MOD2_TOGGLES) {
            // Если мы на верхнем ряду кнопок (0: Ampl.Mod или 2: Scale) -> шаг ВНИЗ на 1 (+1)
            if (sub_index == 0 || sub_index == 2) {
                sub_index++; // 0 -> 1 (Vibrato), 2 -> 3 (Sustain)
            } else {
                // С нижнего ряда кнопок (1 или 3) выходим ВНИЗ на форму волны (Wf)
                current_zone = (current_zone == ZONE_MOD1_TOGGLES) ? ZONE_MOD1_WAVE : ZONE_MOD2_WAVE;
                sub_index = (sub_index == 1) ? 0 : 2; // Sine или Abs
            }
        }
        else if (current_zone == ZONE_ALGORITHM) {
            if (sub_index < 1) sub_index++;
        }
    }

    // ================================================================
    // НАВИГАЦИЯ ВЛЕВО-ВПРАВО / ИЗМЕНЕНИЕ СЛАЙДЕРА
    // ================================================================
    else if (key == KEY_LEFT || key == KEY_RIGHT) {
        int dir = (key == KEY_RIGHT) ? 1 : -1;

        // Перемещение по кнопкам-тумблерам (Ampl.Mod, Scale, Vibrato, Sustain)
        if (current_zone == ZONE_MOD1_TOGGLES || current_zone == ZONE_MOD2_TOGGLES) {
            if (dir > 0) {
                // Движение ВПРАВО
                if (sub_index < 2) {
                    sub_index += 2; // 0 -> 2 (Ampl.Mod -> Scale), 1 -> 3 (Vibrato -> Sustain)
                } else if (current_zone == ZONE_MOD1_TOGGLES) {
                    // Переход из правой колонки Mod1 в левую колонку Mod2
                    current_zone = ZONE_MOD2_TOGGLES;
                    sub_index -= 2; // 2 -> 0, 3 -> 1
                } else if (current_zone == ZONE_MOD2_TOGGLES) {
                    // Переход из правой колонки Mod2 в блок Алгоритма
                    current_zone = ZONE_ALGORITHM;
                    sub_index = (sub_index == 2) ? 0 : 1;
                }
            } else {
                // Движение ВЛЕВО
                if (sub_index >= 2) {
                    sub_index -= 2; // 2 -> 0 (Scale -> Ampl.Mod), 3 -> 1 (Sustain -> Vibrato)
                } else if (current_zone == ZONE_MOD2_TOGGLES) {
                    // Переход из левой колонки Mod2 в правую колонку Mod1
                    current_zone = ZONE_MOD1_TOGGLES;
                    sub_index += 2; // 0 -> 2, 1 -> 3
                }
            }
        }
        // Перемещение по 4 кнопкам формы волны (Sine, Half, Abs, Qrt)
        else if (current_zone == ZONE_MOD1_WAVE || current_zone == ZONE_MOD2_WAVE) {
            if (dir > 0 && sub_index < 3) {
                sub_index++;
            } else if (dir < 0 && sub_index > 0) {
                sub_index--;
            } else if (dir > 0 && current_zone == ZONE_MOD1_WAVE) {
                current_zone = ZONE_MOD2_WAVE;
                sub_index = 0;
            } else if (dir < 0 && current_zone == ZONE_MOD2_WAVE) {
                current_zone = ZONE_MOD1_WAVE;
                sub_index = 3;
            }
        }
        // Блок Алгоритма: возврат влево в Mod2
        else if (current_zone == ZONE_ALGORITHM) {
            if (dir < 0) {
                current_zone = ZONE_MOD2_TOGGLES;
                sub_index = (sub_index == 0) ? 2 : 3;
            }
        }
        // Иначе мы на слайдерах — изменяем значение текущего слайдера
        else {
            uint8_t* ptr = NULL;
            int max_val = 15;

            if (current_zone == ZONE_INS_SELECT) { 
                ptr = (uint8_t*)&ctx->selected_ins; 
                max_val = 15; 
            }
            else if (current_zone == ZONE_FB_SLIDER) { 
                ptr = &ins->feedback; 
                max_val = 7; 
            }
            else if (current_zone == ZONE_MOD1_SLIDERS || current_zone == ZONE_MOD2_SLIDERS) {
                opl_operator_t* op = (current_zone == ZONE_MOD1_SLIDERS) ? &ins->mod : &ins->car;
                uint8_t* vals[] = {&op->attack, &op->decay, &op->sustain, &op->release, &op->mult, &op->ksl, &op->total_level};
                int maxes[] = {15, 15, 15, 15, 15, 3, 63};
                ptr = vals[sub_index];
                max_val = maxes[sub_index];
            }

            if (ptr) {
                int val = (int)(*ptr) + dir;
                if (val < 0) val = 0;
                if (val > max_val) val = max_val;
                *ptr = (uint8_t)val;
            }
        }
    }

    // ================================================================
    // ПРОБЕЛ / ENTER (ПЕРЕКЛЮЧЕНИЕ ТУМБЛЕРОВ И КНОПОК)
    // ================================================================
    else if (key == ' ' || key == '\r' || key == '\n') {
        if (current_zone == ZONE_MOD1_TOGGLES || current_zone == ZONE_MOD2_TOGGLES) {
            opl_operator_t* op = (current_zone == ZONE_MOD1_TOGGLES) ? &ins->mod : &ins->car;
            if (sub_index == 0) op->tremolo = !op->tremolo;
            if (sub_index == 1) op->vibrato = !op->vibrato;
            if (sub_index == 2) op->ksr     = !op->ksr;
            if (sub_index == 3) op->eg_type = !op->eg_type;
        }
        else if (current_zone == ZONE_MOD1_WAVE || current_zone == ZONE_MOD2_WAVE) {
            opl_operator_t* op = (current_zone == ZONE_MOD1_WAVE) ? &ins->mod : &ins->car;
            op->waveform = sub_index; // 0=Sine, 1=Half, 2=Abs, 3=Qrt
        }
        else if (current_zone == ZONE_ALGORITHM) {
            ins->connection = (sub_index == 0) ? 1 : 0; // 0 = A-M (Additive), 1 = F-M (FM)
        }
    }
}