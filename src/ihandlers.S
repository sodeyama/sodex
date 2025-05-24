/*
 *  @File        ihandlers.S
 *  @Brief       interrupt handlers
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/05/13  update: 2007/05/15  
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <idt.h>
#include <ihandlers.h>
#include <process.h>

__KERNEL_CS = 0x08        
__KERNEL_DS = 0x10
__USER_CS = 0x23
__USER_DS = 0x2B
SYS_CALL_TIMER = 250        

/* void asm_defaulthandler()
 *  This is for default Handler when interrupt occur.
 */
.global asm_defaulthandler
.align 4, 0x90      
asm_defaulthandler:
        cli
        pusha
        call    defaulthandler
        popa
        sti
        iret

.global asm_i00h
.align 4, 0x90              
asm_i00h:  
        cli     
        pusha
        call    i00h
        popa
        sti
        iret

.global asm_i01h
.align 4, 0x90                      
asm_i01h:  
        cli     
        pusha
        call    i01h
        popa
        sti
        iret

.global asm_i02h
.align 4, 0x90                      
asm_i02h:  
        cli     
        pusha
        call    i02h
        popa
        sti
        iret

.global asm_i03h
.align 4, 0x90                      
asm_i03h:  
        cli     
        pusha
        call    i03h
        popa
        sti
        iret

.global asm_i04h
.align 4, 0x90                      
asm_i04h:  
        cli     
        pusha
        call    i04h
        popa
        sti
        iret

.global asm_i05h
.align 4, 0x90                      
asm_i05h:  
        cli     
        pusha
        call    i05h
        popa
        sti
        iret

.global asm_i06h
.align 4, 0x90                      
asm_i06h:  
        cli     
        pusha
        call    i06h
        popa
        sti
        iret

.global asm_i07h
.align 4, 0x90                      
asm_i07h:  
        cli     
        pusha
        call    i07h
        popa
        sti
        iret

.global asm_i08h
.align 4, 0x90                      
asm_i08h:  
        cli     
        pusha
        call    i08h
        popa
        sti
        iret

.global asm_i09h
.align 4, 0x90                      
asm_i09h:  
        cli     
        pusha
        call    i09h
        popa
        sti
        iret        

.global asm_i0Ah
.align 4, 0x90              
asm_i0Ah:  
        cli     
        pusha
        call    i0Ah
        popa
        sti
        iret

.global asm_i0Bh
.align 4, 0x90                      
asm_i0Bh:  
        cli     
        pusha
        call    i0Bh
        popa
        sti
        iret

.global asm_i0Ch
.align 4, 0x90                      
asm_i0Ch:  
        cli     
        pusha
        call    i0Ch
        popa
        sti
        iret

.global asm_i0Dh
.align 4, 0x90              
asm_i0Dh:
        cli
        movl    %esp, %ebp
        pushl   %ebp
        #push    %ds
        #push    %es
        #pusha
        #movw    $0x10, %ax
        #movw    %ax, %ds
        #movw    %ax, %es    
        call    i0Dh
        call    to_usermode
        popa
        pop     %es
        pop     %ds
        sti
        iret

.global asm_i0Eh
.align 4, 0x90      
asm_i0Eh:  
        cli     
        pusha
        call    i0Eh
        popa
        sti
        iret

.global asm_i10h
.align 4, 0x90              
asm_i10h:  
        cli     
        pusha
        call    i10h
        popa
        sti
        iret

.global asm_i11h
.align 4, 0x90      
asm_i11h:  
        cli     
        pusha
        call    i11h
        popa
        sti
        iret

.global asm_i12h
.align 4, 0x90              
asm_i12h:  
        cli     
        pusha
        call    i12h
        popa
        sti
        iret

.global asm_i13h
.align 4, 0x90
asm_i13h:  
        cli     
        pusha
        call    i13h
        popa
        sti
        iret                                        

.global asm_process_switch
.align 4, 0x90
IRET_1 = 0x4    # EIP
IRET_2 = 0x8    # CS
IRET_3 = 0xC    # EFLAGS
IRET_4 = 0x10   # ESP
IRET_5 = 0x14   # SS                             
IS_USERMODE_CS   = 0x8
SAME_PRIVILEGE   = 0
OUTER_PRIVILEGE  = 1                
asm_process_switch:
        cli
        pushl   %ebp
        movl    %esp, %ebp
        pusha
        #subl    $128, %esp
        movw    $__KERNEL_DS, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %fs
        movw    %ax, %gs
        movl    IS_USERMODE_CS(%ebp), %eax
        cmp     $__KERNEL_CS, %ax
        je      same_privilege_switch     # prev's cs is kernel's cs
        cmp     $__USER_CS, %ax
        je      outer_privilege_switch
error:
        movl    $0x9999, %eax
        jmp     error
same_privilege_switch:                    # kernel to kernel
        movl    $SAME_PRIVILEGE, %eax
        jmp     1f
outer_privilege_switch:                   # user to kernel
        movl    $OUTER_PRIVILEGE, %eax
1:
        pushl   %ebp
        pushl   IRET_5(%ebp)
        pushl   IRET_4(%ebp)
        pushl   IRET_3(%ebp)
        pushl   IRET_2(%ebp)
        pushl   IRET_1(%ebp)
        pushl   %eax
        call    i20h_do_timer
#The following parts are never executed        
        popa
        popl    %ebp
        movw    $0x2B, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %fs
        movw    %ax, %gs
        sti
        iret

.global asm_pictimer
.align 4, 0x90      
asm_pictimer:   
        pusha
        call    i20h_pictimer
        popa
        iret        

.global asm_i21h
.align 4, 0x90                      
asm_i21h:
		cli
        pusha
        push    %es
        push    %ds
        call    i21h_keyhandler
        pop     %ds
        pop     %es
        popa
		sti
        iret

.global asm_fdchandler
.align 4, 0x90              
asm_fdchandler:
        pusha
        push    %es
        push    %ds
        call    i26h_fdchandler
        pop     %ds
        pop     %es
        popa
        iret

.global asm_uhcihandler
.align 4, 0x90              
asm_uhcihandler:
        pusha
        push    %es
        push    %ds
        call    intr_uhcihandler
        pop     %ds
        pop     %es
        popa
        iret        

.global asm_i2Bh
.align 4, 0x90              
asm_i2Bh:
        pusha
        push    %es
        push    %ds
        call    i2Bh_ne2000_interrupt
        pop     %ds
        pop     %es
        popa
        iret


/*      
 *   0(%esp) - %ebx
 *   4(%esp) - %ecx
 *   8(%esp) - %edx
 *   C(%esp) - %esi
 *  10(%esp) - %edi
 *  14(%esp) - %ebp
 *  18(%esp) - %eax
 *  1C(%esp) - %ds
 *  20(%esp) - %es
 *  24(%esp) - orig_eax
 *  28(%esp) - %eip
 *  2C(%esp) - %cs
 *  30(%esp) - %eflags
 *  34(%esp) - %oldesp
 *  38(%esp) - %oldss
 */
# int 0x80
CS      = 0x8        
.global asm_syscall
.align 4, 0x90
asm_syscall:
        pushl   %ebp
        movl    %esp, %ebp

        pusha
        movw    $__KERNEL_DS, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %fs
        movw    %ax, %gs
        movl    IS_USERMODE_CS(%ebp), %eax
        cmp     $__KERNEL_CS, %ax
        je      syscall_same_privilege_switch     # prev's cs is kernel's cs
        cmp     $__USER_CS, %ax
        je      syscall_outer_privilege_switch
1:
        movl    $0x9999, %eax
        jmp     1b
syscall_same_privilege_switch:                    # kernel to kernel
        movl    $SAME_PRIVILEGE, %eax
        jmp     1f
syscall_outer_privilege_switch:                   # user to kernel
        movl    $OUTER_PRIVILEGE, %eax
1:
        pushl   %ebp
        pushl   IRET_5(%ebp)
        pushl   IRET_4(%ebp)
        pushl   IRET_3(%ebp)
        pushl   IRET_2(%ebp)
        pushl   IRET_1(%ebp)
        pushl   %eax
        call    i80h_syscall
        
        addl    $28, %esp
        cli

        popl    %edi
        popl    %esi
        popl    %ebp
        addl    $4, %esp      # %esp
        popl    %ebx
        popl    %edx
        popl    %ecx
        addl    $4, %esp      # %eax

        pushl   %eax
        movw    $__USER_DS, %ax
        movw    %ax, %ds
        movw    %ax, %es        
        movw    %ax, %fs
        movw    %ax, %gs
        popl    %eax

        popl    %ebp
        iret
