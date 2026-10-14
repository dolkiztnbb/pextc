#include "ulibc.h"

// ====================================================================
// УТИЛИТЫ ДЛЯ РАБОТЫ СО СТРОКАМИ И ПАМЯТЬЮ
// ====================================================================

// Отключаем оптимизацию, чтобы предотвратить генерацию SSE инструкций ядром
__attribute__((optimize("O0")))
void u_memset(void* ptr, int val, uint32_t size) {
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = (uint8_t)val;
    }
}

__attribute__((optimize("O0")))
void u_memcpy(void* dest, const void* src, uint32_t size) {
    volatile uint8_t* d = (volatile uint8_t*)dest;
    volatile const uint8_t* s = (volatile const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

__attribute__((optimize("O0")))
void u_strncpy(char* dest, const char* src, uint32_t max_len) {
    if (!dest || !src || max_len == 0) return;
    volatile uint8_t* d = (volatile uint8_t*)dest;
    volatile const uint8_t* s = (volatile const uint8_t*)src;
    uint32_t i = 0;
    while (s[i] && i < max_len - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}

int u_strlen(const char* s) {
    if (!s) return 0;
    int l = 0;
    while (s[l]) l++;
    return l;
}

int u_strcmp(const char* a, const char* b) {
    if (!a || !b) return -1;
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int u_strncmp(const char* a, const char* b, uint32_t n) {
    if (!a || !b) return -1;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] == '\0' && b[i] == '\0') return 0;
        if (a[i] != b[i]) return *(const unsigned char*)(a+i) - *(const unsigned char*)(b+i);
    }
    return 0;
}

// ====================================================================
// УТИЛИТЫ ФОРМАТИРОВАНИЯ И ПУТЕЙ
// ====================================================================

void u_format_num(uint32_t val, char* buf, uint32_t buf_size) {
    char temp[16];
    int i = 0;
    if (val == 0) {
        if (buf_size > 1) { buf[0] = '0'; buf[1] = '\0'; }
        return;
    }
    while (val > 0 && i < 15) {
        temp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0 && j < (int)buf_size - 1) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

void u_path_join(char* dest, const char* path1, const char* path2, uint32_t max_len) {
    if (!dest || max_len == 0) return;
    uint32_t len = 0;
    
    if (path1) {
        while (path1[len] && len < max_len - 1) {
            dest[len] = path1[len];
            len++;
        }
    }
    if (len > 0 && dest[len - 1] != '/' && len < max_len - 1) {
        dest[len++] = '/';
    }
    if (path2) {
        uint32_t j = 0;
        while (path2[j] == '/') j++;
        while (path2[j] && len < max_len - 1) {
            dest[len++] = path2[j++];
        }
    }
    dest[len] = '\0';
}

// ====================================================================
// ТАБЛИЦА ПЕРЕМЕННЫХ ОКРУЖЕНИЯ
// ====================================================================

#define MAX_ENV 32
static struct {
    char key[32];
    char val[128];
    int active;
} env_table[MAX_ENV];

void u_setenv(const char* key, const char* val) {
    for (int i = 0; i < MAX_ENV; i++) {
        if (env_table[i].active && u_strcmp(env_table[i].key, key) == 0) {
            u_strncpy(env_table[i].val, val, 128);
            return;
        }
    }
    for (int i = 0; i < MAX_ENV; i++) {
        if (!env_table[i].active) {
            u_strncpy(env_table[i].key, key, 32);
            u_strncpy(env_table[i].val, val, 128);
            env_table[i].active = 1;
            return;
        }
    }
}

const char* u_getenv(const char* key) {
    for (int i = 0; i < MAX_ENV; i++) {
        if (env_table[i].active && u_strcmp(env_table[i].key, key) == 0) {
            return env_table[i].val;
        }
    }
    return NULL;
}

#include "ulibc.h"

static int uint_to_str(uint32_t val, int base, int uppercase, char *buf) {
    static const char digits_l[] = "0123456789abcdef";
    static const char digits_u[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_u : digits_l;
    char temp[36];
    int i = 0;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (val > 0) {
        temp[i++] = digits[val % base];
        val /= base;
    }

    for (int j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = '\0';
    return i;
}

int vsnprintf(char* buf, uint32_t size, const char* fmt, va_list args) {
    if (!buf || size == 0) return 0;

    uint32_t written = 0;

#define EMIT_CHAR(ch) do { \
    if (written < size - 1) { \
        buf[written] = (ch); \
    } \
    written++; \
} while (0)

    while (*fmt) {
        if (*fmt != '%') {
            EMIT_CHAR(*fmt++);
            continue;
        }

        fmt++; // Пропускаем '%'

        // 1. Флаги выравнивания и заполнения
        int pad_zero = 0;
        int left_align = 0;

        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') pad_zero = 1;
            if (*fmt == '-') left_align = 1;
            fmt++;
        }
        if (left_align) pad_zero = 0; // Выравнивание влево отменяет паддинг нулями

        // 2. Ширина поля (N)
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        char spec = *fmt++;
        if (!spec) break;

        // 3. Обработка спецификаторов
        switch (spec) {
            // СТРОКА: %s и %Ns
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";

                int len = u_strlen(s);
                int padding = (width > len) ? (width - len) : 0;

                // Если выравнивание по правому краю
                if (!left_align) {
                    while (padding-- > 0) EMIT_CHAR(' ');
                }

                while (*s) EMIT_CHAR(*s++);

                // Если выравнивание по левому краю
                if (left_align) {
                    while (padding-- > 0) EMIT_CHAR(' ');
                }
                break;
            }

            // СИМВОЛ: %c
            case 'c': {
                char c = (char)va_arg(args, int);
                int padding = (width > 1) ? (width - 1) : 0;

                if (!left_align) while (padding-- > 0) EMIT_CHAR(' ');
                EMIT_CHAR(c);
                if (left_align) while (padding-- > 0) EMIT_CHAR(' ');
                break;
            }

            // ЭКРАНИРОВАНИЕ ПРОЦЕНТА: %%
            case '%': {
                EMIT_CHAR('%');
                break;
            }

            // ЧИСЛА: %d, %u, %x, %X, %o, %b
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'b': {
                char num_buf[36];
                int is_neg = 0;
                uint32_t uval = 0;
                int base = 10;
                int uppercase = (spec == 'X');

                if (spec == 'd' || spec == 'i') {
                    int32_t d = va_arg(args, int32_t);
                    if (d < 0) {
                        is_neg = 1;
                        uval = (uint32_t)(-d);
                    } else {
                        uval = (uint32_t)d;
                    }
                    base = 10;
                } else {
                    uval = va_arg(args, uint32_t);
                    if (spec == 'u') base = 10;
                    else if (spec == 'x' || spec == 'X') base = 16;
                    else if (spec == 'o') base = 8;
                    else if (spec == 'b') base = 2; // ДВОИЧНЫЙ РЕЖИМ
                }

                int num_len = uint_to_str(uval, base, uppercase, num_buf);
                int total_len = num_len + (is_neg ? 1 : 0);
                int padding = (width > total_len) ? (width - total_len) : 0;

                // Заполнение пробелами справа (если не нули и не выравнивание влево)
                if (!left_align && !pad_zero) {
                    while (padding-- > 0) EMIT_CHAR(' ');
                }

                // Знак минус пишется ПЕРЕД нулями
                if (is_neg) EMIT_CHAR('-');

                // Паддинг нулями (например, %08x или %032b)
                if (!left_align && pad_zero) {
                    while (padding-- > 0) EMIT_CHAR('0');
                }

                // Сами цифры числа
                for (int i = 0; i < num_len; i++) {
                    EMIT_CHAR(num_buf[i]);
                }

                // Паддинг пробелами при выравнивании влево (%-10d)
                if (left_align) {
                    while (padding-- > 0) EMIT_CHAR(' ');
                }
                break;
            }

            default:
                // Неизвестный символ - выводим как есть
                EMIT_CHAR('%');
                EMIT_CHAR(spec);
                break;
        }
    }

    // Завершающий нулевой байт
    if (written < size) {
        buf[written] = '\0';
    } else {
        buf[size - 1] = '\0';
    }

    return written;
#undef EMIT_CHAR
}

int snprintf(char* buf, uint32_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int printf(const char* fmt, ...) {
    char stack_buf[512]; // Буфер на стеке для мгновенного единого вызова write()
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);

    print(stack_buf); // Отправляем в терминал за 1 системный вызов
    return ret;
}