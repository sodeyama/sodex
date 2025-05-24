STDOUT_FILENO = 1
            
.text
.global _start, proctest
_start:         
proctest:
        movl    $0x4, %eax
        movl    $STDOUT_FILENO, %ebx
        movl    $mes, %ecx
        movl    $(mes_end - mes), %edx
        movl    %esp, %esi
        int     $0x80
        jmp     proctest
halt:           
        jmp     halt
        
.data
mes:    .ascii "process test message 1\n\0"
mes_end:
