#include <sys/syscall.h>

.global dup
dup:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_DUP, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
