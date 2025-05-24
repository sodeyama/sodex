#include <sys/syscall.h>
        	
.global lseek
lseek:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_LSEEK, %eax
        movl    0x8(%ebp), %ebx
        movl    0xC(%ebp), %ecx
        movl    0x10(%ebp), %edx
        int     $0x80
        popl    %ebp
        ret

