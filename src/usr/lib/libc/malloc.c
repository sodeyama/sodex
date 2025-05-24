#include <malloc.h>
#include <process.h>
#include <stdlib.h>

static MemHole* _search_used_mhole(void* ptr, MemHole* use_list);
static void merge_hole(MemHole* memfrom, MemHole* memto);
static MemHole* new_mhole(MemHole* hole_list);
static void init_mem();

static void init_mem()
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
  mfree_list.next->base = ((struct task_struct*)getpstat())->allocpoint;
  mfree_list.next->size = INITIAL_MEMSIZE;
  sbrk(INITIAL_MEMSIZE);
}

static MemHole* _search_used_mhole(void* ptr, MemHole* use_list)
{
  MemHole* p;
  for (p = use_list->next; p != use_list; p = p->next) {
    if (p->base == (u_int32_t)ptr)
      return p;
  }
  return NULL;
}

void free(void* ptr)
{
  if (ptr == NULL)
    return;

  MemHole* mem = _search_used_mhole(ptr, &muse_list);
  if (mem == NULL) {
    printf("kfree:invalid memory pointer >> %x\n", ptr);
    return;
  }
  MHOLE_REMOVE(mem);

  // We use the first-fit-algorithm for searching right base.
  MemHole* i;
  for (i = mfree_list.next; i != &mfree_list; i = i->next) {
    if (i->base > mem->base) {
      if (mem->base + mem->size > i->base)
        return;
      MHOLE_INSERT_BEFORE(mem, i);
      break;
    }
  }
  if (i == &mfree_list)
    printf("%s:can't find mfree_list\n", __func__);

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
  return;
}

static void merge_hole(MemHole* memfrom, MemHole* memto)
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

void* malloc(size_t size)
{
  static int first_flag = 0;
  if (first_flag == 0) {
    init_mem();
    first_flag++;
  }

  MemHole *i, *new;
  for (i = mfree_list.next; i != &mfree_list; i = i->next) {
    if (i->size > size) {
      if (i->size - size > MIN_MEMSIZE) {
        new = new_mhole(&mhole_list);
        if (new == NULL)
          puts("new_mhole() error\n");
        new->base = i->base;
        new->size = size;
        i->base += size;
        i->size -= size;
        MHOLE_INSERT_HEAD(new, muse_list);
#ifdef DEBUG
        printf("new->base:%x\n", new->base);
#endif
        return (void*)new->base;
      } else {
        MHOLE_REMOVE(i);
        MHOLE_INSERT_HEAD(i, muse_list);
        return (void*)i->base;
      }
    } else if (i->size == size) {
      MHOLE_REMOVE(i);
      MHOLE_INSERT_HEAD(i, muse_list);
      return (void*)i->base;
    }
  }

  u_int32_t new_reserve_size;
  if (size*2 < SBRK_RESERVE_SIZE)
    new_reserve_size = SBRK_RESERVE_SIZE;
  else
    new_reserve_size = CEIL(size*2, BLOCK_SIZE);

  void* new_addr = sbrk(new_reserve_size);
  if (new_addr == NULL)
    return NULL;
  (mfree_list.prev)->base += size;
  (mfree_list.prev)->size += (new_reserve_size - size);

  return (mfree_list.prev)->base;
}

static MemHole* new_mhole(MemHole* hole_list)
{
  MemHole* ret;
  
  ret = hole_list->next;
  //memdump(0x8048ff0, 0x20);
  MHOLE_REMOVE(ret);

  return ret;
}
