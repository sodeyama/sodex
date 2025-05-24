#include <sys/syscall.h>
        	
.global fork
fork:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_FORK, %eax
        int     $0x80
        popl    %ebp
        ret

