#ifndef LCL_H
#define LCL_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 *               СТАНДАРТНЫЕ ДЕСКРИПТОРЫ И ФЛАГИ
 * ============================================================ */
#define STDOUT 0
#define STDIN  1
#define STDERR 2

#define READ     (1 << 0)
#define WRITE    (1 << 1)
#define APPEND   (1 << 2)
#define BINARY   (1 << 3)
#define TEXT     (1 << 4)
#define CREATE   (1 << 5)
#define TRUNCATE (1 << 6)

#define TYPE_FILE 0
#define TYPE_DIR  1

#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* ============================================================
 *               СТРУКТУРЫ ДАННЫХ VFS
 * ============================================================ */
typedef struct {
    char name[32];
    uint8_t type;
    uint32_t size;
} pht_dirent_t;

typedef struct {
    uint8_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t change_time;
} pht_stat_t;

/* ============================================================
 *               ПОДМЕНА ИМЕН НА LCL-ВЕРСИИ
 * ============================================================ */
#define dirent_t         pht_dirent_t
#define stat_t           pht_stat_t

#define open             pht_open
#define read             pht_read
#define write            pht_write
#define close            pht_close
#define seek             pht_seek
#define readdir          pht_readdir
#define mkdir            pht_mkdir
#define unlink           pht_unlink
#define stat             pht_stat
#define chdir            pht_chdir
#define getcwd           pht_getcwd
#define dup2             pht_dup2

#define putchar          pht_putchar
#define print            pht_print
#define printn           pht_printn
#define clear_screen     pht_clear_screen
#define gotoxy           pht_gotoxy
#define get_cursor       pht_get_cursor
#define get_screen_size  pht_get_screen_size
#define poll_key         pht_poll_key
#define getchar_blocking pht_getchar_blocking
#define set_io_color     pht_set_io_color
#define set_io_flags     pht_set_io_flags

#define malloc           pht_malloc
#define free             pht_free
#define sbrk             pht_sbrk

#define exec             pht_exec
#define exit             pht_exit
#define waitpid          pht_waitpid
#define getpid           pht_getpid
#define yield            pht_yield
#define sleep            pht_sleep

#define get_ticks        pht_get_ticks
#define get_time         pht_get_time
#define get_date         pht_get_date

#define pipe             pht_pipe
#define send_signal      pht_send_signal

/* ============================================================
 *               ПРОТОТИПЫ ФУНКЦИЙ LCL
 * ============================================================ */
int32_t pht_open(const char* path, char flags);
int32_t pht_read(int32_t fd, void* buffer, uint32_t size);
int32_t pht_write(int32_t fd, const void* data, uint32_t size);
int32_t pht_close(int32_t fd);
int32_t pht_seek(int32_t fd, int32_t offset, int whence);
int32_t pht_readdir(int32_t fd, uint32_t index, pht_dirent_t* entry);
int32_t pht_mkdir(const char* path);
int32_t pht_unlink(const char* path);
int32_t pht_stat(const char* path, pht_stat_t* st);
int32_t pht_chdir(const char* path);
int32_t pht_getcwd(char* buffer, uint32_t size);
int32_t pht_dup2(int32_t oldfd, int32_t newfd);

void    pht_putchar(char c);
void    pht_print(const char* str);
void    pht_printn(const char* str);
void    pht_clear_screen(uint8_t color);
void    pht_gotoxy(int x, int y);
void    pht_get_cursor(int* x, int* y);
void    pht_get_screen_size(int* cols, int* rows);
char    pht_poll_key(void);
char    pht_getchar_blocking(void);
void    pht_set_io_color(uint8_t in_bg, uint8_t in_fg, uint8_t out_bg, uint8_t out_fg);
void    pht_set_io_flags(uint8_t flags);

void*   pht_malloc(uint32_t size);
void    pht_free(void* ptr);
void*   pht_sbrk(intptr_t increment);

int32_t pht_exec(const char* path);
void    pht_exit(int32_t status) __attribute__((noreturn));
int32_t pht_waitpid(int32_t pid, int32_t* status);
int32_t pht_getpid(void);
void    pht_yield(void);
void    pht_sleep(uint32_t ms);

uint32_t pht_get_ticks(void);
void     pht_get_time(uint8_t* h, uint8_t* m, uint8_t* s);
void     pht_get_date(uint8_t* d, uint8_t* m, uint16_t* y);

int32_t pht_pipe(int32_t pipefd[2]);
int32_t pht_send_signal(int32_t pid, int32_t sig);

#endif // LCL_H