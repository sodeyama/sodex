#include <sys/syscall.h>

.global console_cols
console_cols:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_CONSOLE_COLS, %eax
        int     $0x80
        popl    %ebp
        ret
