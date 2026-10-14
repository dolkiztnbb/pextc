#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <elf.h>
#include "../header/pex_header.h"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <input.elf> <raw_code.bin> <output.pex> [stack_size] [heap_size] [flags]\n", argv[0]);
        return 1;
    }

    const char* elf_path = argv[1];
    const char* bin_path = argv[2];
    const char* out_path = argv[3];
    uint32_t stack_size  = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0x4000;  // 16 KB default
    uint32_t heap_size   = (argc >= 6) ? (uint32_t)strtoul(argv[5], NULL, 0) : 0x10000; // 64 KB default
    uint16_t flags       = (argc >= 7) ? (uint16_t)strtoul(argv[6], NULL, 0) : 0x0000; // <-- Читаем флаги

    // 1. Открываем ELF для извлечения информации о секциях
    FILE* felf = fopen(elf_path, "rb");
    if (!felf) {
        perror("Failed to open ELF file");
        return 1;
    }

    Elf32_Ehdr ehdr;
    if (fread(&ehdr, 1, sizeof(ehdr), felf) != sizeof(ehdr)) {
        perror("Failed to read ELF header");
        fclose(felf);
        return 1;
    }

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Error: Not a valid 32-bit ELF binary\n");
        fclose(felf);
        return 1;
    }

    // Считываем заголовки секций
    Elf32_Shdr* shdrs = (Elf32_Shdr*)malloc(ehdr.e_shnum * sizeof(Elf32_Shdr));
    fseek(felf, ehdr.e_shoff, SEEK_SET);
    fread(shdrs, sizeof(Elf32_Shdr), ehdr.e_shnum, felf);

    // Считываем строковую таблицу секций
    char* shstrtab = (char*)malloc(shdrs[ehdr.e_shstrndx].sh_size);
    fseek(felf, shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, felf);

    uint32_t text_size = 0;
    uint32_t data_size = 0;
    uint32_t bss_size  = 0;

    for (int i = 0; i < ehdr.e_shnum; i++) {
        const char* name = shstrtab + shdrs[i].sh_name;
        if (strcmp(name, ".text") == 0 || strcmp(name, ".rodata") == 0) {
            text_size += shdrs[i].sh_size;
        } else if (strcmp(name, ".data") == 0) {
            data_size += shdrs[i].sh_size;
        } else if (strcmp(name, ".bss") == 0 || shdrs[i].sh_type == SHT_NOBITS) {
            bss_size += shdrs[i].sh_size;
        }
    }

    free(shstrtab);
    free(shdrs);
    fclose(felf);

    // 2. Читаем бинарный образ (.bin)
    FILE* fbin = fopen(bin_path, "rb");
    if (!fbin) {
        perror("Failed to open binary payload file");
        return 1;
    }

    fseek(fbin, 0, SEEK_END);
    long bin_size = ftell(fbin);
    fseek(fbin, 0, SEEK_SET);

    char* bin_data = (char*)malloc(bin_size);
    if (fread(bin_data, 1, bin_size, fbin) != (size_t)bin_size) {
        perror("Failed to read payload data");
        fclose(fbin);
        free(bin_data);
        return 1;
    }
    fclose(fbin);

    // 3. Формируем заголовок PEX
    pex_header_t header;
    memset(&header, 0, sizeof(pex_header_t));

    header.magic          = PEX_MAGIC;
    header.header_size    = sizeof(pex_header_t);
    header.entry_point    = ehdr.e_entry - 0x04000000; // Относительное смещение точки входа
    header.stack_size     = stack_size;
    header.flags          = flags; // <-- ЗАПИСЫВАЕМ ФЛАГИ В ЗАГОЛОВОК!
    header.heap_size      = heap_size;
    header.text_size      = text_size;
    header.data_size      = (bin_size > text_size) ? (bin_size - text_size) : data_size;
    header.bss_size       = bss_size;
    header.min_kernel_ver = (1 << 4) | (0 << 2) | 0; // v1.0.0

    // 4. Записываем готовый .pex файл
    FILE* fout = fopen(out_path, "wb");
    if (!fout) {
        perror("Failed to create output PEX file");
        free(bin_data);
        return 1;
    }

    fwrite(&header, 1, sizeof(pex_header_t), fout);
    fwrite(bin_data, 1, bin_size, fout);

    fclose(fout);
    free(bin_data);

    // printf("┌── .PEx summary\n");
    // printf("│  .text size  : %-6u\n", header.text_size);
    // printf("│  .data size  : %-6u\n", header.data_size);
    // printf("│  .bss size   : %-6u\n", header.bss_size);
    // printf("│  Stack size  : %-6u\n", header.stack_size);
    // printf("│  Heap size   : %-6u\n", header.heap_size);
    // printf("│  Total File  : %-6lu bytes -> %s\n", bin_size + sizeof(pex_header_t), out_path);
    // printf("└──\n");

    return 0;
}