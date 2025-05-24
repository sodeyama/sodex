#include <sys/syscall.h>
        	
.global getpstat
getpstat:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_GETPSTAT, %eax
        int     $0x80
        popl    %ebp
        ret

