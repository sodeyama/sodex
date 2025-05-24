/*
 *  @File        bootmiddle.S
 *  @Brief       The Second boot section of Sodex
 *  @Description This section mainly consist of changing from 16bit mode to
 *               32bit mode and setting other CPU's something ought to be.
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/07  update: 2007/04/18  
 *      
 *  Copyright (C) 2007 Sodex
 */

#define DEBUG

INITSEG         = 0x9000
__KERNEL_CS     = 0x08

MEMORY_SIZE     = 0x0       

.code16
.text

.global _start,middle_start,print_all_registers
_start:         
middle_start:   
        movw    %cs, %ax
        movw    %ax, %ds
        movw    %ax, %es

        # Get the max size of the physical memory,
        # and we'll set it at 0x90000.
        xorl    %ecx, %ecx
        xorl    %edx, %edx
        movw    $0x9000, %ax
        movw    %ax, %fs
        xorl    %eax, %eax
        movl    %eax, %fs:(0x0)
        jmp     pre_e820

/* We have to get the phisical memory map.
 * Using "Function E820" and "Function E801" but not "Function 88H",
 * we'll get it.    
 *  EAX 0xE820 - BIOS command to get the memory map
 *  EDX "SMAP" - special string to get the memory map
 *  ES:DI - entry buffer address to set the memory map
 *  ECX - the size of the entry buffer                                  
 */
        e820_mmap_maxsize: .byte 32
SMAP = 0x534d4150 # "SMAP"

pre_e820:               
        xorl    %ebx, %ebx
        movw    $MEMORY_MAP, %di

e820_start:
        movl    $0x0000e820, %eax        
        movl    $SMAP, %edx     
        movl    $20, %ecx
        int     $0x15
        jc      e820_error
        cmpl    $SMAP, %eax
        jc      e820_error

        cmpl    $0, %ebx
        jz      e820_end

        movb    (e820_mmap_maxsize), %al
        cmpb    $0, %al
        jz      e820_end

        decb    (e820_mmap_maxsize)
        movw    %di, %ax
        addw    $20, %ax
        movw    %ax, %di
        jmp     e820_start              # get the next memory map

e820_error:
        call    print_all_registers
        movw    $e820_fail_mes, %si
        call    printstr
        
e820_end:

/* We have to get the memory size. 
 * This function is the getting physical memory size from BIOS.
 *  If memory size is less than 64MB, this function will be failed.
 *  At the time, we'll use the 88 function.     
 */
                
e801_start:
        movw    $0xe801, %ax
        int     $0x15
        jc      e801_error
#if DEBUG
        call    print_all_registers
#endif      
        pushw   %bx
        shr     $10, %ax
        xorb    %ah, %ah
        incb    %al
        movw    %ax, (MEMORY_SIZE)
        popw    %bx
        shr     $4, %bx
        addw    %bx, (MEMORY_SIZE)

#if DEBUG
        movw    (MEMORY_SIZE), %ax
        call    print_register
#endif
                        
        jmp     mem_check_end

e801_error:     
        call    print_all_registers
        movw    $e801_fail_mes, %si
        call    printstr

/* This function have the feature getting memory size less than 64MB
 *  If this function also fail, we set 64MB as default memory size.
 */                             
m88_start:
        movb    $0x88, %ah
        int     $0x15
        shr     $10, %ax
        xorb    %ah, %ah
        incb    %al
        movw    %ax, (MEMORY_SIZE)      
#if DEBUG
        movw    (MEMORY_SIZE), %ax
        call    print_register
#endif      
        
        jmp     mem_check_end

m88_error:      
        call    print_all_registers
        movw    $m88_fail_mes, %si
        call    printstr

mem_set_default:
        movw    $0x40, %ax              # set 64MB
        movw    %ax, (MEMORY_SIZE)

mem_check_end:

        

a20_start:              
        cli                             # ignore interrupt
        movb    $0x80, %al
        outb    %al, $0x70              # stop NMI
        jmp     a20_main_loop

a20_loop_tries: .byte 0xff  # we try to test the following process 0xff times
a20_main_loop:
        call    a20_test                # check if a20's test is already done
        jnz     a20_end

a20_bios:
        movw    $0x2401, %ax
        pushfl
        int     $0x15
        popfl

        call    a20_test
        jnz     a20_end
        
a20_kbc:
        call    a20_kbc_empty_check

        call    a20_test                # just in case
        jnz     a20_end

        movb    $0xd1, %al
        outb    %al, $0x64
        call    a20_kbc_empty_check

a20_kbc_wait:   
        xorw    %cx, %cx
a20_kbc_wait_loop:      
        call    a20_test
        jnz     a20_end
        loop    a20_kbc_wait_loop
        
a20_fast:       
        inb     $0x92, %al
        orb     $0x02, %al
        andb    $0xfe, %al
        outb    %al, $0x92

a20_fast_wait:
        xorw    %cx, %cx
a20_fast_wait_loop:     
        call    a20_test
        jnz     a20_end
        loop    a20_fast_wait_loop      
        
        decb    (a20_loop_tries)
        jnz     a20_main_loop

        movw    $a20_fail_mes, %si
        call    printstr

a20_fail:       
        hlt
        jmp     a20_fail                
        
a20_end:                

        xorl    %eax, %eax
        movw    %ds, %ax
        shll    $4, %eax
        addl    $gdt, %eax
        movl    %eax, (gdtr+2)

        lgdt    gdtr
        lidt    idtr

        movw    $1, %ax
        lmsw    %ax
        jmp     flush_pipe_queue
        
flush_pipe_queue:
        .byte   0x66, 0xea
        .long   first_code32 + 0x90000
        .word   __KERNEL_CS


print_mmap:
        pusha
        movw    $MEMORY_MAP, %si
        movw    $0x20, %dx
print_mmap_start:           
        movw    $20, %cx
print_mmap_itr_start:
        lodsb
        addb    $0x30, %al
        movb    $0x0e, %ah
        movb    $0, %bh
        int     $0x10
        decw    %cx
        cmpw    $0, %cx
        jnz     print_mmap_itr_start
print_mmap_itr_end:
        decw    %dx
        jnz     print_mmap_start        
        popa
        ret

A20_NUM_OF_TEST = 8             # In Linux, this param is 32, but I think
                                # it is enough by two times.
A20_CHECK_ADDR  = 0x100             

# If this test fail, it won't return zero flag      
a20_test:       
        pusha
        xorw    %bx, %bx
        movw    %bx, %fs                # fs = 0x0000
        incw    %bx
        movw    %bx, %gs                # gs = 0xffff
        movw    $A20_NUM_OF_TEST, %cx
        movw    %fs:(A20_CHECK_ADDR), %ax
        pushw   %ax
a20_test_loop:
        incw    %ax
        movw    %ax, %fs:(A20_CHECK_ADDR)
        call    delay
        cmpw    %gs:(A20_CHECK_ADDR+0x10), %ax
                                        # 0xffff0 + 0x10 + 0x100 = 0x100100
        loope   a20_test_loop
        popw    %ax
        popa
        ret

delay:
        pushw   %cx
        movw    $32, %cx
delay_loop:             
        nop
        loop    delay_loop
        popw    %cx
        ret


a20_kbc_empty_check:
        pushl   %ecx
        movl    $100000, %ecx
a20_kbc_empty_check_loop:       
        decl    %ecx
        jz      a20_kbc_empty_check_end
        call    delay
        inb     $0x64, %al
        testb   $1, %al
        jz      no_output
        call    delay
        inb     $0x60, %al
        jmp     a20_kbc_empty_check_loop
no_output:      
        testb   $2, %al
        jnz     a20_kbc_empty_check_loop        
a20_kbc_empty_check_end:        
        popl    %ecx                
        ret             

#########################################               
# for debug
#########################################                       
_ax:    .ascii "ax:\0"
_bx:    .ascii "bx:\0"
_cx:    .ascii "cx:\0"
_dx:    .ascii "dx:\0"
_si:    .ascii "si:\0"
_di:    .ascii "di:\0"
_sp:    .ascii "sp:\0"
_cs:    .ascii "cs:\0"
_ds:    .ascii "ds:\0"
_es:    .ascii "es:\0"
_ss:    .ascii "ss:\0"
_space: .ascii " \0"        
        
print_all_registers:        
        pusha
        pushw   %si
        
        movw    $_ax, %si
        call    printstr
        call    print_register  # print ax
        
        movw    $_bx, %si
        call    printstr
        movw    %bx, %ax
        call    print_register  # print bx

        movw    $_cx, %si
        call    printstr
        movw    %cx, %ax
        call    print_register  # print cx
        
        movw    $_dx, %si
        call    printstr
        movw    %dx, %ax
        call    print_register  # print dx

        movw    $_si, %si
        call    printstr
        popw    %si
        movw    %si, %ax
        call    print_register  # print si

        movw    $_di, %si
        call    printstr
        movw    %di, %ax
        call    print_register  # print di

        movw    $_cs, %si
        call    printstr
        movw    %cs, %ax
        call    print_register  # print cs

        movw    $_ds, %si
        call    printstr
        movw    %ds, %ax
        call    print_register  # print ds

        movw    $_es, %si
        call    printstr
        movw    %es, %ax
        call    print_register  # print es

        movw    $_ss, %si
        call    printstr
        movw    %ss, %ax
        call    print_register  # print ss
        
        popa
        ret     

print_register: # print %ax
        pushw   %ax
        shr     $8, %ax
        call    print_char
        popw    %ax
        call    print_char
        pushw   %si
        movw    $_space, %si
        call    printstr
        popw    %si
        ret             

print_char:
        pushw   %bx     
        pushw   %ax
        shr     $4, %al
        cmpb    $9, %al
        
        ja      print_check1
        addb    $0x30, %al
        jmp     print_check1_end
print_check1:
        addb    $0x37, %al
print_check1_end:
        
        movb    $0x0e, %ah
        xorb    %bh, %bh
        int     $0x10
        popw    %ax
        movb    %al, %bl
        pushw   %ax
        movb    %bl, %al
        andb    $0x0f, %al

        cmpb    $9, %al
        ja      print_check2
        addb    $0x30, %al
        jmp     print_check2_end
print_check2:
        addb    $0x37, %al
print_check2_end:
        
        movb    $0x0e, %ah
        xorb    %bh, %bh
        int     $0x10
        popw    %ax
        popw    %bx     
        ret

printstr:
        pusha
printstr_start:
        lodsb
        cmpb    $0, %al
        jz      printstr_end
        movb    $0x0e, %ah
        movb    $0, %bh
        int     $0x10
        jmp     printstr_start
printstr_end:
        popa
        ret
                

.code32
.text
first_code32:
        movl    $0x10, %eax
        movw    %ax, %ds
        movw    %ax, %es
		movw	%ax, %fs
		movw	%ax, %gs
        #movl    $0x18, %eax
        movw    %ax, %ss
        movl    $0x9c000, %esp

        ljmp    $__KERNEL_CS, $0x1000

forever:   
        jmp     forever        
        
sec_boot_mes:   
        .ascii  "This is middle boot section.\r\n\0"
e820_fail_mes:  
        .ascii  "e820: getting memory map is failed\r\n\0"
e801_fail_mes:
        .ascii  "e801: getting memory size function is failed\r\n\0"
m88_fail_mes:           
        .ascii  "88: getting memory size function is failed\r\n\0"
a20_fail_mes:   
        .ascii  "a20 enable function failed\r\n\0"      
                
MEMORY_MAP:
        .rept 32
        .long 0         # addr
        .long 0
        
        .long 0         # size
        .long 0
        
        .long 0         # type
        .endr

#MEMORY_SIZE:   
#       .word   0       

gdtr:   .word   gdt_end - gdt - 1
        .long   0       # filled in code
gdt:
gdt00h:                 #It's dummy for cpu
        .word   0       #selector 00h
        .word   0
        .byte   0
        .byte   0
        .byte   0
        .byte   0
gdt08h:                 #code segment
        .word   0xFFFF  #selector 08h
        .word   0
        .byte   0
        .byte   0x9A    # P=1, DPL=0, S=1, TYPE=5, A=0
        .byte   0xCF    # G=1, D=1
        .byte   0
gdt10h:                 #data segment
        .word   0xFFFF  #selector 10h
        .word   0
        .byte   0
        .byte   0x92    # P=1, DPL=0, S=1, TYPE=1, A=0
        .byte   0xCF    # G=1, D=1
        .byte   0
gdt18h:                 #stack segment
        .word   0       #selector 18h 
        .word   0x0
        .byte   0x0
        #.byte   0x96    # P=1, DPL=0, S=1, TYPE=3, A=0
        .byte   0x92    # P=1, DPL=0, S=1, TYPE=1, A=0		
        .byte   0xC0    # G=1, D=1
        .byte   0                       
gdt_end:                

idtr:   .word   0
        .word   0, 0

        .org 0x800
