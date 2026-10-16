#ifndef WM_RENDER_H
#define WM_RENDER_H
#include "photon.h"
void wm_renderer_init(uint8_t *vga_planes[4], const uint8_t *font);
void put_pixel(int x, int y, uint8_t color);
void fill_rect(int x, int y, int w, int h, uint8_t color);
void draw_bevel(int x, int y, int w, int h, uint8_t bg, int sunken);
void draw_char(int x, int y, unsigned char ch, uint8_t color);
void draw_text(int x, int y, const char *str, uint8_t color);
void draw_text_clipped(int x, int y, const char *str, int max_chars, uint8_t color);
void clear_desktop(uint8_t color);
void flip_vga(void);
#endif
