#ifndef PEX_HEADER_H
#define PEX_HEADER_H

#include <stdint.h>

#define PEX_MAGIC 0x71414550 // "PE\x41\x71"

typedef struct __attribute__((packed)) {
    uint32_t magic;          
    uint32_t checksum;       
    uint16_t header_size;    
    uint16_t flags;          
    
    uint32_t min_kernel_ver; 
    uint32_t entry_point;    // Смещение точки входа от конца заголовка (обычно 0)
    uint32_t stack_size;     
    uint32_t heap_size;      
    
    uint32_t text_size;      
    uint32_t data_size;      
    uint32_t bss_size;       
} pex_header_t;

#endif