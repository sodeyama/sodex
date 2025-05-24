#include <sys/syscall.h>
        	
.global creat
creat:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_CREAT, %eax
        movl    0x8(%ebp), %ebx
        movl    0xC(%ebp), %ecx
        int     $0x80
        popl    %ebp
        ret

