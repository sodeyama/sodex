#include <sys/syscall.h>

.global set_foreground_pid
set_foreground_pid:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_SET_FOREGROUND_PID, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
