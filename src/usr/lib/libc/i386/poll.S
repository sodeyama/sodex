#include <sys/syscall.h>

.global poll
poll:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_POLL, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
