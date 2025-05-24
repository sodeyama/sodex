.global memcpy
memcpy: 
        pushl   %ebp
        movl    %esp, %ebp
        movl    0x8(%ebp), %edi
        movl    0xC(%ebp), %esi
        movl    0x10(%ebp), %ecx
        
        cld
        rep
        movsl
        
        popl    %ebp
        ret

