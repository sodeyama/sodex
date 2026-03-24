#include <sys/syscall.h>

.global get_boot_profile
get_boot_profile:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_GET_BOOT_PROFILE, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
