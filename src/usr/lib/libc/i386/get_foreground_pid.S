#include <sys/syscall.h>

.global get_foreground_pid
get_foreground_pid:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_GET_FOREGROUND_PID, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
