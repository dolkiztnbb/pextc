#ifndef PHOTON_H
#define PHOTON_H

/* ============================================================
 *               ТИПЫ ДАННЫХ (БЕЗ STDLIB / NOSTDINC)
 * ============================================================ */
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;

typedef uint32_t           size_t;
typedef int32_t            intptr_t;
typedef uint32_t           uintptr_t;

#define NULL ((void*)0)

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

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* ============================================================
 *               НОМЕРА СИСТЕМНЫХ ВЫЗОВОВ
 * ============================================================ */
#define SYS_OPEN         0x01
#define SYS_READ         0x02
#define SYS_WRITE        0x03
#define SYS_CLOSE        0x04
#define SYS_SEEK         0x05
#define SYS_QUERY        0x06
#define SYS_SET_IO_COLOR 0x07
#define SYS_READDIR      0x08
#define SYS_MKDIR        0x09
#define SYS_UNLINK       0x0A
#define SYS_STAT         0x0B
#define SYS_CHDIR        0x0C
#define SYS_GETCWD       0x0D
#define SYS_DUP2         0x0E

#define SYS_MALLOC       0x10
#define SYS_FREE         0x11
#define SYS_MSTATUS      0x12
#define SYS_BRK          0x13
#define SYS_MMAP         0x14
#define SYS_MUNMAP       0x15
#define SYS_SHM_CREATE   0x16
#define SYS_SHM_ATTACH   0x17
#define SYS_SHM_DETACH   0x18

#define SYS_ADD_TASK     0x20
#define SYS_KILL_TASK    0x21
#define SYS_EXEC         0x22
#define SYS_EXIT         0x23
#define SYS_WAITPID      0x24
#define SYS_GETPID       0x25
#define SYS_YIELD        0x26
#define SYS_SLEEP        0x27
#define SYS_GETUID       0x28 // Получить UID текущего процесса (0 = root)
#define SYS_REBOOT       0x29 // Аппаратная перезагрузка системы
#define SYS_SETUID       0x2A // Установить UID процесса

#define SYS_SETIOCOL     0x30
#define SYS_SETIOFL      0x31
#define SYS_GOTOXY       0x32
#define SYS_GET_CURSOR   0x33
#define SYS_POLL_KEY     0x34
#define SYS_CLEAR_SCR    0x35
#define SYS_GET_SCR_SIZE 0x36
#define SYS_GET_MOUSE_DELTA  0x37

#define SYS_GET_TICKS    0x40
#define SYS_GET_TIME     0x41
#define SYS_GET_DATE     0x42

#define SYS_PIPE         0x50
#define SYS_SIGNAL       0x51

/* ============================================================
 *               СТРУКТУРЫ ДАННЫХ VFS (БЕЗ УПАКОВКИ!)
 * ============================================================ */
typedef struct {
    char name[32];
    uint8_t type;
    uint32_t size;
} dirent_t;

typedef struct {
    uint8_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t change_time;
} stat_t;

/* ============================================================
 *               СТРУКТУРЫ ДАННЫХ МЫШКИ
 * ============================================================ */
typedef struct {
    int32_t dx;
    int32_t dy;
    uint8_t buttons; // бит 0: Left, бит 1: Right, бит 2: Middle
} mouse_state_t;

/* ============================================================
 *               ПРОТОТИПЫ ФУНКЦИЙ ЮЗЕРСПЕЙСА
 * ============================================================ */

int32_t open(const char* path, char flags);
int32_t read(int32_t fd, void* buffer, uint32_t size);
int32_t write(int32_t fd, const void* data, uint32_t size);
int32_t close(int32_t fd);
int32_t seek(int32_t fd, int32_t offset, int whence);
int32_t readdir(int32_t fd, uint32_t index, dirent_t* entry);
int32_t mkdir(const char* path);
int32_t unlink(const char* path);
int32_t stat(const char* path, stat_t* st);
int32_t chdir(const char* path);
int32_t getcwd(char* buffer, uint32_t size);
int32_t dup2(int32_t oldfd, int32_t newfd);

int32_t getuid(void);
int32_t reboot(void);

void    putchar(char c);
void    print(const char* str);
void    printn(const char* str);
void    clear_screen(uint8_t color);
void    gotoxy(int x, int y);
void    get_cursor(int* x, int* y);
void    get_screen_size(int* cols, int* rows);
char    poll_key(void);
char    getchar_blocking(void);
void    set_io_color(uint8_t in_bg, uint8_t in_fg, uint8_t out_bg, uint8_t out_fg);
void    set_io_flags(uint8_t flags);

void*   malloc(uint32_t size);
void    free(void* ptr);
void*   sbrk(intptr_t increment);
int32_t shm_create(uint32_t size);
void*   shm_attach(int32_t shmid);
int32_t shm_detach(int32_t shmid);
int32_t get_mouse(mouse_state_t* state);

int32_t exec(const char* path, char* const argv[]);
void    exit(int32_t status) __attribute__((noreturn));
int32_t waitpid(int32_t pid, int32_t* status);
int32_t getpid(void);
void    yield(void);
void    sleep(uint32_t ms);
int32_t setuid(int32_t uid);

uint32_t get_ticks(void);
void     get_time(uint8_t* h, uint8_t* m, uint8_t* s);
void     get_date(uint8_t* d, uint8_t* m, uint16_t* y);

int32_t pipe(int32_t pipefd[2]);
int32_t send_signal(int32_t pid, int32_t sig);

#endif