#include <sys/syscall.h>

.global rename
rename:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_RENAME, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
