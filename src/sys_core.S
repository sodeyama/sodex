/*
 *  @File        sys_core.S
 *  @Brief       The API which need to be written by assembler is in this file.
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/19  update: 2007/04/23  
 *      
 *  Copyright (C) 2007 Sodex
 */
#include <io.h>
#include <descriptor.h>
#include <vga.h>
#include <process.h>

VRAM_ADDRESS = 0xc00b8000

.code32
.text
.global out8, out16, out32, in8, in16, in32
.global enableInterrupt, disableInterrupt, is_enableInterrupt
.global _poscolor_printc, lgdt, lidt, ltr, procjmp
.global memset, memcpy      

enableInterrupt:
        sti
        ret
        
disableInterrupt:
        cli
        ret

INTFLAG = 0x00000200
is_enableInterrupt:
        pushl   %ebp
        movl    %esp, %ebp
        pushf
        movl    -4(%ebp), %eax
        andl    $INTFLAG, %eax
        jnz     1f
        movl    $0, %eax
        jmp     2f        
1:
        movl    $1, %eax
2:              
        popf
        popl    %ebp
        ret


/* void out8(u_int16_t port, u_int8_t num)
 *  num is al, port is dx
 */                     
out8:
        pushl   %ebp
        movl    %esp, %ebp
        movw    0x8(%ebp), %dx
        movb    0xc(%ebp), %al
        outb    %al, %dx
        popl    %ebp
        ret
        
/* void out16(u_int16_t port, u_int16_t num)
 *  num is ax, port is dx
 */                     
out16:
        pushl   %ebp
        movl    %esp, %ebp
        movw    0x8(%ebp), %dx      
        movw    0xc(%ebp), %ax
        outw    %ax, %dx
        popl    %ebp
        ret

/* void out32(u_int16_t port, u_int32_t num)
 *  num is eax, port is dx
 */                     
out32:
        pushl   %ebp
        movl    %esp, %ebp
        movw    0x8(%ebp), %dx
        movl    0xc(%ebp), %eax
        outl    %eax, %dx
        popl    %ebp
        ret     
        
/* u_int8_t in8(u_int16_t port)
 *   port is dx, return is al
 */             
in8:
        pushl   %ebp
        movl    %esp, %ebp
        movw    8(%ebp), %dx
        inb     %dx, %al
        xorb    %ah, %ah
        popl    %ebp
        ret

/* u_int16_t in16(u_int16_t port)
 *   port is dx, return is ax
 */             
in16:
        pushl   %ebp
        movl    %esp, %ebp
        movw    8(%ebp), %dx
        inw     %dx, %ax
        popl    %ebp
        ret

/* u_int32_t in16(u_int16_t port)
 *   port is dx, return is eax
 */             
in32:
        pushl   %ebp
        movl    %esp, %ebp
        movw    8(%ebp), %dx
        inl     %dx, %eax
        popl    %ebp
        ret     


/* void _poscolor_printc(int x, int y, char color, char c)
 *   x is ebx, y is eax, color is dh, c is dl
 */
_poscolor_printc:    
        pushl   %ebp
        movl    %esp, %ebp
        movl    0xc(%ebp), %eax
        movw    $80, %cx
        mulw    %cx             # eax = y * 80
        movw    $2, %cx
        mulw    %cx             # eax = y * 80 * 2

        movl    0x8(%ebp), %ebx
        addl    %ebx, %eax
        addl    %ebx, %eax
        addl    $VRAM_ADDRESS, %eax
        movb    0x14(%ebp), %dl # set char
        movb    0x10(%ebp), %dh # set color
        movw    %dx, (%eax)

        popl    %ebp
        ret

/* void lgdt(_gdtr* pgdtr)
 *  pgdtr is eax.
 */             
lgdt:   
        pushl   %ebp
        movl    %esp, %ebp
        movl    0x8(%ebp), %eax

        lgdt    (%eax)
        
        popl    %ebp
        ret

/* void lidt(_idtr* pidtr)
 *  pidtr is eax.
 */             
lidt:   
        pushl   %ebp
        movl    %esp, %ebp
        movl    0x8(%ebp), %eax

        lidt    (%eax)
        
        popl    %ebp
        ret

/* void ltr(u_int16_t selector)
 *  selector is ax          
 */
ltr:    
        pushl   %ebp
        movl    %esp, %ebp
        movw    0x8(%ebp), %ax

        ltr     %ax

        popl    %ebp
        ret

/* void *memset(void *buf, int ch, size_t n)
 *  buf is edi, ch is al, n is ecx
 *  return is buf       
 */
memset: 
        pushl   %ebp
        movl    %esp, %ebp
        movl    0x8(%ebp), %edi
        movb    0xc(%ebp), %al
        movl    0x10(%ebp), %ecx
        
        rep
        stosb

        movl    0x8(%ebp), %eax     
        popl    %ebp
        ret

/* void *memcpy(void *dest, void *src, size_t n)
 *  dest is edi, src is esi, n is ecx
 *  return is dest
 */
memcpy: 
        pushl   %ebp
        movl    %esp, %ebp
        movl    0x8(%ebp), %edi
        movl    0xc(%ebp), %esi
        movl    0x10(%ebp), %ecx
        
        rep
        movsb

        movl    0x8(%ebp), %eax     
        popl    %ebp
        ret


.global print_registers
print_registers:
        pushl   %ebp
        movl    %esp, %ebp

        # print eax
        pusha
        pushl   $str_eax
        call    _kputs
        add     $4, %esp
        popa
        pusha
        pushl   %eax
        call    _kprintb32
        add     $4, %esp
        popa

        # print ebx
        pusha
        pushl   $str_ebx
        call    _kputs
        add     $4, %esp
        popa
        pusha
        pushl   %ebx
        call    _kprintb32
        add     $4, %esp
        popa        

        # print ecx
        pusha
        pushl   $str_ecx
        call    _kputs
        add     $4, %esp
        popa
        pusha
        pushl   %ecx
        call    _kprintb32
        add     $4, %esp
        popa        

        # print edx
        pusha
        pushl   $str_edx
        call    _kputs
        add     $4, %esp
        popa
        pusha
        pushl   %edx
        call    _kprintb32
        add     $4, %esp
        popa

        # print LF
        pusha
        pushl   $str_lf
        call    _kputs
        add     $4, %esp
        popa

        popl    %ebp
        ret
.data       
str_eax:        .ascii "eax:\0"
str_ebx:        .ascii "ebx:\0"
str_ecx:        .ascii "ecx:\0"
str_edx:        .ascii "edx:\0"
str_lf:         .ascii "\n\0"       
        

.text
/* 
 * void switch_to_outer_privilege(u_int16_t cs, u_int16_t ds, u_int32_t esp,
 *                u_int32_t cr3 u_int32_t eip, struct task_struct* task,
 *                u_int32_t count, u_int32_t prev_eip)
 *  cs: 0x8(%ebp)
 *  ds: 0xC(%ebp)
 *  esp: 0x10(%ebp)
 *  cr3: 0x14(%ebp)
 *  eip: 0x18(%ebp)
 *  prev: 0x1C(%ebp)
 *  count: 0x20(%ebp)
 *  prev_eip: 0x24(%ebp)        
 */
__KERNEL_DS     = 0x10        
__USER_DS       = 0x2B        
NEXT_CS         = 0x8
NEXT_DS         = 0xC       
NEXT_ESP        = 0x10
NEXT_CR3        = 0x14
NEXT_EFLAGS     = 0x18        
NEXT_EIP        = 0x1C
SAVE_ESP        = 0x20

NEXT_EAX        = 0x24
NEXT_EBX        = 0x28
NEXT_ECX        = 0x2C
NEXT_EDX        = 0x30
NEXT_ESP        = 0x34
NEXT_EBP        = 0x38
NEXT_ESI        = 0x3C
NEXT_EDI        = 0x40

.global switch_to_outer_privilege       
switch_to_outer_privilege:
        pushl   %ebp
        movl    %esp, %ebp

        movl    SAVE_ESP(%ebp), %esp
        movl    NEXT_CR3(%ebp), %eax
        movl    %eax, %cr3

        pushl    NEXT_DS(%ebp)
        pushl    NEXT_ESP(%ebp)
        pushl    NEXT_EFLAGS(%ebp)
        pushl    NEXT_CS(%ebp)
        pushl    NEXT_EIP(%ebp)
        movw    $__USER_DS, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %fs
        movw    %ax, %gs
        movl    NEXT_EBX(%ebp), %ebx
        movl    NEXT_ECX(%ebp), %ecx
        movl    NEXT_EDX(%ebp), %edx
        movl    NEXT_ESI(%ebp), %esi
        movl    NEXT_EDI(%ebp), %edi
        movl    NEXT_EAX(%ebp), %eax        
        movl    NEXT_EBP(%ebp), %ebp
        sti
        iret

/* 
 * void switch_to_same_privilege(u_int16_t cs, u_int16_t ds, u_int32_t esp,
 *                u_int32_t cr3 u_int32_t eip, struct task_struct* task,
 *                u_int32_t count, u_int32_t prev_eip)
 *  cs: 0x8(%ebp)
 *  ds: 0xC(%ebp)
 *  esp: 0x10(%ebp)
 *  cr3: 0x14(%ebp)
 *  eip: 0x18(%ebp)
 *  prev: 0x1C(%ebp)
 *  count: 0x20(%ebp)
 *  prev_eip: 0x24(%ebp)        
 */
__KERNEL_CS     = 0x8        
NEXT_CS         = 0x8
NEXT_CR3        = 0xC
NEXT_EFLAGS     = 0x10        
NEXT_EIP        = 0x14
SAVE_ESP        = 0x18

NEXT_EAX        = 0x1C
NEXT_EBX        = 0x20
NEXT_ECX        = 0x24
NEXT_EDX        = 0x28
NEXT_ESP        = 0x2C
NEXT_EBP        = 0x30
NEXT_ESI        = 0x34
NEXT_EDI        = 0x38                

.global switch_to_same_privilege       
switch_to_same_privilege:
        pushl   %ebp
        movl    %esp, %ebp
/*        
        movl    NEXT_COUNT(%ebp), %eax
        cmp     $0, %eax
        jz      1f
        pushl   IRET_EFLAGS(%ebp)        
        pushl   IRET_ESP(%ebp)      
        pushl   IRET_EIP(%ebp)
        jmp     2f
1:
        pushf
        movl    (%esp), %eax
        movl    %eax, NEXT_EFLAGS(%ebp)
        pushl   NEXT_ESP(%ebp)
        pushl   PREV_EIP(%ebp)
2:                      
        pushl   PREV(%ebp)
        call    set_context
        addl    $12, %esp
*/
        movl    SAVE_ESP(%ebp), %esp
        movl    NEXT_CR3(%ebp), %eax
        movl    %eax, %cr3

        pushl   NEXT_EFLAGS(%ebp)
        pushl   NEXT_CS(%ebp)
        pushl   NEXT_EIP(%ebp)
        movw    $__KERNEL_DS, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %fs
        movw    %ax, %gs
        movl    NEXT_EBX(%ebp), %ebx
        movl    NEXT_ECX(%ebp), %ecx
        movl    NEXT_EDX(%ebp), %edx
        movl    NEXT_ESI(%ebp), %esi
        movl    NEXT_EDI(%ebp), %edi
        movl    NEXT_EAX(%ebp), %eax        
        movl    NEXT_EBP(%ebp), %ebp
        sti
        iret

        
/*                
NEXT_CS         = 0x8
NEXT_DS         = 0xC       
NEXT_ESP        = 0x10
NEXT_CR3        = 0x14
NEXT_EIP        = 0x18
PREV            = 0x1C      
NEXT_COUNT      = 0x20
PREV_EIP        = 0x24                      

IRET_EIP        = 0x80                      
IRET_CS         = 0x84
IRET_EFLAGS     = 0x88      
IRET_ESP        = 0x8C
IRET_SS         = 0x90              

.global switch_to       
switch_to:
        pushl   %ebp
        movl    %esp, %ebp
        movl    NEXT_COUNT(%ebp), %eax
        cmp     $0, %eax
        jz      1f
        pushl   IRET_ESP(%ebp)      
        pushl   IRET_EIP(%ebp)
        jmp     2f
1:
        pushl   NEXT_ESP(%ebp)
        pushl   PREV_EIP(%ebp)
2:                      
        pushl   PREV(%ebp)
        call    set_context
        addl    $12, %esp
        movl    NEXT_CR3(%ebp), %eax
        movl    %eax, %cr3

        movl    NEXT_CS(%ebp), %eax
        movl    %eax, IRET_CS(%ebp)
        movl    NEXT_DS(%ebp), %eax
        movl    %eax, IRET_SS(%ebp)
        movl    NEXT_ESP(%ebp), %eax
        movl    %eax, IRET_ESP(%ebp)        
        movl    NEXT_EIP(%ebp), %eax
        movl    %eax, IRET_EIP(%ebp)
        popl    %ebp
        ret
*/

