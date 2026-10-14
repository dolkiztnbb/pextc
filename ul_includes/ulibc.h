#ifndef ULIBC_H
#define ULIBC_H

typedef __builtin_va_list   va_list;
#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_copy(dest, src)  __builtin_va_copy(dest, src)z

#include "photon.h"

// ==========================================
// СТРОКИ И ПАМЯТЬ
// ==========================================
void  u_memset(void* ptr, int val, uint32_t size);
void  u_memcpy(void* dest, const void* src, uint32_t size);
void  u_strncpy(char* dest, const char* src, uint32_t max_len);
int   u_strlen(const char* s);
int   u_strcmp(const char* a, const char* b);
int   u_strncmp(const char* a, const char* b, uint32_t n);
int   vsnprintf(char* buf, uint32_t size, const char* fmt, va_list args);
int   snprintf(char* buf, uint32_t size, const char* fmt, ...);
int   printf(const char* fmt, ...);

// ==========================================
// ФОРМАТИРОВАНИЕ И ФАЙЛОВЫЕ ПУТИ
// ==========================================
void  u_format_num(uint32_t val, char* buf, uint32_t buf_size);
void  u_path_join(char* dest, const char* path1, const char* path2, uint32_t max_len);

// ==========================================
// ПЕРЕМЕННЫЕ ОКРУЖЕНИЯ (ENVIRONMENT)
// ==========================================
void  u_setenv(const char* key, const char* val);
const char* u_getenv(const char* key);

#endif // ULIBC_H