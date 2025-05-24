#include <sys/syscall.h>
        	
.global mkdir
mkdir:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_MKDIR, %eax
        movl    0x8(%ebp), %ebx
        movl    0xC(%ebp), %ecx
        int     $0x80
        popl    %ebp
        ret

