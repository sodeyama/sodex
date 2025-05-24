/*
 *  @File        page_asm.S
 *  @Brief       The functions which is used at paging
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/06/28  update: 2007/06/28
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <page.h>

__PAGE_OFFSET = 0xC0000000              
                    
.global pg_enable_pse
pg_enable_pse:
        movl    %cr4, %eax
        orl     $0x10, %eax
        movl    %eax, %cr4              
        ret

.global pg_disable_pse
pg_disable_pse:
        movl    %cr4, %eax
        andl    $0xffffffef, %eax
        movl    %eax, %cr4
        ret             
        
.global pg_enable_paging
pg_enable_paging:       
        movl    %cr0, %eax
        orl     $0x80000000, %eax
        movl    %eax, %cr0
        ret
        
.global pg_disable_paging
pg_disable_paging:      
        movl    %cr0, %eax
        andl    $0x7fffffff, %eax
        movl    %eax, %cr0
        ret

/*
 * void pg_load_cr3(u_int32_t* pg_dir)      
 */             
.global pg_load_cr3
pg_load_cr3:
        pushl   %ebp
        movl    %esp, %ebp
        movl    8(%ebp), %eax
        subl    $__PAGE_OFFSET, %eax
        movl    %eax, %cr3
        popl    %ebp
        ret

/*
 * u_int32_t pg_get_cr3()
 */                        
.global pg_get_cr3
pg_get_cr3:             
        movl    %cr3, %eax
        ret

.global pg_flush_tlb
pg_flush_tlb:   
        movl    %cr3, %eax
        movl    %eax, %cr3
        ret
		