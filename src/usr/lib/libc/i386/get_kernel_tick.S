#include <sys/syscall.h>

.global get_kernel_tick
get_kernel_tick:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_GET_KERNEL_TICK, %eax
        int     $0x80
        popl    %ebp
        ret
