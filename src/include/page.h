#ifndef _PAGE_H
#define _PAGE_H

#include <sodex/const.h>
#include <ld/page_linker.h>

#define PAGE_DIR_SIZE		 1024
#define PGDIR_KERNEL_START	 768
#define PGDIR_KERNEL_END  	 (PGDIR_KERNEL_START+64) // 64MB
#define PSE_PAGE_SIZE		 4194304 // 4MB Paging Size Extensions
#define PAGE_SIZE			 4096	 // 4KB

#define BLOCK_BITS           12

//flag
#define PAGE_PRESENT    1
#define PAGE_RW         2
#define PAGE_US         4
#define PAGE_PWT        8
#define PAGE_PCD        16
#define PAGE_ACCESS     32
#define PAGE_DIRTY      64
#define PAGE_PSE        128
#define PAGE_GLOBAL     256
        

PUBLIC u_int32_t first_pg_dir[PAGE_DIR_SIZE];
PUBLIC u_int32_t pg_dir[PAGE_DIR_SIZE];

PUBLIC void create_kernel_page(u_int32_t* pg_dir);
PUBLIC void* create_process_page(u_int32_t* pg_dir, size_t size);
PUBLIC void* set_process_page(u_int32_t* pg_dir, u_int32_t start_vaddr,
							  size_t size);
PUBLIC void init_paging();
PUBLIC void pg_enable_pse();
PUBLIC void pg_disable_pse();
PUBLIC void pg_enable_paging();
PUBLIC void pg_disable_paging();
PUBLIC void pg_load_cr3(u_int32_t* pg_dir);
PUBLIC u_int32_t pg_get_cr3();
PUBLIC void pg_flush_tbl();

#endif 
