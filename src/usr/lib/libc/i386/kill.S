#include <sys/syscall.h>
        	
.global kill
kill:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_KILL, %eax
        movl    0x8(%ebp), %ebx
        movl    0xC(%ebp), %ecx
        int     $0x80
        popl    %ebp
        ret

