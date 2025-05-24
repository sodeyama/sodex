#include <sys/syscall.h>
        
.global exit
exit:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_EXIT, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebp
        ret

