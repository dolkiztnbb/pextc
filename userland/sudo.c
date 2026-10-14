#include "photon.h"
#include "ulibc.h"

static uint32_t crc32(const char* str) {
    uint32_t crc = 0xFFFFFFFF;
    while (*str) {
        crc ^= (uint8_t)(*str++);
        for (int i = 0; i < 8; i++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

static uint32_t parse_hex(const char* s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint32_t val = 0;
    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
        else break;
    }
    return val;
}

// Поиск пути к исполняемому файлу
static void resolve_cmd(const char* cmd, char* out_path, uint32_t max_len) {
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '/') { // Если передан путь (/bin/psh или ./psh)
            u_strncpy(out_path, cmd, max_len);
            return;
        }
    }
    // Если передано просто имя (psh), ищем в /bin/
    u_path_join(out_path, "/bin", cmd, max_len);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print("Usage: sudo <command> [args...]\n");
        exit(1);
    }

    print("[sudo] password for user: ");
    set_io_flags(1); // скрыть ввод
    char entered_pass[64];
    int len = 0;
    while (len < 63) {
        char c = getchar_blocking();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') {
            if (len > 0) len--;
            continue;
        }
        entered_pass[len++] = c;
    }
    entered_pass[len] = '\0';
    set_io_flags(0);
    print("\n");

    int fd = open("/sys/passwd", READ);
    if (fd < 0) {
        print("sudo: cannot read authentication database\n");
        exit(1);
    }
    char hash_buf[32];
    int bytes = read(fd, hash_buf, sizeof(hash_buf) - 1);
    close(fd);
    if (bytes <= 0) {
        print("sudo: password database is empty\n");
        exit(1);
    }
    hash_buf[bytes] = '\0';

    if (crc32(entered_pass) != parse_hex(hash_buf)) {
        print("sudo: incorrect password. This incident will be reported.\n");
        exit(1);
    }

    // Резолвим имя команды (psh -> /bin/psh)
    char target_path[128];
    resolve_cmd(argv[1], target_path, sizeof(target_path));

    int pid = exec(target_path, &argv[1]);
    if (pid >= 0) {
        int status = 0;
        waitpid(pid, &status);
        exit(status);
    } else {
        print("sudo: failed to execute ");
        print(argv[1]);
        print("\n");
        exit(1);
    }
    return 0;
}