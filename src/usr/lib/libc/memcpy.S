.global memcpy
memcpy: 
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %esi
        pushl   %edi
        movl    0x8(%ebp), %eax
        movl    %eax, %edi
        movl    0xC(%ebp), %esi
        movl    0x10(%ebp), %ecx
        
        cld
        rep
        movsb
        
        popl    %edi
        popl    %esi
        popl    %ebp
        ret
