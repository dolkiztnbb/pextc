#include "photon.h"

static inline int32_t syscall0(uint32_t num) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int32_t syscall1(uint32_t num, uint32_t a1) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory");
    return ret;
}

static inline int32_t syscall2(uint32_t num, uint32_t a1, uint32_t a2) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static inline int32_t syscall4(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4) : "memory");
    return ret;
}

static inline int32_t syscall5(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    int32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "memory");
    return ret;
}

/* Files */
int32_t open(const char* path, char flags) { return syscall2(SYS_OPEN, (uint32_t)path, (uint32_t)flags); }
int32_t read(int32_t fd, void* buffer, uint32_t size) { return syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buffer, size); }
int32_t write(int32_t fd, const void* data, uint32_t size) { return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)data, size); }
int32_t close(int32_t fd) { return syscall1(SYS_CLOSE, (uint32_t)fd); }
int32_t seek(int32_t fd, int32_t offset, int whence) { return syscall3(SYS_SEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence); }
int32_t readdir(int32_t fd, uint32_t index, dirent_t* entry) { return syscall3(SYS_READDIR, (uint32_t)fd, index, (uint32_t)entry); }
int32_t mkdir(const char* path) { return syscall1(SYS_MKDIR, (uint32_t)path); }
int32_t unlink(const char* path) { return syscall1(SYS_UNLINK, (uint32_t)path); }
int32_t stat(const char* path, stat_t* st) { return syscall2(SYS_STAT, (uint32_t)path, (uint32_t)st); }
int32_t chdir(const char* path) { return syscall1(SYS_CHDIR, (uint32_t)path); }
int32_t getcwd(char* buffer, uint32_t size) { return syscall2(SYS_GETCWD, (uint32_t)buffer, size); }
int32_t dup2(int32_t oldfd, int32_t newfd) { return syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd); }
int32_t setuid(int32_t uid) {    return syscall1(SYS_SETUID, (uint32_t)uid); }
int32_t shm_create(uint32_t size) { return syscall1(SYS_SHM_CREATE, size); }
void*   shm_attach(int32_t shmid) { return (void*)syscall1(SYS_SHM_ATTACH, (uint32_t)shmid); }
int32_t shm_detach(int32_t shmid) { return syscall1(SYS_SHM_DETACH, (uint32_t)shmid); }
int32_t get_mouse(mouse_state_t* state) {
    return syscall1(SYS_GET_MOUSE_DELTA, (uint32_t)state);
}

/* Screen & Keyboard */
void putchar(char c) { write(STDOUT, &c, 1); }
void print(const char* str) {
    if (!str) return;
    uint32_t len = 0;
    while (str[len]) len++;
    write(STDOUT, str, len);
}
void printn(const char* str) { print(str); putchar('\n'); }
void clear_screen(uint8_t color) { syscall1(SYS_CLEAR_SCR, color); }
void gotoxy(int x, int y) { syscall2(SYS_GOTOXY, x, y); }
void get_cursor(int* x, int* y) { syscall2(SYS_GET_CURSOR, (uint32_t)x, (uint32_t)y); }
void get_screen_size(int* cols, int* rows) { syscall2(SYS_GET_SCR_SIZE, (uint32_t)cols, (uint32_t)rows); }
char poll_key(void) { return (char)syscall0(SYS_POLL_KEY); }
char getchar_blocking(void) {
    char c = 0;
    while ((c = poll_key()) == 0) {
        yield();
    }
    return c;
}
void set_io_color(uint8_t in_bg, uint8_t in_fg, uint8_t out_bg, uint8_t out_fg) {
    syscall4(SYS_SETIOCOL, in_bg, in_fg, out_bg, out_fg);
}
void set_io_flags(uint8_t flags) { syscall1(SYS_SETIOFL, flags); }

int32_t getuid(void) { 
    return syscall0(SYS_GETUID); 
}

int32_t reboot(void) { 
    return syscall0(SYS_REBOOT); 
}

uint32_t malloced=0;
/* Memory */
void* malloc(uint32_t size) {
    return (void*)syscall1(SYS_MALLOC, size);
}
void free(void* ptr) { syscall1(SYS_FREE, (uint32_t)ptr); }
void* sbrk(intptr_t increment) {
    void* cur_brk = (void*)syscall1(SYS_BRK, 0);
    if (increment == 0) return cur_brk;
    void* new_brk = (void*)((uintptr_t)cur_brk + increment);
    void* res = (void*)syscall1(SYS_BRK, (uint32_t)new_brk);
    return (res == (void*)-1) ? (void*)-1 : cur_brk;
}

/* Process */
int32_t exec(const char* path, char* const argv[]) {
    return syscall2(SYS_EXEC, (uint32_t)path, (uint32_t)argv); 
}
void exit(int32_t status) {
    syscall1(SYS_EXIT, (uint32_t)status);
    while (1) asm volatile("hlt");
}
int32_t waitpid(int32_t pid, int32_t* status) { return syscall2(SYS_WAITPID, (uint32_t)pid, (uint32_t)status); }
int32_t getpid(void) { return syscall0(SYS_GETPID); }
void yield(void) { syscall0(SYS_YIELD); }
void sleep(uint32_t ms) { syscall1(SYS_SLEEP, ms); }

/* Time */
uint32_t get_ticks(void) { return (uint32_t)syscall0(SYS_GET_TICKS); }
void get_time(uint8_t* h, uint8_t* m, uint8_t* s) { syscall3(SYS_GET_TIME, (uint32_t)h, (uint32_t)m, (uint32_t)s); }
void get_date(uint8_t* d, uint8_t* m, uint16_t* y) { syscall3(SYS_GET_DATE, (uint32_t)d, (uint32_t)m, (uint32_t)y); }

/* IPC */
int32_t pipe(int32_t pipefd[2]) { return syscall1(SYS_PIPE, (uint32_t)pipefd); }
int32_t send_signal(int32_t pid, int32_t sig) { return syscall2(SYS_SIGNAL, (uint32_t)pid, (uint32_t)sig); }