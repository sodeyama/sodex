#include <sys/syscall.h>

.global debug_write
debug_write:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_DEBUG_WRITE, %eax
        movl    0x8(%ebp), %ebx
        movl    0xC(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
