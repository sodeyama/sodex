#include <sys/syscall.h>
        	
.global chdir
chdir:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_CHDIR, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebp
        ret

