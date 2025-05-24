#define SYS_CALL_CLOSE		6
#include <sys/syscall.h>
        	
.global close
close:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_CLOSE, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebp
        ret

