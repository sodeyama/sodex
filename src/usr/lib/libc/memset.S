.global memset
memset: 
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %edi
        movl    0x8(%ebp), %edx
        movl    %edx, %edi
        movl    0xC(%ebp), %eax
        movl    0x10(%ebp), %ecx

        cld
        rep
        stosb

        movl    %edx, %eax
        popl    %edi
        popl    %ebp
        ret
