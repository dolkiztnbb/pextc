#ifndef __MAIN_H__
#define __MAIN_H__

#include "../photon.h"
#include "defs.h"
#include "mem.h"
#include "ui.h"
#include "tracker.h"
#include "instrument_ui.h"
#include "pattern_ui.h" // <-- Подключаем наш секвенсор!

song_t* current_song;
tracker_context_t* ctx;

void startapp() {
    current_song = song_create("AdLib Anthem", 16, 32, 32);
    ctx = tracker_context_create();

    if (!current_song || !ctx) {
        print("Out of memory!\n");
        exit(1);
    }

    adlib_silence();
    ctx->current_tab = TAB_PATTERN; // По умолчанию открываем секвенсор (Скрин 2)

    clear_screen(0x07);
    draw_pattern_editor();

    int needs_redraw = 0;
    uint32_t last_row_tick = get_ticks();

    while (1) {
        // 1. Опрос клавиатуры
        char c = poll_key();
        if (c != 0) {
            if (c == '`') break;

            // Переключение вкладок по TAB
            if (c == '\t') {
                ctx->current_tab = (ctx->current_tab == TAB_PATTERN) 
                                   ? TAB_INSTRUMENT : TAB_PATTERN;
                clear_screen(0x07);
                needs_redraw = 1;
            }
            // Пробел — запуск/остановка воспроизведения
            else if (c == ' ') {
                ctx->is_playing = !ctx->is_playing;
                if (!ctx->is_playing) {
                    ctx->play_row = 0;
                    for (int ch = 0; ch < 9; ch++) opl_channel_note_off(ch);
                } else {
                    last_row_tick = get_ticks();
                }
                needs_redraw = 1;
            }
            else {
                // Передаем ввод в активную вкладку
                if (ctx->current_tab == TAB_PATTERN) {
                    handle_pattern_input(c);
                } else if (ctx->current_tab == TAB_INSTRUMENT) {
                    handle_instrument_input(c);
                }
                needs_redraw = 1;
            }
        }

        // 2. Отслеживание отпускания клавиши '\' в редакторе инструмента
        audition_note_update();

        // 3. Автовоспроизведение секвенсора (Play Engine)
        if (ctx->is_playing) {
            uint32_t now = get_ticks();
            // Расчет длительности тика на основе Speed и BPM:
            uint32_t ms_per_row = (60000 / current_song->bpm) / 4; 

            if (now - last_row_tick >= ms_per_row) {
                last_row_tick = now;

                pattern_t* pat = &current_song->patterns[0];
                for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
                    pattern_note* n = &pat->rows[ctx->play_row][ch];
                    if (n->note == NOTE_OFF) {
                        opl_channel_note_off(ch);
                    } else if (n->note >= 1 && n->note <= 12) {
                        apply_instrument_to_channel(&current_song->instruments[n->instrument - 1], ch);
                        opl_channel_note_on(ch, n->note, n->octave);
                    }
                }

                // Переход к следующей строке
                ctx->play_row = (ctx->play_row + 1) % DEFAULT_PATLEN;
                ctx->cur_row = ctx->play_row;
                needs_redraw = 1;
            }
        }

        // 4. Отрисовка активного экрана
        if (needs_redraw) {
            if (ctx->current_tab == TAB_PATTERN) {
                draw_pattern_editor();
            } else if (ctx->current_tab == TAB_INSTRUMENT) {
                draw_instrument_editor();
            }
            needs_redraw = 0;
        }

        yield();
    }

    // Выключение звука при выходе
    for (int ch = 0; ch < 9; ch++) opl_channel_note_off(ch);
    adlib_silence();

    tracker_context_free(ctx);
    song_free(current_song, 16);
    clear_screen(0x07);
}

#endif