/*
 *  @File        bootusb.S
 *  @Brief       The initial boot section of Sodex from usb
 *  
 *  @Author      Sodex
 *  @Revision    0.0.3
 *  @License     BSD License
 *  @Date        create: 2009/08/09
 *      
 *  Copyright (C) 2007 Sodex
 */
        
BOOTSEG          = 0x07C0
INITSEG          = 0x9000
KERNEL_SEG       = 0x0100       
KERNEL_STACK_SEG = 0x8000

READ_MAX_SECTS   = 0x40

.code16
.text

.macro  print   str
        movw    \str, %si
        call    printstr
.endm

.macro  print_w word
        pushw   %ax
        pushw   %bx
        movw    \word, %bx        
        movb    %bh, %al
        call    print_char
        movb    %bl, %al
        call    print_char
        popw    %bx        
        popw    %ax
.endm        

.global _start
_start: 
        jmpl    $BOOTSEG, $start2
        
start2:
        xorw    %si, %si
        xorw    %di, %di
        movw    $BOOTSEG, %ax
        movw    %ax, %ds
        movw    $INITSEG, %ax
        movw    %ax, %es
        movw    $0x100, %cx
        cld
        rep     movsw
        jmpl    $INITSEG, $next

next:
        movw    %ax, %ds
        movw    $KERNEL_STACK_SEG, %ax
        movw    %ax, %ss
        xorw    %sp, %sp
        
        movb    (drive), %dl
        cmp     $0x80, %dl
        jb      error
        movb    $0x41, %ah
        movw    $0x55aa, %bx
        int     $0x13
        jc      error

# read middle boot
        movl    $64 , (lba)
        movw    $4, (sector_nums)
        movw    $INITSEG, (buf_segment)
        movw    $0x200, (buf_offset)        
        call    read_disk
        print   $read_success

# read kernel
        movl    $72 , (lba)
        movw    $KERNEL_SECTS, (sector_nums)
        movw    $KERNEL_SEG, (buf_segment)
        movw    $0x0, (buf_offset)        
        call    read_disk
        print   $read_success
        
        ljmp    $0x9000, $0x0200        

read_disk:
        movw    (sector_nums), %cx
        cmp     $READ_MAX_SECTS, %cx
        jb      read_init_end
        movw    $READ_MAX_SECTS, (sector_nums)
        movw    $READ_MAX_SECTS, %bx
        shl     $5, %bx                     # = READ_MAX_SECTS * 512 >> 4
        movw    %bx, (sector_seg_size)      # sector_seg_size is 0x800.
read_init_end:
        movb    (drive), %dl        
        movw    $INITSEG, %bx
        movw    %bx, %ds
        movw    $read_param, %si
read_disk_itr:
        movw    $0x4200, %ax
        int     $0x13
        jc      read_error
        cmp     $0x0, %cx
        jz      read_disk_end
        subw    (sector_nums), %cx
        cmp     $0x0, %cx
        jz      read_disk_end
        cmp     $READ_MAX_SECTS, %cx
        jae     read_over_max
        movw    %cx, (sector_nums)           
        jmp     read_param_set
read_over_max:
        movw    $READ_MAX_SECTS, (sector_nums)        
read_param_set:
        addl    $READ_MAX_SECTS, (lba)
        adcl    $0x0, (lba + 4)
        movw    (sector_seg_size), %bx
        addw    %bx, (buf_segment)           # buf seg:offset += 0x8000
        jmp     read_disk_itr
read_disk_end:  
        ret
        

error:
        print   $error_mes
        jmp     loop

read_error:
        print   $read_error_mes
        jmp     loop

loop:
        jmp     loop

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


drive:  .byte 0x80

read_param:
len:            .word 0x10
sector_nums:    .word 0x0
buf_offset:     .word 0x0
buf_segment:    .word 0x0
lba:            .long 0x0, 0x0

sector_seg_size:    .word 0x0

read_success:       
        .ascii  "read success\r\n\0"        
over_80h:       
        .ascii  "over80h\r\n\0"
under_80h:       
        .ascii  "under80h\r\n\0"        
error_mes: 
        .ascii  "error\r\n\0"
read_error_mes: 
        .ascii  "read error\r\n\0"
crlf: 
        .ascii  "\r\n\0"                        

        .org 510
        .word 0xAA55        
