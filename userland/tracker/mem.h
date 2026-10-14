#include "photon.h"
#include "ulibc.h"
#include "defs.h"


slider_collection* slider_collection_create(int amount) {
    if (amount <= 0) return NULL;

    slider_collection* sc = (slider_collection*)malloc(sizeof(slider_collection));
    if (!sc) return NULL;

    sc->sliders = (slider*)malloc(sizeof(slider) * amount);
    if (!sc->sliders) {
        free(sc);
        return NULL;
    }

    sc->amount = amount;
    sc->selected = 0;

    // Инициализация дефолтными значениями
    for (int i = 0; i < amount; i++) {
        sc->sliders[i].min_value = 0;
        sc->sliders[i].dialed_in = 0;
        sc->sliders[i].max_value = 15;
    }

    return sc;
}

void slider_collection_free(slider_collection* sc) {
    if (!sc) return;
    if (sc->sliders) {
        free(sc->sliders);
        sc->sliders = NULL;
    }
    free(sc);
}

button* button_create(int size, const char* sign, int state) {
    button* b = (button*)malloc(sizeof(button));
    if (!b) return NULL;

    b->size = size;
    b->state = state;
    b->sign = NULL;

    if (sign) {
        int len = u_strlen(sign);
        b->sign = (char*)malloc(len + 1);
        if (b->sign) {
            u_strncpy(b->sign, sign, len + 1);
        }
    }

    return b;
}

void button_free(button* b) {
    if (!b) return;
    if (b->sign) {
        free(b->sign);
        b->sign = NULL;
    }
    free(b);
}

// Инициализация существующего паттерна
int pattern_init(pattern_t* pat, int length) {
    if (!pat) return 0;
    if (length <= 0) length = DEFAULT_PATLEN;

    pat->length = length;
    // 1. Выделяем массив указателей на строки
    pat->rows = (pattern_note**)malloc(sizeof(pattern_note*) * length);
    if (!pat->rows) return 0;

    // 2. Выделяем каждую строку на 9 каналов
    for (int r = 0; r < length; r++) {
        pat->rows[r] = (pattern_note*)malloc(sizeof(pattern_note) * CHANNELS_COUNT);
        if (!pat->rows[r]) {
            // Откат назад при нехватке памяти (Rollback)
            for (int j = 0; j < r; j++) free(pat->rows[j]);
            free(pat->rows);
            pat->rows = NULL;
            return 0;
        }

        // Заполняем строку пустыми нотами по умолчанию
        for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
            pat->rows[r][ch].note = NOTE_EMPTY;
            pat->rows[r][ch].detune = 0;
            pat->rows[r][ch].octave = 0;
            pat->rows[r][ch].instrument = 0;
            pat->rows[r][ch].command = 0;
        }
    }

    return 1;
}

// Очистка содержимого паттерна
void pattern_deinit(pattern_t* pat) {
    if (!pat || !pat->rows) return;

    for (int r = 0; r < pat->length; r++) {
        if (pat->rows[r]) {
            free(pat->rows[r]);
            pat->rows[r] = NULL;
        }
    }
    free(pat->rows);
    pat->rows = NULL;
    pat->length = 0;
}

// Создание паттерна на куче целиком
pattern_t* pattern_create(int length) {
    pattern_t* pat = (pattern_t*)malloc(sizeof(pattern_t));
    if (!pat) return NULL;

    if (!pattern_init(pat, length)) {
        free(pat);
        return NULL;
    }

    return pat;
}

void pattern_free(pattern_t* pat) {
    if (!pat) return;
    pattern_deinit(pat);
    free(pat);
}

int song_orders_init(song_orders_t* orders, int order_count) {
    if (!orders) return 0;
    if (order_count <= 0) order_count = MAX_ORDERS;

    orders->order_count = order_count;
    orders->matrix = (uint8_t**)malloc(sizeof(uint8_t*) * order_count);
    if (!orders->matrix) return 0;

    for (int i = 0; i < order_count; i++) {
        orders->matrix[i] = (uint8_t*)malloc(sizeof(uint8_t) * CHANNELS_COUNT);
        if (!orders->matrix[i]) {
            for (int j = 0; j < i; j++) free(orders->matrix[j]);
            free(orders->matrix);
            orders->matrix = NULL;
            return 0;
        }

        // По умолчанию все каналы играют 0-й паттерн
        for (int ch = 0; ch < CHANNELS_COUNT; ch++) {
            orders->matrix[i][ch] = 0;
        }
    }

    return 1;
}

void song_orders_deinit(song_orders_t* orders) {
    if (!orders || !orders->matrix) return;

    for (int i = 0; i < orders->order_count; i++) {
        if (orders->matrix[i]) {
            free(orders->matrix[i]);
            orders->matrix[i] = NULL;
        }
    }
    free(orders->matrix);
    orders->matrix = NULL;
    orders->order_count = 0;
}

// Сброс параметров инструмента в стандартный пресет (например, Lead Synth)
void instrument_init_default(instrument_t* ins, int id, const char* name) {
    if (!ins) return;
    u_memset(ins, 0, sizeof(instrument_t));

    if (name) u_strncpy(ins->name, name, sizeof(ins->name));
    else      u_strncpy(ins->name, "Default FM", sizeof(ins->name));

    // Модулятор (Mod1)
    ins->mod.attack = 15;
    ins->mod.decay = 4;
    ins->mod.sustain = 2;
    ins->mod.release = 4;
    ins->mod.mult = 1;
    ins->mod.ksl = 0;
    ins->mod.total_level = 16;
    ins->mod.waveform = 0; // Sine

    // Несущая (Car / Mod2)
    ins->car.attack = 15;
    ins->car.decay = 6;
    ins->car.sustain = 3;
    ins->car.release = 5;
    ins->car.mult = 1;
    ins->car.ksl = 0;
    ins->car.total_level = 0; // Макс громкость
    ins->car.waveform = 0; // Sine

    ins->feedback = 2;
    ins->connection = 0; // FM-режим
}

song_t* song_create(const char* title, int pattern_count, int pattern_len, int order_count) {
    if (pattern_count <= 0) pattern_count = 16;
    if (pattern_len <= 0)   pattern_len = DEFAULT_PATLEN;
    if (order_count <= 0)   order_count = MAX_ORDERS;


    song_t* s = (song_t*)malloc(sizeof(song_t));
    if (!s) return NULL;

    u_memset(s, 0, sizeof(song_t));

    // 1. Метаданные и настройки
    if (title) u_strncpy(s->title, title, sizeof(s->title));
    else       u_strncpy(s->title, "New Song", sizeof(s->title));

    s->bpm = 150;
    s->speed = 6;
    s->step = 2;
    s->octave = 5;
    s->divis = 1;

    // 2. Выделяем банк инструментов
    s->instruments = (instrument_t*)malloc(sizeof(instrument_t) * MAX_INSTRUMENTS);
    if (!s->instruments) {
        free(s);
        return NULL;
    }
    for (int i = 0; i < MAX_INSTRUMENTS; i++) {
        instrument_init_default(&s->instruments[i], i, "FM Voice");
    }

    // 3. Выделяем пул паттернов
    s->patterns = (pattern_t*)malloc(sizeof(pattern_t) * pattern_count);
    if (!s->patterns) {
        free(s->instruments);
        free(s);
        return NULL;
    }

    for (int i = 0; i < pattern_count; i++) {
        if (!pattern_init(&s->patterns[i], pattern_len)) {
            // Откат назад
            for (int j = 0; j < i; j++) pattern_deinit(&s->patterns[j]);
            free(s->patterns);
            free(s->instruments);
            free(s);
            return NULL;
        }
    }

    // 4. Выделяем матрицу порядка (Order Matrix)
    if (!song_orders_init(&s->orders, order_count)) {
        for (int i = 0; i < pattern_count; i++) pattern_deinit(&s->patterns[i]);
        free(s->patterns);
        free(s->instruments);
        free(s);
        return NULL;
    }

    return s;
}

void song_free(song_t* s, int pattern_count) {
    if (!s) return;

    // 1. Освобождаем матрицу порядка
    song_orders_deinit(&s->orders);

    // 2. Освобождаем каждый паттерн пула
    if (s->patterns) {
        for (int i = 0; i < pattern_count; i++) {
            pattern_deinit(&s->patterns[i]);
        }
        free(s->patterns);
        s->patterns = NULL;
    }

    // 3. Освобождаем инструменты
    if (s->instruments) {
        free(s->instruments);
        s->instruments = NULL;
    }

    // 4. Освобождаем сам объект
    free(s);
}

tracker_context_t* tracker_context_create(void) {
    tracker_context_t* ctx = (tracker_context_t*)malloc(sizeof(tracker_context_t));
    if (!ctx) return NULL;

    u_memset(ctx, 0, sizeof(tracker_context_t));

    ctx->current_tab = TAB_PATTERN;
    ctx->cur_row = 0;
    ctx->cur_channel = 0;
    ctx->cur_field = 0;
    ctx->scroll_row = 0;

    ctx->cur_operator = 0;
    ctx->cur_slider = 0;
    ctx->selected_ins = 0;

    ctx->allow_write = 1;
    ctx->is_playing = 0;
    ctx->play_row = 0;
    ctx->play_order = 0;
    ctx->last_tick = 0;

    return ctx;
}

void tracker_context_free(tracker_context_t* ctx) {
    if (!ctx) return;
    free(ctx);
}