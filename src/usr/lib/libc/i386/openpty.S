#include <sys/syscall.h>

.global openpty
openpty:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_OPENPTY, %eax
        int     $0x80
        popl    %ebp
        ret
