#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <termios.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>

/* Структуры объявляем прямо здесь во избежание конфликтов макросов */
#include <stdint.h>
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

// Флаги ОС
#define PHT_READ     (1 << 0)
#define PHT_WRITE    (1 << 1)
#define PHT_CREATE   (1 << 5)
#define PHT_TRUNCATE (1 << 6)
#define PHT_APPEND   (1 << 2)

// ==========================================
// MAPPING DESCRIPTORS
// ==========================================
static int map_fd(int pht_fd) {
    if (pht_fd == 0) return STDOUT_FILENO; // OS STDOUT -> Linux 1
    if (pht_fd == 1) return STDIN_FILENO;  // OS STDIN  -> Linux 0
    if (pht_fd == 2) return STDERR_FILENO; // OS STDERR -> Linux 2
    return pht_fd;
}

// ==========================================
// TERMINAL RAW MODE
// ==========================================
static struct termios orig_term;
static int raw_mode_enabled = 0;

static void disable_raw_mode(void) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
        raw_mode_enabled = 0;
    }
}

static void enable_raw_mode(void) {
    if (!raw_mode_enabled) {
        tcgetattr(STDIN_FILENO, &orig_term);
        atexit(disable_raw_mode);
        struct termios raw = orig_term;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_mode_enabled = 1;
    }
}

// ==========================================
// VFS / FILES
// ==========================================
int32_t pht_open(const char* path, char flags) {
    int lflags = 0;
    if ((flags & PHT_READ) && (flags & PHT_WRITE)) lflags |= O_RDWR;
    else if (flags & PHT_WRITE) lflags |= O_WRONLY;
    else lflags |= O_RDONLY;
    
    if (flags & PHT_CREATE) lflags |= O_CREAT;
    if (flags & PHT_TRUNCATE) lflags |= O_TRUNC;
    if (flags & PHT_APPEND) lflags |= O_APPEND;

    return open(path, lflags, 0666);
}

int32_t pht_read(int32_t fd, void* buffer, uint32_t size) {
    int lfd = map_fd(fd);
    if (lfd == STDIN_FILENO) enable_raw_mode();
    return read(lfd, buffer, size);
}

// ==========================================
// MAGIC DISPLAY (CP437 -> UTF8)
// ==========================================
void pht_putchar(char c) {
    unsigned char uc = (unsigned char)c;
    const char* utf8 = NULL;
    switch(uc) {
        case 0xC9: utf8 = "\xE2\x95\x94"; break; // ╔
        case 0xDA: utf8 = "\xE2\x94\x8C"; break; // ┌
        case 0xBB: utf8 = "\xE2\x95\x97"; break; // ╗
        case 0xBF: utf8 = "\xE2\x94\x90"; break; // ┐
        case 0xC8: utf8 = "\xE2\x95\x9A"; break; // ╚
        case 0xC0: utf8 = "\xE2\x94\x94"; break; // └
        case 0xBC: utf8 = "\xE2\x95\x9D"; break; // ╝
        case 0xD9: utf8 = "\xE2\x94\x98"; break; // ┘
        case 0xCD: utf8 = "\xE2\x95\x90"; break; // ═
        case 0xC4: utf8 = "\xE2\x94\x80"; break; // ─
        case 0xBA: utf8 = "\xE2\x95\x91"; break; // ║
        case 0xB3: utf8 = "\xE2\x94\x82"; break; // │
    }
    if (utf8) fputs(utf8, stdout);
    else putchar(c);
    fflush(stdout);
}

int32_t pht_write(int32_t fd, const void* data, uint32_t size) {
    int lfd = map_fd(fd);
    if (lfd == STDOUT_FILENO) {
        const char* str = (const char*)data;
        for (uint32_t i = 0; i < size; i++) pht_putchar(str[i]);
        return size;
    }
    return write(lfd, data, size);
}

int32_t pht_close(int32_t fd) {
    return close(map_fd(fd));
}

int32_t pht_seek(int32_t fd, int32_t offset, int whence) {
    return lseek(map_fd(fd), offset, whence);
}

int32_t pht_readdir(int32_t fd, uint32_t index, pht_dirent_t* entry) {
    int new_fd = dup(map_fd(fd));
    if (new_fd < 0) return -1;
    DIR* dir = fdopendir(new_fd);
    if (!dir) { close(new_fd); return -1; }
    
    struct dirent* de;
    uint32_t current = 0;
    int32_t ret = -1;
    
    while ((de = readdir(dir)) != NULL) {
        if (current == index) {
            strncpy(entry->name, de->d_name, 31);
            entry->name[31] = '\0';
            entry->type = (de->d_type == DT_DIR) ? 1 : 0; // 1 = TYPE_DIR
            
            struct stat st;
            if (fstatat(map_fd(fd), de->d_name, &st, 0) == 0) entry->size = st.st_size;
            else entry->size = 0;
            
            ret = 0;
            break;
        }
        current++;
    }
    closedir(dir);
    return ret;
}

int32_t pht_mkdir(const char* path) { return mkdir(path, 0755); }
int32_t pht_unlink(const char* path) { return (rmdir(path) == 0) ? 0 : unlink(path); }

int32_t pht_stat(const char* path, pht_stat_t* st) {
    struct stat lstat;
    if (stat(path, &lstat) < 0) return -1;
    st->type = S_ISDIR(lstat.st_mode) ? 1 : 0;
    st->size = lstat.st_size;
    st->permissions = lstat.st_mode & 0777;
    st->change_time = lstat.st_mtime;
    return 0;
}

int32_t pht_chdir(const char* path) { return chdir(path); }
int32_t pht_getcwd(char* buffer, uint32_t size) { return getcwd(buffer, size) ? 0 : -1; }
int32_t pht_dup2(int32_t oldfd, int32_t newfd) { return dup2(map_fd(oldfd), map_fd(newfd)); }

// ==========================================
// CONSOLE
// ==========================================
void pht_print(const char* str) {
    if (!str) return;
    while (*str) pht_putchar(*str++);
}
void pht_printn(const char* str) { pht_print(str); pht_putchar('\n'); }
void pht_clear_screen(uint8_t color) { printf("\033[0m\033[2J\033[H"); fflush(stdout); }
void pht_gotoxy(int x, int y) { printf("\033[%d;%dH", y + 1, x + 1); fflush(stdout); }
void pht_get_cursor(int* x, int* y) { *x = 0; *y = 0; }
void pht_get_screen_size(int* cols, int* rows) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) { *cols = w.ws_col; *rows = w.ws_row; }
    else { *cols = 80; *rows = 24; }
}

void pht_set_io_flags(uint8_t flags) { enable_raw_mode(); }
void pht_set_io_color(uint8_t in_bg, uint8_t in_fg, uint8_t out_bg, uint8_t out_fg) {}

char pht_poll_key(void) {
    enable_raw_mode();
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) < 0) { }
    return c;
}

char pht_getchar_blocking(void) {
    enable_raw_mode();
    char c = 0;
    while (read(STDIN_FILENO, &c, 1) <= 0) {
        usleep(10000); // 10ms yield
    }
    return c;
}

// ==========================================
// MEMORY / PROCESS / TIME
// ==========================================
void* pht_malloc(uint32_t size) { return malloc(size); }
void  pht_free(void* ptr) { free(ptr); }
void* pht_sbrk(intptr_t increment) { return sbrk(increment); }

int32_t pht_exec(const char* path) { return system(path); }
void    pht_exit(int32_t status) { disable_raw_mode(); exit(status); }
int32_t pht_waitpid(int32_t pid, int32_t* status) { return waitpid(pid, status, 0); }
int32_t pht_getpid(void) { return getpid(); }
void    pht_yield(void) { usleep(1000); }
void    pht_sleep(uint32_t ms) { usleep(ms * 1000); }

uint32_t pht_get_ticks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 100) + (ts.tv_nsec / 10000000);
}

void pht_get_time(uint8_t* h, uint8_t* m, uint8_t* s) {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    if (h) { *h = tm->tm_hour; }
    if (m) { *m = tm->tm_min; }
    if (s) { *s = tm->tm_sec; }
}

void pht_get_date(uint8_t* d, uint8_t* m, uint16_t* y) {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    if (d) { *d = tm->tm_mday; }
    if (m) { *m = tm->tm_mon + 1; }
    if (y) { *y = tm->tm_year + 1900; }
}

int32_t pht_pipe(int32_t pipefd[2]) { return pipe(pipefd); }
int32_t pht_send_signal(int32_t pid, int32_t sig) { return kill(pid, sig); }