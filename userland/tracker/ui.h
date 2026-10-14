#ifndef __PT_UI__
#define __PT_UI__

#include "putc.h"
#include "ulibc.h"

#define SLIDER_START    195
#define SLIDER_END      180
#define SLIDER_SLIDER   196
#define SLIDER_THUMB    232

#define SLIDER_WINDOW_L 222
#define SLIDER_WINDOW_R 221

#define COLOR_INACTIVE 0x08     // Неактивный: темно-серый на черном
#define COLOR_ACTIVE   0x07     // Активный: светло-серый на черном
#define COLOR_NUM_BOX  0x1E     // Значение: яркий жёлтый на синем фоне (0x10 | 0x0E)

#define COL(x) x?COLOR_ACTIVE:COLOR_INACTIVE
#define COLOR_BTN_OFF_UNSEL   8   // Выкл, невыбранная
#define COLOR_BTN_OFF_SEL     7   // Выкл, выбранная
#define COLOR_BTN_ON_UNSEL    14  // Вкл, невыбранная
#define COLOR_BTN_ON_SEL      11  // Вкл, выбранная

#define BTN_COLOR(state, selected) \
    ((selected) ? ((state) ? COLOR_BTN_ON_SEL : COLOR_BTN_OFF_SEL) \
                : ((state) ? COLOR_BTN_ON_UNSEL : COLOR_BTN_OFF_UNSEL))

#include "defs.h"

void paint_slider(slider s, int x, int y, int state){
    putc_attr(x, y, SLIDER_START, COL(state));
    for(int i=1;i<22;i++){
        putc_attr(x+i, y, SLIDER_SLIDER, COL(state));
    }

    int slider_pos = (float)(s.dialed_in) / (float)(s.max_value-s.min_value) * 21. + 1;

    slider_pos = slider_pos>21?21:slider_pos; // clamp max
    slider_pos = slider_pos<1?1:slider_pos; // clamp min

    putc_attr(x+slider_pos, y, SLIDER_THUMB, COL(state));

    putc_attr(x+22, y, SLIDER_END, COL(state));

    gotoxy(x+23,y);
    
    printf("\033[40;34m%c\033[44;93m%2d\033[40;34m%c",
        SLIDER_WINDOW_L,
        s.dialed_in,
        SLIDER_WINDOW_R
    );
}

void paint_slider_collection(slider_collection s, int x, int y) {
    for(int i=0; i<s.amount; i++){
        paint_slider(s.sliders[i],x,y+i,i==s.selected);
    }
}

/**
 * Отрисовка кнопки-тумблера
 * @param b         Структура кнопки (size, sign, state)
 * @param x         Начальная координата X
 * @param y         Координата Y
 * @param selected  1 - если кнопка сейчас в фокусе (выбрана), 0 - нет
 */
void paint_button(button b, int x, int y, int selected) {
    const char* text = b.sign ? b.sign : "";
    int len = u_strlen(text);

    // Минимальная ширина кнопки: длина текста + 2 символа границы (├ и ┤)
    int width = b.size;
    if (width < len + 2) {
        width = len + 2;
    }

    uint8_t color = BTN_COLOR(b.state, selected);

    putc_attr(x, y, SLIDER_START, color);

    int inner_space = width - 2;
    int pad_total   = inner_space - len;
    int pad_left    = pad_total / 2;
    int pad_right   = pad_total - pad_left;

    int cur_x = x + 1;

    for (int i = 0; i < pad_left; i++, cur_x++) {
        putc_attr(cur_x, y, SLIDER_SLIDER, color);
    }

    for (int i = 0; i < len; i++, cur_x++) {
        char ch = text[i];
        if (ch == ' ') {
            ch = (char)SLIDER_SLIDER;
        }
        putc_attr(cur_x, y, ch, color);
    }

    for (int i = 0; i < pad_right; i++, cur_x++) {
        putc_attr(cur_x, y, SLIDER_SLIDER, color);
    }

    putc_attr(cur_x, y, SLIDER_END, color);
}

#endif