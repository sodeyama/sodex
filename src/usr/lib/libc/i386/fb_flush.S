#include <sys/syscall.h>

.global fb_flush
fb_flush:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_FB_FLUSH, %eax
        int     $0x80
        popl    %ebp
        ret
