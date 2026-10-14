.global _start
.extern main

.section .text.entry, "ax", @progbits
.align 4

_start:
    /* Согласно System V ABI:
       4(%esp) = argc
       8(%esp) = argv */
    movl 4(%esp), %eax      /* Забираем argc */
    movl 8(%esp), %edx      /* Забираем argv */

    /* Обязательное выравнивание стека по 16 байт для GCC */
    andl $-16, %esp
    
    /* Кладем аргументы для вызова main(argc, argv) */
    subl $8, %esp           /* Паддинг выравнивания */
    pushl %edx              /* push argv */
    pushl %eax              /* push argc */
    
    call main

    /* Завершение: SYS_EXIT(код возврата из EAX) */
    movl %eax, %ebx         /* Статус выхода */
    movl $0x23, %eax        /* SYS_EXIT = 0x23 */
    int $0x80

.halt_loop:
    hlt
    jmp .halt_loop