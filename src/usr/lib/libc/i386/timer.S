#include <sys/syscall.h>
        	
.global sys_timer
sys_timer:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_TIMER, %eax
        int     $0x80
        popl    %ebp
        ret

