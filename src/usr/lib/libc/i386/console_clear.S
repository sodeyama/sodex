#include <sys/syscall.h>

.global console_clear
console_clear:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_CONSOLE_CLEAR, %eax
        int     $0x80
        popl    %ebp
        ret
