SYS_CALL_EXIT = 1
        	
.text	
.global _start	
_start:
        pushl   %ebx
        pushl   %eax
        call    main
        movl    %eax, %ebx
        movl    $SYS_CALL_EXIT, %eax
        int     $0x80

                

