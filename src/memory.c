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

#ifdef TEST_BUILD
#include "mock_memory_deps.h"
#include <memory.h>
#else
#include <ld/page_linker.h>
#include <memory.h>
#include <memory_layout.h>
#include <io.h>
#include <vga.h>
#endif

#define MEMDEBUG 1

PRIVATE MemHole* _search_used_mhole(void* ptr, MemHole* use_list);
PRIVATE MemHole* _search_used_align_mhole(void* ptr, MemHole* use_list);
PRIVATE int _internal_common_merge(MemHole* mem);
PRIVATE void merge_hole(MemHole* memfrom, MemHole* memto);
PRIVATE MemHole* new_mhole(MemHole* hole_list);
PRIVATE void init_kmem();
PRIVATE void init_pmem();
PRIVATE void used_check(MemHole* mem);
PRIVATE void init_hole_pool(MemHole* holes, u_int32_t hole_count,
                            MemHole* hole_list, MemHole* free_list,
                            MemHole* use_list, u_int32_t base,
                            u_int32_t size);

#ifndef TEST_BUILD
PRIVATE void mem_serial_putc(char c);
PRIVATE void mem_serial_puts(const char* s);
PRIVATE void mem_serial_putdec(u_int32_t value);
PRIVATE void mem_serial_puthex(u_int32_t value);
PRIVATE void log_memory_layout(void);
#endif

#ifndef TEST_BUILD
PUBLIC void init_mem()
{
  extern u_int32_t __bss_end;
  u_int32_t kernel_image_end_phys =
    (((u_int32_t)&__bss_end - __PAGE_OFFSET + BLOCK_SIZE - 1) &
     ~(BLOCK_SIZE - 1));

  memory_layout_init_runtime(kernel_image_end_phys);
  init_kmem();
  init_pmem();
  log_memory_layout();
}
#endif

/*
 * init_mem_core: Hardware-independent core initialization.
 * Sets up memory hole pool and free/used lists for kernel memory.
 * base_addr: start of allocatable memory region
 * pool_size: size of the allocatable region in bytes
 */
PUBLIC void init_mem_core(u_int32_t base_addr, u_int32_t pool_size)
{
  init_hole_pool(memhole, MAX_MHOLES, &mhole_list, &mfree_list, &muse_list,
                 base_addr, pool_size);
}

PRIVATE void init_hole_pool(MemHole* holes, u_int32_t hole_count,
                            MemHole* hole_list, MemHole* free_list,
                            MemHole* use_list, u_int32_t base,
                            u_int32_t size)
{
  int i;
  for (i = 0; i < hole_count; ++i) {
    if (i == hole_count - 1)
      holes[i].next = hole_list;
    else
      holes[i].next = &holes[i+1];

    if (i == 0)
      holes[i].prev = hole_list;
    else
      holes[i].prev = &holes[i-1];
  }
  hole_list->next = &holes[0];
  hole_list->prev = &holes[hole_count-1];
  holes[0].prev = hole_list;
  holes[hole_count-1].next = hole_list;

  hole_list->base = 0;
  hole_list->size = 0;
  free_list->base = 0;
  free_list->size = 0;
  use_list->base = 0;
  use_list->size = 0;

  use_list->next = use_list;
  use_list->prev = use_list;
  free_list->next = free_list;
  free_list->prev = free_list;

  MemHole* mem = hole_list->next;
  MHOLE_REMOVE(mem);
  MHOLE_INSERT_HEAD(mem, (*free_list));
  free_list->next->base = base;
  free_list->next->size = size;
}

#ifndef TEST_BUILD
PRIVATE void init_kmem()
{
  const memory_layout_policy_t* layout = memory_get_layout_policy();

  if (memory_layout_is_initialized() == 0 || layout->kernel_heap.size == 0) {
    extern u_int32_t __bss_end;
    u_int32_t kernel_membase =
      ((u_int32_t)&__bss_end + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    init_mem_core(kernel_membase, KERNEL_MEMEND - kernel_membase);
    return;
  }

  init_mem_core(layout->kernel_heap.base, layout->kernel_heap.size);
}
#endif

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


#ifndef TEST_BUILD
PRIVATE void init_pmem()
{
  const memory_layout_policy_t* layout = memory_get_layout_policy();

  if (memory_layout_is_initialized() == 0 || layout->process_pool.size == 0) {
    init_hole_pool(p_memhole, MAX_PMHOLES, &p_mhole_list,
                   &p_mfree_list, &p_muse_list,
                   KERNEL_PMEMBASE, KERNEL_PMEMEND - KERNEL_PMEMBASE);
    return;
  }

  init_hole_pool(p_memhole, MAX_PMHOLES, &p_mhole_list,
                 &p_mfree_list, &p_muse_list,
                 layout->process_pool.base, layout->process_pool.size);
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
#endif /* !TEST_BUILD */

#ifndef TEST_BUILD
PRIVATE void mem_serial_putc(char c)
{
  while (!(in8(0x3F8 + 5) & 0x20));
  out8(0x3F8, c);
}

PRIVATE void mem_serial_puts(const char* s)
{
  while (*s) {
    if (*s == '\n')
      mem_serial_putc('\r');
    mem_serial_putc(*s++);
  }
}

PRIVATE void mem_serial_putdec(u_int32_t value)
{
  char buf[12];
  int len = 0;

  if (value == 0) {
    mem_serial_putc('0');
    return;
  }

  while (value > 0) {
    buf[len++] = '0' + (value % 10);
    value /= 10;
  }
  while (len > 0)
    mem_serial_putc(buf[--len]);
}

PRIVATE void mem_serial_puthex(u_int32_t value)
{
  int shift;
  const char* hex = "0123456789abcdef";

  mem_serial_puts("0x");
  for (shift = 28; shift >= 0; shift -= 4)
    mem_serial_putc(hex[(value >> shift) & 0xf]);
}

PRIVATE void log_memory_layout(void)
{
  const memory_info_t* info = memory_get_info();
  const memory_layout_policy_t* layout = memory_get_layout_policy();

  _kprintf(" MEMORY: detected=%xMB source=%s cap=%xMB effective=%xMB\n",
           info->detected_ram_bytes / (1024 * 1024),
           memory_source_name(info->source),
           layout->configured_cap_bytes / (1024 * 1024),
           layout->effective_ram_bytes / (1024 * 1024));
  _kprintf(" MEMORY: heap=%x size=%x pool=%x size=%x pde_end=%x\n",
           layout->kernel_heap.base, layout->kernel_heap.size,
           layout->process_pool.base, layout->process_pool.size,
           layout->kernel_pde_end);

  mem_serial_puts("MEMORY_LAYOUT detected_mb=");
  mem_serial_putdec(info->detected_ram_bytes / (1024 * 1024));
  mem_serial_puts(" source=");
  mem_serial_puts(memory_source_name(info->source));
  mem_serial_puts(" cap_mb=");
  mem_serial_putdec(layout->configured_cap_bytes / (1024 * 1024));
  mem_serial_puts(" effective_mb=");
  mem_serial_putdec(layout->effective_ram_bytes / (1024 * 1024));
  mem_serial_puts(" direct_map_mb=");
  mem_serial_putdec(layout->direct_map_bytes / (1024 * 1024));
  mem_serial_puts(" kernel_heap_base=");
  mem_serial_puthex(layout->kernel_heap.base);
  mem_serial_puts(" kernel_heap_size=");
  mem_serial_puthex(layout->kernel_heap.size);
  mem_serial_puts(" process_pool_base=");
  mem_serial_puthex(layout->process_pool.base);
  mem_serial_puts(" process_pool_size=");
  mem_serial_puthex(layout->process_pool.size);
  mem_serial_puts(" pde_end=");
  mem_serial_putdec(layout->kernel_pde_end);
  mem_serial_puts("\n");
}
#endif

PRIVATE void used_check(MemHole* mem)
{
  MemHole* i;
  for (i = muse_list.next; i != &muse_list; i = i->next) {
    if (i->base == mem->base) {
      _kprintf("[MEMORY ALLOC ERROR] The mem block already be used !!\n");
#ifndef TEST_BUILD
      for(;;);
#endif
    }
  }
}
