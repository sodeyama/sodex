#include <sys/syscall.h>

.global console_putc_at
console_putc_at:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        pushl   %esi
        movl    $SYS_CALL_CONSOLE_PUTC_AT, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        movl    0x14(%ebp), %esi
        int     $0x80
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret
