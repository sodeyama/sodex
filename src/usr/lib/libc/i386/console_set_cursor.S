#include <sys/syscall.h>

.global console_set_cursor
console_set_cursor:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_CONSOLE_SET_CURSOR, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
