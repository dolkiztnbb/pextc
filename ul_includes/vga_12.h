#ifndef VGA_MODE12H_H
#define VGA_MODE12H_H

#include "photon.h"

// ====================================================================
// БАЗОВЫЕ ФУНКЦИИ ВВОДА/ВЫВОДА (IN / OUT)
// ====================================================================

static inline void vga_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t vga_inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void vga_outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

// ====================================================================
// ДАННЫЕ РЕГИСТРОВ ДЛЯ РЕЖИМА 640x480 @ 16 ЦВЕТОВ (MODE 12h)
// ====================================================================

// 1. Секвенсер (Регистры 1-4)
static const uint8_t vga_mode12h_seq[] = {
    0x01, // 0x01: Clocking Mode (8 точек на символ -> 80 * 8 = 640)
    0x0F, // 0x02: Map Mask (Разрешить запись во все 4 плоскости)
    0x00, // 0x03: Character Map Select
    0x06  // 0x04: Memory Mode (Extended Memory ON, Odd/Even OFF)
};

// 2. CRTC Контроллер (Регистры 0x00 - 0x18)
static const uint8_t vga_mode12h_crtc[] = {
    0x5F, // 0x00: Horizontal Total
    0x4F, // 0x01: Horizontal Display End (80 колонок - 1 = 79)
    0x50, // 0x02: Start Horizontal Blanking
    0x82, // 0x03: End Horizontal Blanking
    0x54, // 0x04: Start Horizontal Retrace
    0x80, // 0x05: End Horizontal Retrace
    0x0B, // 0x06: Vertical Total
    0x3E, // 0x07: Overflow
    0x00, // 0x08: Preset Row Scan
    0x40, // 0x09: Maximum Scan Line (480 строк, без удвоения)
    0x00, // 0x0A: Cursor Start
    0x00, // 0x0B: Cursor End
    0x00, // 0x0C: Start Address High
    0x00, // 0x0D: Start Address Low
    0x00, // 0x0E: Cursor Location High
    0x00, // 0x0F: Cursor Location Low
    0xEA, // 0x10: Vertical Retrace Start
    0x8C, // 0x11: Vertical Retrace End (Бит 7 = 1 блокирует запись 0-7)
    0xDF, // 0x12: Vertical Display End (480 строк - 1 = 479)
    0x28, // 0x13: Offset / Pitch (40 слов = 80 байт на строку сканирования)
    0x00, // 0x14: Underline Location
    0xE7, // 0x15: Start Vertical Blanking
    0x04, // 0x16: End Vertical Blanking
    0xE3, // 0x17: CRTC Mode Control (Byte Mode)
    0xFF  // 0x18: Line Compare
};

// 3. Graphics Controller (Регистры 0x00 - 0x08)
static const uint8_t vga_mode12h_gc[] = {
    0x00, // 0x00: Set/Reset
    0x00, // 0x01: Enable Set/Reset
    0x00, // 0x02: Color Compare
    0x00, // 0x03: Data Rotate
    0x00, // 0x04: Read Map Select (Плоскость 0)
    0x00, // 0x05: Graphics Mode Register (Write Mode 0, Read Mode 0)
    0x05, // 0x06: Miscellaneous (Карта 0xA0000 64KB, Графический режим)
    0x0F, // 0x07: Color Don't Care
    0xFF  // 0x08: Bit Mask (Все биты разрешены)
};

// 4. Attribute Controller (Регистры 0x00 - 0x14)
static const uint8_t vga_mode12h_attr[] = {
    // Палитра 16 цветов (EGA индексы)
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x01, // 0x10: Mode Control (Графика, 16 цветов)
    0x00, // 0x11: Overscan Color (Черная рамка)
    0x0F, // 0x12: Color Plane Enable (Все 4 плоскости активны)
    0x00, // 0x13: Horizontal Pixel Panning
    0x00  // 0x14: Color Select
};

// 5. Стандартная 16-цветная палитра RGB DAC (каждый канал 0..63)
static const uint8_t vga_dac_16colors[16][3] = {
    { 0,  0,  0}, // 0: Черный
    { 0,  0, 42}, // 1: Синий
    { 0, 42,  0}, // 2: Зеленый
    { 0, 42, 42}, // 3: Циан
    {42,  0,  0}, // 4: Красный
    {42,  0, 42}, // 5: Маджента
    {42, 21,  0}, // 6: Коричневый
    {42, 42, 42}, // 7: Светло-серый
    {21, 21, 21}, // 8: Темно-серый
    {21, 21, 63}, // 9: Ярко-синий
    {21, 63, 21}, // 10: Ярко-зеленый
    {21, 63, 63}, // 11: Ярко-циан
    {63, 21, 21}, // 12: Ярко-красный
    {63, 21, 63}, // 13: Ярко-маджента
    {63, 63, 21}, // 14: Желтый
    {63, 63, 63}  // 15: Белый
};

// ====================================================================
// ОСНОВНАЯ ФУНКЦИЯ ПЕРЕКЛЮЧЕНИЯ В 640x480 16 ЦВЕТОВ
// ====================================================================

static inline void userland_set_mode_640x480(void) {
    asm volatile("cli"); // Запрещаем прерывания на момент смены таймингов

    // 1. ОСТАНОВКА СЕКВЕНСЕРА (Синхронный сброс)
    vga_outw(0x3C4, 0x0100);

    // 2. MISCELLANEOUS OUTPUT REGISTER (Порт 0x3C2)
    // 0xE3 = 11100011b:
    // Бит 0: 1 (Эмуляция цвета, CRTC на 0x3D4)
    // Бит 1: 1 (Разрешить доступ к видеоОЗУ)
    // Биты 2-3: 01 (Тактовая частота 25.175 МГц для 640 точек)
    // Биты 6-7: 11 (Отрицательная полярность H-Sync и V-Sync -> 480 строк)
    vga_outb(0x3C2, 0xE3);

    // 3. ЗАПИСЬ СЕКВЕНСЕРА (Sequencer)
    for (uint8_t i = 1; i <= 4; i++) {
        vga_outb(0x3C4, i);
        vga_outb(0x3C5, vga_mode12h_seq[i - 1]);
    }

    // 4. ПЕРЕЗАПУСК СЕКВЕНСЕРА
    vga_outw(0x3C4, 0x0300);

    // 5. РАЗБЛОКИРОВКА ЗАПИСИ В РЕГИСТРЫ CRTC
    // В регистре 0x11 бит 7 по умолчанию защищает регистры 0x00-0x07 от перезаписи
    vga_outb(0x3D4, 0x11);
    uint8_t crtc_protect = vga_inb(0x3D5);
    vga_outb(0x3D5, crtc_protect & 0x7F); // Сбрасываем бит 7

    // 6. ЗАПИСЬ РЕГИСТРОВ CRTC (Тайминги экрана)
    for (uint8_t i = 0; i < 25; i++) {
        vga_outb(0x3D4, i);
        vga_outb(0x3D5, vga_mode12h_crtc[i]);
    }

    // 7. ЗАПИСЬ GRAPHICS CONTROLLER (0x3CE / 0x3CF)
    for (uint8_t i = 0; i < 9; i++) {
        vga_outb(0x3CE, i);
        vga_outb(0x3CF, vga_mode12h_gc[i]);
    }

    // 8. ЗАПИСЬ ATTRIBUTE CONTROLLER (Порт 0x3C0)
    // Перед каждой палитрой читаем статус 0x3DA, чтобы сбросить flip-flop в состояние "индекс"
    for (uint8_t i = 0; i < 21; i++) {
        vga_inb(0x3DA); // Сброс flip-flop
        vga_outb(0x3C0, i);
        vga_outb(0x3C0, vga_mode12h_attr[i]);
    }

    // Включаем видеосигнал: запись 0x20 в порт 0x3C0 (бит 5 PAS = 1)
    vga_inb(0x3DA);
    vga_outb(0x3C0, 0x20);

    // 9. ЗАГРУЗКА 16 ЦВЕТОВ В ЦАП (DAC PALETTE)
    vga_outb(0x3C8, 0x00); // Начинаем с цвета 0
    for (int i = 0; i < 16; i++) {
        vga_outb(0x3C9, vga_dac_16colors[i][0]); // R
        vga_outb(0x3C9, vga_dac_16colors[i][1]); // G
        vga_outb(0x3C9, vga_dac_16colors[i][2]); // B
    }

    // 10. ОЧИСТКА ВИДЕОПАМЯТИ 0xA0000
    // Включаем запись во все 4 плоскости одновременно
    vga_outw(0x3C4, 0x0F02);
    volatile uint8_t* vram = (volatile uint8_t*)0xA0000;
    // 640x480 / 8 = 80 байт на строку. 80 * 480 = 38400 байт на плоскость
    for (int i = 0; i < 38400; i++) {
        vram[i] = 0x00;
    }

    asm volatile("sti"); // Включаем прерывания обратно
}

static const uint8_t vga_text_seq[] = { 0x00, 0x03, 0x00, 0x02 }; // 9-dot char, Odd/Even ON
static const uint8_t vga_text_gc[]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
static const uint8_t vga_text_crtc[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
};
static const uint8_t vga_text_attr[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static inline void userland_set_mode_text(void) {
    asm volatile("cli");

    // 1. Сброс секвенсера
    vga_outw(0x3C4, 0x0100);

    // 2. Miscellaneous Output Register (0x67 для текста 400 строк / 70 Гц)
    vga_outb(0x3C2, 0x67);

    // 3. Sequencer
    for (uint8_t i = 1; i <= 4; i++) {
        vga_outb(0x3C4, i);
        vga_outb(0x3C5, vga_text_seq[i - 1]);
    }
    vga_outw(0x3C4, 0x0300); // Запуск секвенсера

    // 4. Разблокировка CRTC
    vga_outb(0x3D4, 0x11);
    vga_outb(0x3D5, vga_inb(0x3D5) & 0x7F);

    // 5. CRTC
    for (uint8_t i = 0; i < 25; i++) {
        vga_outb(0x3D4, i);
        vga_outb(0x3D5, vga_text_crtc[i]);
    }

    // 6. Graphics Controller (0x0E в рег. 6 переключает карту на 0xB8000)
    for (uint8_t i = 0; i < 9; i++) {
        vga_outb(0x3CE, i);
        vga_outb(0x3CF, vga_text_gc[i]);
    }

    // 7. Attribute Controller
    for (uint8_t i = 0; i < 21; i++) {
        vga_inb(0x3DA);
        vga_outb(0x3C0, i);
        vga_outb(0x3C0, vga_text_attr[i]);
    }
    vga_inb(0x3DA);
    vga_outb(0x3C0, 0x20); // Enable video output

    // 8. Очистка видеопамяти 0xB8000 (пробелы серым цветом 0x07)
    volatile uint16_t* text_vram = (volatile uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        text_vram[i] = 0x0720;
    }

    asm volatile("sti");
}

#endif // VGA_MODE12H_H