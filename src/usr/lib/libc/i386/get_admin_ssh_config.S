#include <sys/syscall.h>

.global get_admin_ssh_config
get_admin_ssh_config:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_GET_ADMIN_SSH_CONFIG, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
