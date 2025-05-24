#include <sys/syscall.h>
        	
.global send
send:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_SEND, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebp
        ret

