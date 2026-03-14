#include <sys/syscall.h>

.global set_input_mode
set_input_mode:
        pushl   %ebp
        movl    %esp, %ebp
        movl    $SYS_CALL_SET_INPUT_MODE, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebp
        ret
