/*
 *  @File        page.c
 *  @Brief       manage paging functions
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/06/28  update: 2007/06/28
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <sys/types.h>
#include <page.h>

PRIVATE void delete_first_page();
PRIVATE u_int32_t get_need_blocks(size_t need_size, size_t size);

PUBLIC void init_paging()
{
  delete_first_page();
  create_kernel_page(pg_dir);
  pg_enable_pse();
  pg_load_cr3(pg_dir);
}

PRIVATE void delete_first_page()
{
  first_pg_dir[0] = 0;
  first_pg_dir[1] = 0;
}

PUBLIC void create_kernel_page(u_int32_t* pg_dir)
{
  u_int32_t pte;
  int pos;
  for (pos = 0; pos < PGDIR_KERNEL_END; pos++) {
    if (pos < PGDIR_KERNEL_START) {
      pg_dir[pos] = 0;
    } else {
      pte = (pos - PGDIR_KERNEL_START) * PSE_PAGE_SIZE;
      pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US|PAGE_PSE|PAGE_GLOBAL);
      pg_dir[pos] = pte;
    }
  }
}

PUBLIC void* create_process_page(u_int32_t* pg_dir, size_t size)
{
  u_int32_t need_blocks = ((size&~(BLOCK_SIZE-1)) == 0) ? 
    ((size&~(BLOCK_SIZE-1))>>12) + 1 : ((size&~(BLOCK_SIZE-1))>>12);
  void* phy_proc_mem = palloc(size);

  u_int32_t need_pgdir_blocks = ((need_blocks/1024) == 0) ?
    (need_blocks/1024)+1 : (need_blocks/1024);

  u_int32_t pte;
  int pos;
  for (pos = 0; pos < need_pgdir_blocks; pos++) {
    u_int32_t* pg_tbl = kalloc(BLOCK_SIZE*2);
    pg_tbl = ((u_int32_t)pg_tbl & ~(BLOCK_SIZE-1)) + BLOCK_SIZE;
    int i;
    for (i=0; i<1024; i++) {
      if (pos*1024+i >= need_blocks) break;
      pte = phy_proc_mem + (pos * 1024 + i)*BLOCK_SIZE;
      pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
      pg_tbl[i] = pte;
    }
    pte = (u_int32_t)pg_tbl - __PAGE_OFFSET;
    pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
    pg_dir[pos] = pte;
  }
  void* linear_proc_mem = (void*)0;
  return linear_proc_mem;
}

/* Don't set the address which is across the other memory address */
PUBLIC void* set_process_page(u_int32_t* pg_dir, u_int32_t start_vaddr,
                              size_t size)
{
  u_int32_t new_start_vaddr = (start_vaddr&~(BLOCK_SIZE-1));
  size_t need_size = start_vaddr - new_start_vaddr + size;
  u_int32_t need_blocks = get_need_blocks(need_size, size);

  void* phy_proc_mem = palloc(need_blocks*BLOCK_SIZE);
  if (phy_proc_mem == NULL) {
    _kprintf("%s: phy_proc_mem palloc error\n", __func__);
    return NULL;
  }

  u_int32_t need_pgdir_blocks = ((need_blocks/1024) == 0) ?
    (need_blocks/1024)+1 : (need_blocks/1024);

  u_int32_t pte;
  int pgdir_start_pos, pgdir_end_pos, pgtbl_start_pos, pgtbl_end_pos;
  pgdir_start_pos = new_start_vaddr/(BLOCK_SIZE*1024);
  pgdir_end_pos = pgdir_start_pos + need_pgdir_blocks;
  int pd_pos;
  for (pd_pos = pgdir_start_pos; pd_pos < pgdir_end_pos; pd_pos++) {
    u_int32_t* pg_tbl;
    if (pg_dir[pd_pos] == NULL) {
      pg_tbl = kalloc(BLOCK_SIZE*2);
      pg_tbl = ((u_int32_t)pg_tbl & ~(BLOCK_SIZE-1)) + BLOCK_SIZE;
    } else {
      pg_tbl = pg_dir[pd_pos]&~(BLOCK_SIZE-1);
      pg_tbl = (u_int32_t*)((u_int32_t)pg_tbl + (__PAGE_OFFSET));
    }
    pgtbl_start_pos = (new_start_vaddr%(BLOCK_SIZE*1024))/BLOCK_SIZE;
    if (pd_pos == pgdir_end_pos - 1)
      pgtbl_end_pos = pgtbl_start_pos + need_blocks;
    else
      pgtbl_end_pos = 1024;
    int pt_pos;
    for (pt_pos = pgtbl_start_pos; pt_pos < pgtbl_end_pos; pt_pos++) {
      pte = phy_proc_mem + ((pd_pos-pgdir_start_pos) * 1024 +
                            (pt_pos-pgtbl_start_pos))*BLOCK_SIZE;
      pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
      pg_tbl[pt_pos] = pte;
    }
    pte = (u_int32_t)pg_tbl - __PAGE_OFFSET;
    pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
    pg_dir[pd_pos] = pte;
  }

  return (void*)new_start_vaddr;
}

PRIVATE u_int32_t get_need_blocks(size_t need_size, size_t size)
{
  size_t soiled_size = (need_size&~(BLOCK_SIZE-1));
  size_t remained_size = (need_size&(BLOCK_SIZE-1));
  //_kprintf("need:%x soiled:%x remianed:%x\n", need_size, soiled_size, remained_size);
  u_int32_t need_blocks;
  if (soiled_size == 0) {
    need_blocks = (soiled_size>>BLOCK_BITS) + 1;
  } else {
    if (remained_size != 0)
      need_blocks = (soiled_size>>BLOCK_BITS) + 1;
    else
      need_blocks = (soiled_size>>BLOCK_BITS);
  }
  //_kprintf("need_blocks:%x\n", need_blocks);
  return need_blocks;
}
