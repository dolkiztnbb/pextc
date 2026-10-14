ENTRY(_start)

SECTIONS
{
    /* Базовый адрес в виртуальной памяти PEX-процесса */
    . = 0x04000000;

    .text ALIGN(4) : {
        _text_start = .;
        *(.text.entry)
        *(.text .text.*)
        _text_end = .;
    }

    .rodata ALIGN(4) : {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    }

    .data ALIGN(4) : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    .bss ALIGN(4) : {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}