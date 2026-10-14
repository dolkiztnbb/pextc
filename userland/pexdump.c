#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

#include "ulibc.h"

// Структура PEX заголовка (копия из ядра)
#define PEX_MAGIC 0x71414550
typedef struct __attribute__((packed)) {
    uint32_t magic;          
    uint32_t checksum;       
    uint16_t header_size;    
    uint16_t flags;          
    uint32_t min_kernel_ver; 
    uint32_t entry_point;    
    uint32_t stack_size;     
    uint32_t heap_size;      
    uint32_t text_size;      
    uint32_t data_size;      
    uint32_t bss_size;       
} pex_header_t;

// Вспомогательные функции для печати в Hex (так как в userland пока нет printf)
static void print_hex8(uint8_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    putchar(hex_chars[(val >> 4) & 0x0F]);
    putchar(hex_chars[val & 0x0F]);
}

static void print_hex32(uint32_t val) {
    print_hex8((val >> 24) & 0xFF);
    print_hex8((val >> 16) & 0xFF);
    print_hex8((val >> 8) & 0xFF);
    print_hex8(val & 0xFF);
}

// Функция для красивого дампа памяти
static void hexdump(const uint8_t* data, uint32_t size, uint32_t base_address) {
    for (uint32_t i = 0; i < size; i += 16) {
        print("\033[93m0x");
        print_hex32(base_address + i);
        print(" \033[37m");

        // Вывод HEX
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < size) {
                print_hex8(data[i + j]);
                print(" ");
            } else {
                print("   ");
            }
            if (j == 7) print(" ");
        }

        print(" \033[96m|");
        // Вывод ASCII
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t c = data[i + j];
                if (c >= 32 && c <= 126) putchar(c);
                else putchar('.');
            } else {
                print(" ");
            }
        }
        print("|\033[0m\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print("Usage: pexdump <file.pex>\n");
        exit(1);
    }

    int fd = open(argv[1], READ | BINARY);
    if (fd < 0) {
        print("pexdump: cannot open file\n");
        exit(1);
    }

    pex_header_t hdr;
    if (read(fd, &hdr, sizeof(pex_header_t)) != sizeof(pex_header_t)) {
        print("pexdump: failed to read header\n");
        close(fd);
        exit(1);
    }

    if (hdr.magic != PEX_MAGIC) {
        print("pexdump: invalid magic number (not a PEX executable)\n");
        close(fd);
        exit(1);
    }

    // Вывод информации об исполняемом файле
    print("\033[97;44m=== PEX Executable Info ===\033[0m\n");
    print("File:         "); print(argv[1]); print("\n");
    print("Entry Point:  +0x"); print_hex32(hdr.entry_point); print("\n");
    print("Code (.text): 0x"); print_hex32(hdr.text_size); print(" bytes\n");
    print("Data (.data): 0x"); print_hex32(hdr.data_size); print(" bytes\n");
    print("BSS  (.bss):  0x"); print_hex32(hdr.bss_size); print(" bytes\n");
    print("Stack Size:   0x"); print_hex32(hdr.stack_size); print(" bytes\n");
    print("Heap Size:    0x"); print_hex32(hdr.heap_size); print(" bytes\n\n");

    // Загрузка и дамп секции кода
    uint32_t limit = hdr.text_size;
    if (limit > 64) {
        limit = 64;
    } else {
        print("Hexdump of .text section:\n");
    }

    uint8_t* code_buf = (uint8_t*)malloc(limit);
    if (code_buf) {
        seek(fd, hdr.header_size, SEEK_SET);
        read(fd, code_buf, limit);
        hexdump(code_buf, limit, 0x04000000); // 0x04000000 - PEX_VIRT_BASE
        free(code_buf);
    }

    close(fd);
    return 0;
}