#include <sys/syscall.h>
        	
.global getdentry
getdentry:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_GETDENTRY, %eax
        int     $0x80
        popl    %ebp
        ret

