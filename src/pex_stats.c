#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../header/pex_header.h"

typedef struct {
    char name[64];
    uint32_t file_size;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t ram_total; // text + data + bss + stack + heap
} app_stat_t;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <pex_file1> [pex_file2 ...]\n", argv[0]);
        return 1;
    }

    int total_files = argc - 1;
    app_stat_t* stats = (app_stat_t*)malloc(sizeof(app_stat_t) * total_files);
    if (!stats) return 1;

    int valid_count = 0;

    uint32_t min_file = 0xFFFFFFFF, max_file = 0;
    uint64_t sum_file = 0;

    uint32_t min_text = 0xFFFFFFFF, max_text = 0;
    uint64_t sum_text = 0;

    uint32_t min_data = 0xFFFFFFFF, max_data = 0;
    uint64_t sum_data = 0;

    uint32_t min_bss = 0xFFFFFFFF, max_bss = 0;
    uint64_t sum_bss = 0;

    uint32_t min_ram = 0xFFFFFFFF, max_ram = 0;
    uint64_t sum_ram = 0;

    for (int i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        pex_header_t hdr;
        if (fread(&hdr, 1, sizeof(pex_header_t), f) != sizeof(pex_header_t) || hdr.magic != PEX_MAGIC) {
            fclose(f);
            continue;
        }
        fclose(f);

        const char* base_name = strrchr(argv[i], '/');
        base_name = base_name ? base_name + 1 : argv[i];

        app_stat_t* s = &stats[valid_count++];
        strncpy(s->name, base_name, sizeof(s->name) - 1);
        s->name[sizeof(s->name) - 1] = '\0';
        s->file_size = (uint32_t)fsize;
        s->text_size = hdr.text_size;
        s->data_size = hdr.data_size;
        s->bss_size  = hdr.bss_size;
        s->ram_total = hdr.text_size + hdr.data_size + hdr.bss_size + hdr.stack_size + hdr.heap_size;

        if (s->file_size < min_file) min_file = s->file_size;
        if (s->file_size > max_file) max_file = s->file_size;
        sum_file += s->file_size;

        if (s->text_size < min_text) min_text = s->text_size;
        if (s->text_size > max_text) max_text = s->text_size;
        sum_text += s->text_size;

        if (s->data_size < min_data) min_data = s->data_size;
        if (s->data_size > max_data) max_data = s->data_size;
        sum_data += s->data_size;

        if (s->bss_size < min_bss) min_bss = s->bss_size;
        if (s->bss_size > max_bss) max_bss = s->bss_size;
        sum_bss += s->bss_size;

        if (s->ram_total < min_ram) min_ram = s->ram_total;
        if (s->ram_total > max_ram) max_ram = s->ram_total;
        sum_ram += s->ram_total;
    }

    if (valid_count == 0) {
        printf("[PEX STATS] No valid PEX binaries found.\n");
        free(stats);
        return 0;
    }

    // Вывод красивой таблицы (ровно 80 символов в ширину)
    printf("\n");
    printf("\033[1;34m┌──────────────────────────────────────────────────────────────────────────────┐\033[0m\n");
    printf("\033[1;34m│\033[1;37m                          USERLAND PEX BINARIES STATS                         \033[1;34m│\033[0m\n");
    printf("\033[1;34m├──────────────┬──────────┬─────────┬────────┬───────────┬───────────┬─────────┤\033[0m\n");
    printf("\033[1;34m│\033[1;36m App Name     \033[1;34m│\033[1;36m .text    \033[1;34m│\033[1;36m .data   \033[1;34m│\033[1;36m .bss   \033[1;34m│\033[1;36m File Size \033[1;34m│\033[1;36m  +Δ Base  \033[1;34m│\033[1;36m RAM Tot \033[1;34m│\033[0m\n");
    printf("\033[1;34m├──────────────┼──────────┼─────────┼────────┼───────────┼───────────┼─────────┤\033[0m\n");

    for (int i = 0; i < valid_count; i++) {
        uint32_t delta = stats[i].file_size - min_file;
        char delta_str[16];

        printf("│ %-12s │ %6u B │ %5u B │ %4u B │ %7u B │",
               stats[i].name,
               stats[i].text_size,
               stats[i].data_size,
               stats[i].bss_size,
               stats[i].file_size);

        // Выделение: если это базовый минимум — пишем base зеленым, если больше — пишем +X B желтым
        if (delta == 0) {
            printf(" \033[1;32m  base   \033[0m │");
        } else {
            snprintf(delta_str, sizeof(delta_str), "+%u B", delta);
            printf(" \033[1;33m%9s\033[0m │", delta_str);
        }

        printf(" %5u B │\n", stats[i].ram_total);
    }

    printf("\033[1;34m├──────────────┴──────────┴─────────┴────────┴───────────┴───────────┴─────────┤\033[0m\n");
    printf("│ Total Programs : %-59d │\n", valid_count);
    printf("│ Total Size     : %-53llu bytes │\n", (unsigned long long)sum_file);
    printf("│ Base Overhead  : %-41ubytes (min binary) │\n", min_file);
    printf("│ Net Payload    : Avg = %-8.1f B  │  Max = %-13u B (net code/data)  │\n",
           ((double)sum_file / valid_count) - min_file, max_file - min_file);
    printf("\033[1;34m├──────────────────────────────────────────────────────────────────────────────┤\033[0m\n");

    // Итоговые показатели Min / Avg / Max
    double avg_file = (double)sum_file / valid_count;
    double avg_text = (double)sum_text / valid_count;
    double avg_data = (double)sum_data / valid_count;
    double avg_bss  = (double)sum_bss  / valid_count;
    double avg_ram  = (double)sum_ram  / valid_count;

    printf("│ \033[1;33mPEX File Size \033[0m : Min = %-7u B  │  Avg = %-8.1f B  │  Max = %-7u B    │\n",
           min_file, avg_file, max_file);
    printf("│ \033[1;33mCode (.text)  \033[0m : Min = %-7u B  │  Avg = %-8.1f B  │  Max = %-7u B    │\n",
           min_text, avg_text, max_text);
    printf("│ \033[1;33mData (.data)  \033[0m : Min = %-7u B  │  Avg = %-8.1f B  │  Max = %-7u B    │\n",
           min_data, avg_data, max_data);
    printf("│ \033[1;33mBSS  (.bss)   \033[0m : Min = %-7u B  │  Avg = %-8.1f B  │  Max = %-7u B    │\n",
           min_bss, avg_bss, max_bss);
    printf("│ \033[1;33mRAM Allocation\033[0m : Min = %-7u B  │  Avg = %-8.1f B  │  Max = %-7u B    │\n",
           min_ram, avg_ram, max_ram);
    printf("\033[1;34m└──────────────────────────────────────────────────────────────────────────────┘\033[0m\n\n");

    free(stats);
    return 0;
}