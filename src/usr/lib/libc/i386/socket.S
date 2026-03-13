#include <sys/syscall.h>

.global socket
socket:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_SOCKET, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret

.global bind
bind:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_BIND, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret

.global listen
listen:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_LISTEN, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret

.global accept
accept:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_ACCEPT, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret

.global connect
connect:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_CONNECT, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret

.global send_msg
send_msg:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        pushl   %esi
        movl    $SYS_CALL_SEND_MSG, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        movl    0x14(%ebp), %esi
        int     $0x80
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret

.global recv_msg
recv_msg:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        pushl   %esi
        movl    $SYS_CALL_RECV, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        movl    0x14(%ebp), %esi
        int     $0x80
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret

.global sendto
sendto:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        pushl   %esi
        pushl   %edi
        movl    $SYS_CALL_SENDTO, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        movl    0x14(%ebp), %esi
        movl    0x18(%ebp), %edi
        int     $0x80
        popl    %edi
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret

.global recvfrom
recvfrom:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        pushl   %esi
        pushl   %edi
        movl    $SYS_CALL_RECVFROM, %eax
        movl    0x8(%ebp), %ebx
        movl    0xc(%ebp), %ecx
        movl    0x10(%ebp), %edx
        movl    0x14(%ebp), %esi
        movl    0x18(%ebp), %edi
        int     $0x80
        popl    %edi
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret

.global closesocket
closesocket:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ebx
        movl    $SYS_CALL_CLOSE_SOCK, %eax
        movl    0x8(%ebp), %ebx
        int     $0x80
        popl    %ebx
        popl    %ebp
        ret
