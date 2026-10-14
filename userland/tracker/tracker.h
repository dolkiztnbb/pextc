#ifndef __TRACKER_H__
#define __TRACKER_H__

#include "defs.h"
#include "ui.h"

#define ADLIB_PORT_REG  0x388
#define ADLIB_PORT_DATA 0x389

static const uint16_t note_freqs[12] = {
    0x16B, // 1: C  (До)
    0x181, // 2: C#
    0x198, // 3: D  (Ре)
    0x1B0, // 4: D#
    0x1CA, // 5: E  (Ми)
    0x1E5, // 6: F  (Фа)
    0x202, // 7: F#
    0x220, // 8: G  (Соль)
    0x241, // 9: G#
    0x263, // 10: A (Ля)
    0x287, // 11: A#
    0x2AE  // 12: B (Си)
};

static const uint8_t opl_mod_offsets[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
static const uint8_t opl_car_offsets[9] = { 0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15 };
static uint8_t channel_b0_states[9] = {0};

static int test_note_playing = 0;
static uint32_t test_note_last_tick = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %b0, %w1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint8_t current_reg_b0 = 0;

// Запись значения в регистр OPL2
static inline void adlib_write(uint8_t reg, uint8_t val) {
    outb(ADLIB_PORT_REG, reg);
    outb(ADLIB_PORT_DATA, val);
}

// Включение ноты на 0-м канале с битом Key-On (бит 5 = 1)
static inline void note_on(uint8_t note, uint8_t octave) {
    if (note < 1 || note > 12) return;
    uint16_t fnum = note_freqs[note - 1];

    adlib_write(0xA0, fnum & 0xFF);
    current_reg_b0 = ((fnum >> 8) & 0x03) | ((octave & 0x07) << 2) | 0x20;
    adlib_write(0xB0, current_reg_b0);
}

// Выключение ноты со сбросом только бита Key-On (бит 5 = 0)
static inline void note_off(void) {
    current_reg_b0 &= ~0x20;
    adlib_write(0xB0, current_reg_b0);
}

// Сброс всех регистров OPL2 (тишина)
static inline void adlib_silence(void) {
    for (int i = 0x01; i <= 0xF5; i++) {
        adlib_write(i, 0);
    }
    current_reg_b0 = 0;
    for (int ch = 0; ch < 9; ch++) channel_b0_states[ch] = 0;
}

// Применение пресета инструмента к выбранному каналу OPL2 (0..8)
static inline void apply_instrument_to_channel(instrument_t* ins, int ch) {
    if (ch < 0 || ch >= 9 || !ins) return;

    uint8_t mod = opl_mod_offsets[ch];
    uint8_t car = opl_car_offsets[ch];

    // Modulator
    adlib_write(0x20 + mod, ins->mod.mult & 0x0F);
    adlib_write(0x40 + mod, ins->mod.total_level & 0x3F);
    adlib_write(0x60 + mod, ((ins->mod.attack & 0x0F) << 4) | (ins->mod.decay & 0x0F));
    adlib_write(0x80 + mod, ((ins->mod.sustain & 0x0F) << 4) | (ins->mod.release & 0x0F));
    adlib_write(0xE0 + mod, ins->mod.waveform & 0x03);

    // Carrier
    adlib_write(0x20 + car, ins->car.mult & 0x0F);
    adlib_write(0x40 + car, ins->car.total_level & 0x3F);
    adlib_write(0x60 + car, ((ins->car.attack & 0x0F) << 4) | (ins->car.decay & 0x0F));
    adlib_write(0x80 + car, ((ins->car.sustain & 0x0F) << 4) | (ins->car.release & 0x0F));
    adlib_write(0xE0 + car, ins->car.waveform & 0x03);

    // Feedback & Connection
    adlib_write(0xC0 + ch, ((ins->feedback & 0x07) << 1) | (ins->connection & 0x01));
}

// Перезапуск ноты на канале OPL2 с гарантированным перезапуском огибающей (ADSR)
static inline void opl_channel_note_on(int ch, uint8_t note, uint8_t octave) {
    if (ch < 0 || ch >= 9 || note < 1 || note > 12) return;
    uint16_t fnum = note_freqs[note - 1];

    // 1. СБРОС: Сначала принудительно сбрасываем Key-On в 0, чтобы сбросить фазу огибающей!
    adlib_write(0xB0 + ch, channel_b0_states[ch] & ~0x20);

    // 2. Записываем младшие 8 бит частоты
    adlib_write(0xA0 + ch, fnum & 0xFF);

    // 3. ТРИГГЕР: Выставляем Key-On = 1. Чип OPL2 мгновенно запускает новую атаку (Attack phase)
    channel_b0_states[ch] = ((fnum >> 8) & 0x03) | ((octave & 0x07) << 2) | 0x20;
    adlib_write(0xB0 + ch, channel_b0_states[ch]);
}

// Срез/отпускание ноты на конкретном канале (0..8)
static inline void opl_channel_note_off(int ch) {
    if (ch < 0 || ch >= 9) return;
    channel_b0_states[ch] &= ~0x20;
    adlib_write(0xB0 + ch, channel_b0_states[ch]);
}

// Тестовое воспроизведение ноты по '\'
static inline void audition_note_press(void) {
    if (!current_song || !current_song->instruments || !ctx) return;

    apply_instrument_to_channel(&current_song->instruments[ctx->selected_ins], 0);

    uint8_t oct = (current_song->octave >= 1 && current_song->octave <= 7) ? current_song->octave : 5;
    if (!test_note_playing) {
        opl_channel_note_on(0, 1, oct); // Нота C (1) на 0 канале
        test_note_playing = 1;
    }
    test_note_last_tick = get_ticks();
}

// Проверка отпускания клавиши '\'
static inline void audition_note_update(void) {
    if (test_note_playing) {
        uint32_t now = get_ticks();
        if (now - test_note_last_tick > 90) {
            opl_channel_note_off(0);
            test_note_playing = 0;
        }
    }
}

#endif // __TRACKER_H__