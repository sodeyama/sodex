.global memset
memset: 
        pushl   %ebp
        movl    %esp, %ebp
        movl    0x8(%ebp), %edi
        movl    0xC(%ebp), %eax
        movl    0x10(%ebp), %ecx

        cld
        rep
        stosl

        popl    %ebp
        ret

