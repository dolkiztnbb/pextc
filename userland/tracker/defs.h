#ifndef __PT_DEF__
#define __PT_DEF__

#define NULL ((void*)0)

#define CHANNELS_COUNT      9   // 9 мелодических каналов OPL2
#define DEFAULT_PATLEN      32  // 32 строки в паттерне
#define MAX_ORDERS          32  // Макс. длина песни (матрица сверху)
#define MAX_INSTRUMENTS     16  // Количество инструментов в банке

#define NOTE_EMPTY          0   // "..."
#define NOTE_OFF            254 // "===" (KeyOff / срез звука)
#define NOTE_HOLD           255 // "---" (продолжение звучания)

// UI

typedef struct {
    int min_value;
    int dialed_in;
    int max_value;
} slider;

typedef struct {
    slider* sliders;
    int amount;
    int selected;
} slider_collection;

typedef struct {
    int size;
    char* sign;
    int state;
} button;

// Tracker stuff

typedef enum {
    TAB_PATTERN = 0,   
    TAB_INSTRUMENT,     
    TAB_MANUAL          
} tracker_tab_t;

typedef struct {
    uint8_t attack;     // At: 0..15
    uint8_t decay;      // Dc: 0..15
    uint8_t sustain;    // Su: 0..15
    uint8_t release;    // Rl: 0..15
    uint8_t mult;       // Mu: 0..15 (множитель частоты)
    uint8_t ksl;        // LS: 0..3  (Key Scale Level)
    uint8_t total_level;// TL: 0..63 (Громкость / глубина модуляции)

    uint8_t tremolo;    // Ampl.Mod (0/1)
    uint8_t vibrato;    // Vibrato  (0/1)
    uint8_t ksr;        // Scale    (0/1)
    uint8_t eg_type;    // Sustain  (0 = затухающий, 1 = органный)

    uint8_t waveform;   // Wf: 0=Sine, 1=Half, 2=Abs, 3=Qrt
} opl_operator_t;

typedef struct {
    char name[24];              // Название инструмента
    opl_operator_t mod;         // Оператор 1 (Mod1)
    opl_operator_t car;         // Оператор 2 (Mod2)
    
    uint8_t feedback;           // FB: 0..7
    uint8_t connection;         // Algr: 0 = FM, 1 = Additive (A-M)
} instrument_t;

typedef struct {
    int note;
    int detune;
    int octave;
    int instrument;
    unsigned int command;
} pattern_note;

typedef struct {
    int length;
    pattern_note **rows;
} pattern_t;

typedef struct {
    int order_count;                                    // Сколько шагов в песне
    uint8_t **matrix;         // Номера паттернов для каждого канала
} song_orders_t;

typedef struct {
    char title[32];
    uint16_t bpm;       // 150 по умолчанию
    uint8_t speed;      // 6 по умолчанию
    uint8_t step;       // Шаг автоспуска курсора (2 со скриншота)
    uint8_t octave;     // Текущая октава для ввода (5 со скриншота)
    uint8_t divis;      // Divisor

    instrument_t *instruments;
    pattern_t *patterns;        // Пул паттернов
    song_orders_t orders;       // Порядок воспроизведения
} song_t;

typedef struct {
    tracker_tab_t current_tab;  // Какой экран открыт (Seq, Ins или Manual)
    
    int cur_row;                // 0..31
    int cur_channel;            // 0..8
    int cur_field;              // 0 = Note, 1 = Instrument, 2 = Command
    int scroll_row;             // Смещение видимой части сетки

    int cur_operator;           // 0 = Mod1, 1 = Mod2
    int cur_slider;             // 0..6 (At..TL)
    int selected_ins;           // Текущий редактируемый инструмент (0..15)

    int allow_write;            // 1 = пробел пишет/вводит, 0 = только превью
    int is_playing;             // Идет ли воспроизведение
    int play_row;               // Текущая воспроизводимая строка
    int play_order;             // Текущий шаг в матрице ордеров
    uint32_t last_tick;         // Тайминг для тиков плеера
} tracker_context_t;

// Глобальное состояние трекера
extern song_t* current_song;
extern tracker_context_t* ctx;

#ifndef KEY_UP
#define KEY_UP    ((char)0x80)
#define KEY_DOWN  ((char)0x81)
#define KEY_LEFT  ((char)0x82)
#define KEY_RIGHT ((char)0x83)
#define BACKSPACE 8
#endif

#endif