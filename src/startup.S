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
 * Initialize the page tables: pg_tbl1, pg_tbl2, pg_tbl3 (4MB x 3 = 12MB)
 */
        movl    $(1024*3-1), %ecx
        movl    $(first_pg_tbl1-__PAGE_OFFSET), %edi
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
 * Initialize the page directory (identity map + higher half)
 */
        /* Identity map: PDE[0]=tbl1, PDE[1]=tbl2, PDE[2]=tbl3 */
        movl    $(first_pg_tbl1-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax
        addl    $0x7, %eax
        movl    $(first_pg_dir-__PAGE_OFFSET), %edi
        stosl
        movl    $(first_pg_tbl2-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax
        addl    $0x7, %eax
        stosl
        movl    $(first_pg_tbl3-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax
        addl    $0x7, %eax
        stosl

        /* Higher half: PDE[768]=tbl1, PDE[769]=tbl2, PDE[770]=tbl3 */
        movl    $(first_pg_tbl1-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax
        addl    $0x7, %eax
        movl    $(first_pg_dir-__PAGE_OFFSET+768*4), %edi
        stosl
        movl    $(first_pg_tbl2-__PAGE_OFFSET), %eax
        andl    $0xfffff000, %eax
        addl    $0x7, %eax
        stosl
        movl    $(first_pg_tbl3-__PAGE_OFFSET), %eax
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

        movl    $gdt, %eax
        movl    %eax, (gdtr+2)
        lgdt    gdtr

        movl    $0xC009D000, %esp

/*
 * Zero BSS section
 */
        movl    $__bss_start, %edi
        movl    $__bss_end, %ecx
        subl    %edi, %ecx
        shrl    $2, %ecx
        xorl    %eax, %eax
        rep     stosl

        jmp     start_kernel
        
forever:
        jmp     forever

        
.section ".bss.page_aligned","aw",@nobits
.global first_pg_dir
first_pg_dir:
  .space 1024*4
.global first_pg_tbl1
first_pg_tbl1:
  .space 1024*4
.global first_pg_tbl2
first_pg_tbl2:
  .space 1024*4
.global first_pg_tbl3
first_pg_tbl3:
  .space 1024*4

.section ".data"
.global gdtr
gdtr:   .word   gdt_end - gdt - 1
        .long   0       # filled in code
.global gdt
gdt:
gdt00h:                 #It's dummy for cpu
        .word   0       #selector 00h
        .word   0
        .byte   0
        .byte   0
        .byte   0
        .byte   0
gdt08h:                 #code segment DPL=0
        .word   0xFFFF  #selector 08h
        .word   0
        .byte   0
        .byte   0x9A    # P=1, DPL=0, S=1, TYPE=A(code,exec/read)
        .byte   0xCF    # G=1, D=1
        .byte   0
gdt10h:                 #data segment DPL=0
        .word   0xFFFF  #selector 10h
        .word   0
        .byte   0
        .byte   0x92    # P=1, DPL=0, S=1, TYPE=2(data,read/write)
        .byte   0xCF    # G=1, D=1
        .byte   0
gdt18h:                 #reserved (padding for selector 18h)
        .word   0
        .word   0
        .byte   0
        .byte   0
        .byte   0
        .byte   0
gdt20h:                 #code segment DPL=3 (user)
        .word   0xFFFF  #selector 20h | RPL=3 = 0x23
        .word   0
        .byte   0
        .byte   0xFA    # P=1, DPL=3, S=1, TYPE=A(code,exec/read)
        .byte   0xCF    # G=1, D=1
        .byte   0
gdt28h:                 #data segment DPL=3 (user)
        .word   0xFFFF  #selector 28h | RPL=3 = 0x2B
        .word   0
        .byte   0
        .byte   0xF2    # P=1, DPL=3, S=1, TYPE=2(data,read/write)
        .byte   0xCF    # G=1, D=1
        .byte   0
        .space  8*122   # reserve space for up to 128 GDT entries total
gdt_end:                

