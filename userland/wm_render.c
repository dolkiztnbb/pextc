#include "photon.h"
#include "ulibc.h"
#include "vga_12.h"
#include "gui_shm.h"
#include "wm_render.h"

#define SCREEN_W 640
#define SCREEN_H 480
#define ROW_BYTES (SCREEN_W / 8)
#define TOTAL_BYTES (ROW_BYTES * SCREEN_H)
static uint8_t **planes;
static const uint8_t *font_data;
static volatile uint8_t *vram = (volatile uint8_t*)0xA0000;

void wm_renderer_init(uint8_t *vga_planes[4], const uint8_t *font) {
    planes = vga_planes;
    font_data = font;
}

void put_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    int idx = y * ROW_BYTES + (x / 8);
    uint8_t mask = 0x80 >> (x % 8);
    for (int p = 0; p < 4; p++) {
        if (color & (1 << p)) planes[p][idx] |= mask;
        else                  planes[p][idx] &= ~mask;
    }
}

void fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    for (int cy = y; cy < y + h; cy++)
        for (int cx = x; cx < x + w; cx++)
            put_pixel(cx, cy, color);
}

void draw_bevel(int x, int y, int w, int h, uint8_t bg, int sunken) {
    uint8_t tl = sunken ? C_DARKGRAY : C_WHITE;
    uint8_t br = sunken ? C_WHITE    : C_BLACK;
    uint8_t in = sunken ? C_BLACK    : C_DARKGRAY;

    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, w, 1, tl);
    fill_rect(x, y, 1, h, tl);
    fill_rect(x, y + h - 1, w, 1, br);
    fill_rect(x + w - 1, y, 1, h, br);
    if (!sunken && w > 2 && h > 2) {
        fill_rect(x + 1, y + h - 2, w - 2, 1, in);
        fill_rect(x + w - 2, y + 1, 1, h - 2, in);
    }
}

void draw_char(int x, int y, unsigned char ch, uint8_t color) {
    if (!font_data) return;
    const uint8_t* glyph = &font_data[ch * 16];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

void draw_text(int x, int y, const char* str, uint8_t color) {
    if (!str) return;
    while (*str) {
        draw_char(x, y, (unsigned char)*str++, color);
        x += 8;
    }
}

// Строка не длиннее max_chars символов (защита от строк без '\0' в заголовке окна)
void draw_text_clipped(int x, int y, const char* str, int max_chars, uint8_t color) {
    if (!str) return;
    int count = 0;
    while (*str && count < max_chars) {
        draw_char(x, y, (unsigned char)*str++, color);
        x += 8;
        count++;
    }
}

void clear_desktop(uint8_t color) {
    for (int p = 0; p < 4; p++) {
        uint8_t mask = (color & (1 << p)) ? 0xFF : 0x00;
        u_memset(planes[p], mask, TOTAL_BYTES);
    }
}

void flip_vga(void) {
    for (int p = 0; p < 4; p++) {
        vga_outw(0x3C4, (uint16_t)((1 << (p + 8)) | 0x02));
        u_memcpy((void*)vram, planes[p], TOTAL_BYTES);
    }
}

