#include <sys/syscall.h>

.global sleep_ticks
sleep_ticks:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_SLEEP_TICKS, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
