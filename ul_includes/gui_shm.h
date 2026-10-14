#ifndef GUI_SHM_H
#define GUI_SHM_H

#include "photon.h"

#define MAX_WIN_EVENTS  16

// Специальные коды клавиш
#define KEY_UP          ((char)0x80)
#define KEY_DOWN        ((char)0x81)
#define KEY_LEFT        ((char)0x82)
#define KEY_RIGHT       ((char)0x83)
#define KEY_WIN         ((char)0x84)

// Цвета VGA 16
#define C_BLACK         0x00
#define C_BLUE          0x01
#define C_GREEN         0x02
#define C_CYAN          0x03
#define C_RED           0x04
#define C_MAGENTA       0x05
#define C_BROWN         0x06
#define C_LIGHTGRAY     0x07
#define C_DARKGRAY      0x08
#define C_LIGHTBLUE     0x09
#define C_LIGHTGREEN    0x0A
#define C_LIGHTCYAN     0x0B
#define C_LIGHTRED      0x0C
#define C_LIGHTMAGENTA  0x0D
#define C_YELLOW        0x0E
#define C_WHITE         0x0F

// Типы событий окна
#define EVT_NONE        0
#define EVT_CLICK       1
#define EVT_CLOSE       2
#define EVT_KEY         3

typedef struct {
    uint8_t type;
    int16_t x, y;
    uint8_t btn; // Для EVT_CLICK: кнопка мыши; для EVT_KEY: код нажатой клавиши
} shm_event_t;

// Заголовок, лежащий в самом начале разделяемого сегмента
typedef struct {
    uint32_t pid;
    int32_t  shmid;
    int32_t  x, y;          // Координаты окна на экране
    int32_t  w, h;          // Размеры клиентской области
    char     title[32];
    volatile uint8_t active;
    volatile uint8_t is_dirty; // 1 = клиент перерисовал кадр
    volatile uint8_t closed;   // 1 = запрос на закрытие от WM
    volatile uint8_t evt_head;
    volatile uint8_t evt_tail;
    shm_event_t events[MAX_WIN_EVENTS];
} window_header_t;

#endif
