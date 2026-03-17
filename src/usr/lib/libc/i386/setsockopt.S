#include <sys/syscall.h>

.global setsockopt
setsockopt:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %edi
        pushl   %esi
        pushl   %ebx
        movl    $SYS_CALL_SETSOCKOPT, %eax
        movl    0x8(%ebp), %ebx     /* sockfd */
        movl    0xc(%ebp), %ecx     /* level */
        movl    0x10(%ebp), %edx    /* optname */
        movl    0x14(%ebp), %esi    /* optval */
        movl    0x18(%ebp), %edi    /* optlen */
        int     $0x80
        popl    %ebx
        popl    %esi
        popl    %edi
        popl    %ebp
        ret
