/*
 *  @File        startup.S
 *  @Brief       This file set the temporary page directory and page table.
 *
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae:  2007/06/26  update:     2007/06/26
 *
 *  Copyright (C) 2007 Sodex
 */
        
.code32
.text

__BOOT_CS     = 0x08        
__PAGE_OFFSET = 0xC0000000      
        
.global startup
startup:
/*
 * Initialize the page table which indicate the pg_tbl1 and pg_tbl2(4MBx2)
 */                     
        movl    $(1024*2-1), %ecx
        movl    $(first_pg_tbl1-__PAGE_OFFSET), %edi
        andl    $0xfffff000, %eax       
        movl    $0x7, %eax
1:      
        stosl
        cmpl    $0, %ecx
        jz      2f
        addl    $0x1000, %eax
        dec     %ecx
        jmp     1b
2:
/*
 * Initialize the page directory
 */                     
        movl    $(first_pg_tbl1-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax
        addl    $0x7, %eax
        movl    $(first_pg_dir-__PAGE_OFFSET), %edi
        stosl
        movl    $(first_pg_tbl2-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax       
        addl    $0x7, %eax
        stosl

        movl    $(first_pg_tbl1-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax       
        addl    $0x7, %eax
        movl    $(first_pg_dir-__PAGE_OFFSET+768*4), %edi
        stosl
        movl    $(first_pg_tbl2-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax       
        addl    $0x7, %eax
        stosl       

/*
 * Enable paging
 */
        movl    $(first_pg_dir-__PAGE_OFFSET), %eax
        movl    %eax, %cr3
        movl    %cr0, %eax
        orl     $0x80000000, %eax
        movl    %eax, %cr0

        movl    $1f, %eax
        jmp     *%eax
1:

        xorl    %eax, %eax
        movw    %ds, %ax
        shll    $4, %eax
        addl    $gdt, %eax
        movl    %eax, (gdtr+2)
        lgdt    gdtr
                
        movl    $0xC009C000, %esp
        jmp     start_kernel
        
forever:
        jmp     forever

        
.section ".bss.page_aligned","w"
.global first_pg_dir
first_pg_dir:
  .fill 1024,4,0
.global first_pg_tbl1
first_pg_tbl1:
  .fill 1024,4,0
.global first_pg_tbl2
first_pg_tbl2:
  .fill 1024,4,0

.global gdtr
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
gdt_end:                

