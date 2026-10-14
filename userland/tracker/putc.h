#ifndef __PT_PUTC__
#define __PT_PUTC__

#include "photon.h"

// Таблицы соответствия цветов VGA (0..15) кодам ANSI SGR
static const char* vga_fg_normal[8] = {
    "30", "34", "32", "36", "31", "35", "33", "37"
};

static const char* vga_fg_bright[8] = {
    "90", "94", "92", "96", "91", "95", "93", "97"
};

static const char* vga_bg_table[8] = {
    "40", "44", "42", "46", "41", "45", "43", "47"
};

/**
 * Выводит 1 символ с цветом VGA (attr: младшие 4 бита - текст, старшие 4 - фон)
 */
static inline void putc_attr(int x, int y, char ch, uint8_t attr) {
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;

    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x07;

    const char* fg_str = (fg >= 8) ? vga_fg_bright[fg & 7] : vga_fg_normal[fg];
    const char* bg_str = vga_bg_table[bg];

    // Разделяем на ДВЕ независимые команды без точки с запятой:
    // "\033[FFm\033[BBmC\0" (ровно 11 байт)
    char buf[16];
    buf[0]  = '\033'; buf[1]  = '[';
    buf[2]  = fg_str[0]; buf[3]  = fg_str[1];
    buf[4]  = 'm';
    buf[5]  = '\033'; buf[6]  = '[';
    buf[7]  = bg_str[0]; buf[8]  = bg_str[1];
    buf[9]  = 'm';
    buf[10] = (ch == '\0') ? ' ' : ch;
    buf[11] = '\0';

    gotoxy(x, y);
    print(buf);
}

/**
 * Выводит строку с цветом VGA
 */
static inline void puts_attr(int x, int y, const char* str, uint8_t attr) {
    if (x < 0 || y < 0 || y >= 25 || !str) return;

    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x07;

    const char* fg_str = (fg >= 8) ? vga_fg_bright[fg & 7] : vga_fg_normal[fg];
    const char* bg_str = vga_bg_table[bg];

    char prefix[16];
    prefix[0] = '\033'; prefix[1] = '[';
    prefix[2] = fg_str[0]; prefix[3] = fg_str[1];
    prefix[4] = 'm';
    prefix[5] = '\033'; prefix[6] = '[';
    prefix[7] = bg_str[0]; prefix[8] = bg_str[1];
    prefix[9] = 'm';
    prefix[10] = '\0';

    gotoxy(x, y);
    print(prefix);
    print(str);
}

#endif