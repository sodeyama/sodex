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
#include <memory_layout.h>
#include <rs232c.h>
#include <string.h>

/* Forward declarations to avoid pulling in memory.h globals */
PUBLIC void* palloc(u_int32_t size);
PUBLIC int32_t pfree(void* ptr);
PUBLIC void* kalloc(u_int32_t size);
PUBLIC int32_t kfree(void* ptr);
PUBLIC void* aalloc(u_int32_t size, u_int8_t align_bit);
PUBLIC int32_t afree(void* ptr);

PRIVATE void delete_first_page();
PRIVATE u_int32_t get_need_blocks(size_t need_size, size_t size);
PRIVATE u_int32_t kernel_extra_pdes[PAGE_DIR_SIZE];
PRIVATE u_int32_t get_kernel_pde_end();

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
  u_int32_t kernel_pde_end = get_kernel_pde_end();
  int pos;
  for (pos = 0; pos < PAGE_DIR_SIZE; pos++) {
    if (pos >= PGDIR_KERNEL_START && pos < kernel_pde_end) {
      pte = (pos - PGDIR_KERNEL_START) * PSE_PAGE_SIZE;
      pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US|PAGE_PSE|PAGE_GLOBAL);
      pg_dir[pos] = pte;
    } else {
      pg_dir[pos] = 0;
    }
  }

  for (pos = 0; pos < PAGE_DIR_SIZE; pos++) {
    if (kernel_extra_pdes[pos] != 0)
      pg_dir[pos] = kernel_extra_pdes[pos];
  }
}

PRIVATE u_int32_t get_kernel_pde_end()
{
  const memory_layout_policy_t* layout;

  if (memory_layout_is_initialized() == 0)
    return PGDIR_KERNEL_END;

  layout = memory_get_layout_policy();
  if (layout->kernel_pde_end <= PGDIR_KERNEL_START ||
      layout->kernel_pde_end > PAGE_DIR_SIZE)
    return PGDIR_KERNEL_END;

  return layout->kernel_pde_end;
}

PUBLIC void* create_process_page(u_int32_t* pg_dir, size_t size)
{
  u_int32_t need_blocks = ((size&~(BLOCK_SIZE-1)) == 0) ? 
    ((size&~(BLOCK_SIZE-1))>>12) + 1 : ((size&~(BLOCK_SIZE-1))>>12);
  void* phy_proc_mem = palloc(size);

  u_int32_t need_pgdir_blocks = (need_blocks + 1023) / 1024;

  u_int32_t pte;
  int pos;
  for (pos = 0; pos < need_pgdir_blocks; pos++) {
    u_int32_t pg_tbl_phys = (u_int32_t)palloc(BLOCK_SIZE);
    u_int32_t* pg_tbl;
    if (pg_tbl_phys == 0)
      return NULL;
    pg_tbl = (u_int32_t *)(pg_tbl_phys + __PAGE_OFFSET);
    memset(pg_tbl, 0, BLOCK_SIZE);
    int i;
    for (i=0; i<1024; i++) {
      if (pos*1024+i >= need_blocks) break;
      pte = phy_proc_mem + (pos * 1024 + i)*BLOCK_SIZE;
      pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
      if (pos == 0 && i == 0)
        pte |= PAGE_ALLOC_HEAD;
      else
        pte |= PAGE_ALLOC_CONT;
      pg_tbl[i] = pte;
    }
    pte = pg_tbl_phys;
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

  u_int32_t pte;
  u_int32_t mapped_blocks = 0;
  int pgdir_start_pos, pgtbl_start_pos;
  pgdir_start_pos = new_start_vaddr/(BLOCK_SIZE*1024);
  int pd_pos;
  pgtbl_start_pos = (new_start_vaddr%(BLOCK_SIZE*1024))/BLOCK_SIZE;

  for (pd_pos = pgdir_start_pos; mapped_blocks < need_blocks; pd_pos++) {
    u_int32_t* pg_tbl;
    int pt_start_pos;
    int pt_end_pos;
    int pt_pos;
    if (pg_dir[pd_pos] == 0) {
      u_int32_t pg_tbl_phys = (u_int32_t)palloc(BLOCK_SIZE);
      if (pg_tbl_phys == 0) {
        _kprintf("%s: pg_tbl kalloc error\n", __func__);
        return NULL;
      }
      pg_tbl = (u_int32_t *)(pg_tbl_phys + __PAGE_OFFSET);
      memset(pg_tbl, 0, BLOCK_SIZE);
    } else {
      pg_tbl = pg_dir[pd_pos]&~(BLOCK_SIZE-1);
      pg_tbl = (u_int32_t*)((u_int32_t)pg_tbl + (__PAGE_OFFSET));
    }

    pt_start_pos = (pd_pos == pgdir_start_pos) ? pgtbl_start_pos : 0;
    pt_end_pos = pt_start_pos + (need_blocks - mapped_blocks);
    if (pt_end_pos > 1024)
      pt_end_pos = 1024;

    for (pt_pos = pt_start_pos; pt_pos < pt_end_pos; pt_pos++) {
      pte = phy_proc_mem + mapped_blocks*BLOCK_SIZE;
      pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
      if (mapped_blocks == 0)
        pte |= PAGE_ALLOC_HEAD;
      else
        pte |= PAGE_ALLOC_CONT;
      pg_tbl[pt_pos] = pte;
      mapped_blocks++;
    }
    pte = (u_int32_t)pg_tbl - __PAGE_OFFSET;
    pte |= (PAGE_PRESENT|PAGE_RW|PAGE_US);
    pg_dir[pd_pos] = pte;
  }

  return (void*)new_start_vaddr;
}

PUBLIC int clone_process_pages(u_int32_t *dst_pg_dir, u_int32_t *src_pg_dir)
{
  int pd_pos;

  if (dst_pg_dir == NULL || src_pg_dir == NULL)
    return -1;

  for (pd_pos = 0; pd_pos < PGDIR_KERNEL_START; pd_pos++) {
    u_int32_t src_pde = src_pg_dir[pd_pos];
    u_int32_t *src_pg_tbl;
    u_int32_t *dst_pg_tbl;
    u_int32_t dst_pde;
    int pt_pos;

    if (src_pde == 0)
      continue;
    if (src_pde & PAGE_PSE)
      return -1;

    src_pg_tbl = (u_int32_t *)((src_pde & ~(BLOCK_SIZE - 1)) + __PAGE_OFFSET);
    {
      u_int32_t dst_pg_tbl_phys = (u_int32_t)palloc(BLOCK_SIZE);
      if (dst_pg_tbl_phys == 0)
        goto fail;
      dst_pg_tbl = (u_int32_t *)(dst_pg_tbl_phys + __PAGE_OFFSET);
    }
    memset(dst_pg_tbl, 0, BLOCK_SIZE);

    for (pt_pos = 0; pt_pos < 1024; ) {
      u_int32_t src_pte = src_pg_tbl[pt_pos];

      if ((src_pte & PAGE_PRESENT) == 0) {
        pt_pos++;
        continue;
      }

      {
        int run_len = 1;
        u_int32_t dst_run_phys;
        int run_pos;

        while (pt_pos + run_len < 1024 &&
               (src_pg_tbl[pt_pos + run_len] & PAGE_PRESENT) != 0) {
          run_len++;
        }

        dst_run_phys = (u_int32_t)palloc((u_int32_t)(run_len * BLOCK_SIZE));
        if (dst_run_phys == 0)
          goto fail;

        for (run_pos = 0; run_pos < run_len; run_pos++) {
          u_int32_t run_src_pte = src_pg_tbl[pt_pos + run_pos];
          u_int32_t dst_phys = dst_run_phys + (run_pos * BLOCK_SIZE);

          memcpy((void *)(dst_phys + __PAGE_OFFSET),
                 (void *)((run_src_pte & ~(BLOCK_SIZE - 1)) + __PAGE_OFFSET),
                 BLOCK_SIZE);
          dst_pg_tbl[pt_pos + run_pos] =
              dst_phys |
              ((run_src_pte & (BLOCK_SIZE - 1)) &
               ~(PAGE_ALLOC_HEAD | PAGE_ALLOC_CONT)) |
              (run_pos == 0 ? PAGE_ALLOC_HEAD : PAGE_ALLOC_CONT);
        }

        pt_pos += run_len;
      }
    }

    dst_pde = ((u_int32_t)dst_pg_tbl - __PAGE_OFFSET);
    dst_pde |= (src_pde & (BLOCK_SIZE - 1));
    dst_pg_dir[pd_pos] = dst_pde;
  }

  return 0;

fail:
  free_process_pages(dst_pg_dir);
  return -1;
}

PUBLIC void free_process_pages(u_int32_t *pg_dir)
{
  int pd_pos;
  com1_printf("AUDIT free_pages_begin pg=%x\r\n", pg_dir);
  for (pd_pos = 0; pd_pos < PGDIR_KERNEL_START; pd_pos++) {
    u_int32_t pde = pg_dir[pd_pos];
    u_int32_t *pg_tbl;
    int pt_pos;

    if (pde == 0)
      continue;
    if (pde & PAGE_PSE)
      continue;

    pg_tbl = (u_int32_t *)(((pde & ~(BLOCK_SIZE - 1))) + __PAGE_OFFSET);

    for (pt_pos = 0; pt_pos < 1024; pt_pos++) {
      u_int32_t pte = pg_tbl[pt_pos];
      if (pte & PAGE_PRESENT) {
        void *phys = (void *)(pte & ~(BLOCK_SIZE - 1));
        if ((pte & PAGE_ALLOC_CONT) == 0)
          pfree(phys);
      }
    }

    com1_printf("AUDIT free_pages_pde pd=%x tbl=%x\r\n", pd_pos, pg_tbl);
    pfree((void *)(pde & ~(BLOCK_SIZE - 1)));
    pg_dir[pd_pos] = 0;
  }
  com1_printf("AUDIT free_pages_done pg=%x\r\n", pg_dir);
}

PUBLIC void pg_set_kernel_4m_page(u_int32_t virt_addr, u_int32_t phys_addr,
                                  u_int32_t flags)
{
  u_int32_t pos;
  u_int32_t pte;

  pos = virt_addr / PSE_PAGE_SIZE;
  if (pos >= PAGE_DIR_SIZE)
    return;

  pte = (phys_addr & ~(PSE_PAGE_SIZE - 1));
  pte |= (flags | PAGE_PSE);
  kernel_extra_pdes[pos] = pte;
  pg_dir[pos] = pte;
  pg_flush_tlb();
}

PRIVATE u_int32_t get_need_blocks(size_t need_size, size_t size)
{
  /* Round up to page boundary: ceil(need_size / BLOCK_SIZE) */
  return (need_size + BLOCK_SIZE - 1) >> BLOCK_BITS;
}
