#include <sys/syscall.h>
        	
.global _brk
_brk:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_BRK, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebp
        ret

