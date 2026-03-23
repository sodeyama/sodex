SYS_CALL_EXIT = 1
        	
.text	
.extern environ
.global _start	
_start:
        movl    %ecx, environ
        pushl   %ebx
        pushl   %eax
        call    main
        movl    %eax, %ebx
        movl    $SYS_CALL_EXIT, %eax
        int     $0x80

                
