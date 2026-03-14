#include <sys/syscall.h>

.global console_rows
console_rows:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_CONSOLE_ROWS, %eax
        int     $0x80
        popl    %ebp
        ret
