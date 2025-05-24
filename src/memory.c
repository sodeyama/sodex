/*
 *  @File        memory.c
 *  @Brief       manage the memory for kernel
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/25  update: 2007/07/10  
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <ld/page_linker.h>
#include <memory.h>
#include <vga.h>

#define MEMDEBUG 1

PRIVATE MemHole* _search_used_mhole(void* ptr, MemHole* use_list);
PRIVATE MemHole* _search_used_align_mhole(void* ptr, MemHole* use_list);
PRIVATE int _internal_common_merge(MemHole* mem);
PRIVATE void merge_hole(MemHole* memfrom, MemHole* memto);
PRIVATE MemHole* new_mhole(MemHole* hole_list);
PRIVATE void init_kmem();
PRIVATE void init_pmem();
PRIVATE void used_check(MemHole* mem);

PUBLIC void init_mem()
{
  init_kmem();
  init_pmem();
}

PRIVATE void init_kmem()
{
  int i;
  for (i = 0; i < MAX_MHOLES; ++i) {
    if (i == MAX_MHOLES - 1)
      memhole[i].next = &mhole_list;
    else
      memhole[i].next = &memhole[i+1];

    if (i == 0)
      memhole[i].prev = &mhole_list;
    else
      memhole[i].prev = &memhole[i-1];
  }
  mhole_list.next = &memhole[0];
  mhole_list.prev = &memhole[MAX_MHOLES-1];
  memhole[0].prev = &mhole_list;
  memhole[MAX_MHOLES-1].next = &mhole_list;

  mhole_list.base = 0;
  mhole_list.size = 0;
  mfree_list.base = 0;
  mfree_list.size = 0;
  muse_list.base = 0;
  muse_list.size = 0;

  //initial muse_list setting
  muse_list.next = &muse_list;
  muse_list.prev = &muse_list;

  //initial mfree_list setting
  mfree_list.next = &mfree_list;
  mfree_list.prev = &mfree_list;

  MemHole* mem = mhole_list.next;
  MHOLE_REMOVE(mem);
  MHOLE_INSERT_HEAD(mem, mfree_list);
  mfree_list.next->base = KERNEL_MEMBASE;
  mfree_list.next->size = KERNEL_MEMEND - KERNEL_MEMBASE;
}

PRIVATE MemHole* _search_used_mhole(void* ptr, MemHole* use_list)
{
  MemHole* p;
  for (p = use_list->next; p != use_list; p = p->next) {
    if (p->base == (u_int32_t)ptr)
      return p;
  }
  return NULL;
}

PUBLIC int32_t kfree(void* ptr)
{
  MemHole* mem = _search_used_mhole(ptr, &muse_list);
  if (mem == NULL) {
    _kprintf("kfree:invalid memory pointer >> %x\n", ptr);
    return KFREE_FAIL_NOT_MHOLE;
  }
  return _internal_common_merge(mem);
}

PRIVATE void merge_hole(MemHole* memfrom, MemHole* memto)
{
  MemHole *i, *j, *next;
  for (i = memfrom->next; i != memto->next; i = i->next) {
    memfrom->size += i->size;
  }

  i = memfrom->next;
  next = memto->next;
  while (i != next) {
    j = i->next;
    MHOLE_REMOVE(i);
    MHOLE_CLEAN(i);
    MHOLE_INSERT_HEAD(i, mhole_list);
    i = j;
  }
}

PUBLIC void* kalloc(u_int32_t size)
{
  MemHole *i, *new;
  for (i = mfree_list.next; i != &mfree_list; i = i->next) {
    if (i->size > size) {
#ifdef MEMDEBUG
      used_check(i);
#endif
      if (i->size - size > MIN_MEMSIZE) {
        new = new_mhole(&mhole_list);
        if (new == NULL)
          _kputs("new_mhole() error\n");
        new->base = i->base;
        new->size = size;
        i->base += size;
        i->size -= size;
        MHOLE_INSERT_HEAD(new, muse_list);
        return (void*)new->base;
      } else {
        MHOLE_REMOVE(i);
        MHOLE_INSERT_HEAD(i, muse_list);
        return (void*)i->base;
      }
    } else if (i->size == size) {
#ifdef MEMDEBUG
      used_check(i);
#endif
      MHOLE_REMOVE(i);
      MHOLE_INSERT_HEAD(i, muse_list);
      return (void*)i->base;
    }
  }
  return NULL;
}

PRIVATE MemHole* _search_used_align_mhole(void* ptr, MemHole* use_list)
{
  MemHole* p;
  for (p = use_list->next; p != use_list; p = p->next) {
    if (p->align_base == (u_int32_t)ptr)
      return p;
  }
  return NULL;
}

/* align free */
PUBLIC int32_t afree(void* ptr)
{
  MemHole* mem = _search_used_align_mhole(ptr, &muse_list);
  if (mem == NULL) {
    _kprintf("kfree:invalid memory pointer >> %x\n", ptr);
    return KFREE_FAIL_NOT_MHOLE;
  }
  return _internal_common_merge(mem);
}

/* align alloc */
PUBLIC void* aalloc(u_int32_t size, u_int8_t align_bit)
{
  if (align_bit > 32) {
    return NULL;
  }
  u_int32_t block = 1 << align_bit;
  u_int32_t block_size = (size&(block-1)) == 0 ?
    size : (size&~(block-1)) + block;

  MemHole *i, *new;
  for (i = mfree_list.next; i != &mfree_list; i = i->next) {
    u_int32_t align_base = ((u_int32_t)i->base&(block-1)) == 0 ?
      i->base : ((u_int32_t)i->base&~(block-1)) + block;
    u_int32_t alloc_size = align_base + block_size - i->base;
    if (i->size > alloc_size) {
      if (i->size - alloc_size > MIN_MEMSIZE) {
#ifdef MEMDEBUG
        used_check(i);
#endif
        new = new_mhole(&mhole_list);
        if (new == NULL)
          _kputs("new_mhole() error\n");
        new->base = i->base;
        new->align_base = align_base;
        new->size = alloc_size;
        i->base += alloc_size;
        i->size -= alloc_size;
        MHOLE_INSERT_HEAD(new, muse_list);
        return (void*)new->align_base;
      } else {
#ifdef MEMDEBUG
        used_check(i);
#endif
        MHOLE_REMOVE(i);
        MHOLE_INSERT_HEAD(i, muse_list);
        i->align_base = align_base;
        return (void*)i->align_base;
      }
    } else if (i->size == alloc_size) {
#ifdef MEMDEBUG
      used_check(i);
#endif
      MHOLE_REMOVE(i);
      MHOLE_INSERT_HEAD(i, muse_list);
      i->align_base = align_base;
      return (void*)i->align_base;
    }
  }
  return NULL;
}

PRIVATE int _internal_common_merge(MemHole* mem)
{
  MHOLE_REMOVE(mem);
  // We use the first-fit-algorithm for searching right base.
  MemHole* i;
  for (i = mfree_list.next; i != &mfree_list; i = i->next) {
    if (i->base > mem->base) {
      if (mem->base + mem->size > i->base)
        return KFREE_FAIL_ILLEGAL1;
      MHOLE_INSERT_BEFORE(mem, i);
      break;
    }
  }
  if (i == &mfree_list)
    _kprintf("%s:can't find mfree_list\n", __func__);

  /* Although mem->prev == &mfree_list, mfree_list's base + its size never
   *  equal this mem base, because mfree_list's base and size is always 0
   *  and all mem's base is not 0.
   * Likewise, if mem->next == &mfree_list, this mem's base + size never 
   *  equal mfree_list's base.
   */
  if (mem->prev->base + mem->prev->size == mem->base &&
        mem->base + mem->size == mem->next->base) {
    merge_hole(mem->prev, mem->next);
  } else if (mem->prev->base + mem->prev->size == mem->base) {
    merge_hole(mem->prev, mem);
  } else if (mem->base + mem->size == mem->next->base) {
    merge_hole(mem, mem->next);
  }
  return KFREE_OK;
}

PRIVATE MemHole* new_mhole(MemHole* hole_list)
{
  MemHole* ret;
  
  ret = hole_list->next;
  MHOLE_REMOVE(ret);
  return ret;
}

PUBLIC void _kprint_mem()
{
  u_int32_t count = 0;
  u_int32_t sum = 0;
  u_int32_t max = 0;
  MemHole* i;
  _kprintf("mfree_list:");
  for (i = mfree_list.next; i != &mfree_list; i = i->next) {
    //_kprintf("Memhole base = %x MemHole size = %x\n",
    //       i->base, i->size);
    sum += i->size;
    if (i->base > max) max = i->base;
    count++;
  }
  _kprintf("The num of mfree_list is %x, sum is %x, max is %x\n",
           count, sum, max);

  max = 0;
  sum = 0;
  count = 0;
  _kprintf("muse_list:");
  for (i = muse_list.next; i != &muse_list; i = i->next) {
    //_kprintf("Memhole base = %x MemHole size = %x\n",
    //         i->base, i->size);
    sum += i->size;
    if (i->base > max) max = i->base;
    count++;
  }
  _kprintf("The num of muse_list is %x, sum is %x, max is %x\n",
           count, sum, max);
}


/* For process, alloc physical memory between 32MB and 64MB */
PRIVATE void init_pmem()
{
  int i;
  for (i = 0; i < MAX_PMHOLES; ++i) {
    if (i == MAX_PMHOLES - 1)
      p_memhole[i].next = &p_mhole_list;
    else
      p_memhole[i].next = &p_memhole[i+1];

    if (i == 0)
      p_memhole[i].prev = &p_mhole_list;
    else
      p_memhole[i].prev = &p_memhole[i-1];
  }
  p_mhole_list.next = &p_memhole[0];
  p_mhole_list.prev = &p_memhole[MAX_MHOLES-1];
  p_memhole[0].prev = &p_mhole_list;
  p_memhole[MAX_PMHOLES-1].next = &p_mhole_list;

  p_mhole_list.base = 0;
  p_mhole_list.size = 0;
  p_mfree_list.base = 0;
  p_mfree_list.size = 0;
  p_muse_list.base = 0;
  p_muse_list.size = 0;

  //initial muse_list setting
  p_muse_list.next = &p_muse_list;
  p_muse_list.prev = &p_muse_list;

  //initial mfree_list setting
  p_mfree_list.next = &p_mfree_list;
  p_mfree_list.prev = &p_mfree_list;

  MemHole* mem = p_mhole_list.next;
  MHOLE_REMOVE(mem);
  MHOLE_INSERT_HEAD(mem, p_mfree_list);
  p_mfree_list.next->base = KERNEL_PMEMBASE;
  p_mfree_list.next->size = KERNEL_PMEMEND - KERNEL_PMEMBASE;
}

PUBLIC void* palloc(u_int32_t size)
{
  u_int32_t block_size = ((size&~(BLOCK_SIZE-1)) == 0) ? 
	(size&~(BLOCK_SIZE-1)) + BLOCK_SIZE : (size&~(BLOCK_SIZE-1));

  MemHole *i, *new;
  for (i = p_mfree_list.next; i != &p_mfree_list; i = i->next) {
    if (i->size > block_size) {
	  new = new_mhole(&p_mhole_list);
	  if (new == NULL)
		_kputs("p_new_mhole() error\n");
	  new->base = i->base;
	  new->size = block_size;
	  i->base += block_size;
	  i->size -= block_size;
	  MHOLE_INSERT_HEAD(new, p_muse_list);
	  return (void*)new->base;
    } else if (i->size == block_size) {
      MHOLE_REMOVE(i);
      MHOLE_INSERT_HEAD(i, p_muse_list);
      return (void*)i->base;
    }
  }
  return NULL;
}

PUBLIC int32_t pfree(void* ptr)
{
  MemHole* mem = _search_used_mhole(ptr, &p_muse_list);
  if (mem == NULL) {
    _kprintf("pfree:invalid memory pointer >> %x\n", ptr);
    return KFREE_FAIL_NOT_MHOLE;
  }
  MHOLE_REMOVE(mem);

  // We use the first-fit-algorithm for searching right base.
  MemHole* i;
  for (i = p_mfree_list.next; i != &p_mfree_list; i = i->next) {
    if (i->base > mem->base) {
      if (mem->base + mem->size > i->base)
        return KFREE_FAIL_ILLEGAL1;
      MHOLE_INSERT_BEFORE(mem, i);
      break;
    }
  }
  if (i == &p_mfree_list)
    _kprintf("%s:can't find p_mfree_list\n", __func__);

  /* Although mem->prev == &mfree_list, mfree_list's base + its size never
   *  equal this mem base, because mfree_list's base and size is always 0
   *  and all mem's base is not 0.
   * Likewise, if mem->next == &mfree_list, this mem's base + size never 
   *  equal mfree_list's base.
   */
  if (mem->prev->base + mem->prev->size == mem->base &&
        mem->base + mem->size == mem->next->base) {
    merge_hole(mem->prev, mem->next);
  } else if (mem->prev->base + mem->prev->size == mem->base) {
    merge_hole(mem->prev, mem);
  } else if (mem->base + mem->size == mem->next->base) {
    merge_hole(mem, mem->next);
  }
  return KFREE_OK;
}

PUBLIC u_int32_t get_realaddr(u_int32_t virt_addr)
{
  return virt_addr - __PAGE_OFFSET;
}

PRIVATE void used_check(MemHole* mem)
{
  MemHole* i;
  for (i = muse_list.next; i != &muse_list; i = i->next) {
    if (i->base == mem->base) {
      _kprintf("[MEMORY ALLOC ERROR] The mem block already be used !!\n");
      for(;;);
    }
  }
}
