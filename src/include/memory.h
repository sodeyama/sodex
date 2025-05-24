#ifndef _MEMORY_H
#define _MEMORY_H

#include <sodex/const.h>
#include <sys/types.h>


#define MAXMEMSIZE (((u_int16_t*)(0xc0090000))[0])

#define MAX_MHOLES 8192
#define MAX_PMHOLES 1024

// we need the memory size of 64MB at least
#define KERNEL_MEMBASE 0xc0100000
#define KERNEL_MEMEND  0xc2000000	// 32MB
#define KERNEL_PMEMBASE 0x2000000	// physical address
#define KERNEL_PMEMEND	0x4000000	// physical address

#define MIN_MEMSIZE 32

#define KFREE_OK                0
#define KFREE_FAIL_NOT_MHOLE    1
#define KFREE_FAIL_ILLEGAL1     2
#define KFREE_FAIL_ILLEGAL2     3 

#define PMEM_BLOCK	(4096-1)

typedef struct _MemHole {
  u_int32_t base;
  u_int32_t align_base;
  u_int32_t size;
  struct _MemHole*  prev;
  struct _MemHole*  next;
} MemHole;

PUBLIC MemHole memhole[MAX_MHOLES];
PUBLIC MemHole p_memhole[MAX_PMHOLES];

PUBLIC MemHole muse_list;
PUBLIC MemHole mfree_list;
PUBLIC MemHole mhole_list;

PUBLIC MemHole p_muse_list;
PUBLIC MemHole p_mfree_list;
PUBLIC MemHole p_mhole_list;

PUBLIC void init_mem();
PUBLIC void* kalloc(u_int32_t size);
PUBLIC int32_t kfree(void* ptr);
PUBLIC void* aalloc(u_int32_t size, u_int8_t align_bit);
PUBLIC int32_t afree(void* ptr);

PUBLIC void _kprint_mem();

PUBLIC void* palloc(u_int32_t size);
PUBLIC int32_t pfree(void* ptr);

PUBLIC u_int32_t get_realaddr(u_int32_t virt_addr);

#define MHOLE_REMOVE(p) do {    \
    p->prev->next = p->next;    \
    p->next->prev = p->prev;    \
  } while(0)

#define MHOLES_REMOVE(to, from) do {    \
    to->prev->next = from->next;        \
    from->next->prev = to->prev;        \
  } while(0)

#define MHOLE_CLEAN(p) do {     \
    p->base = 0;                \
    p->size = 0;                \
  } while (0)

#define MHOLE_INSERT_HEAD(p, head) do { \
    head.next->prev = p;                \
    p->next = head.next;                \
    head.next = p;                      \
    p->prev = &head;                    \
  } while(0)

#define MHOLE_INSERT_TAIL(p, head) do { \
    head.prev->next = p;                \
    p->prev = head.prev;                \
    head.prev = p;                      \
    p->next = &head;                    \
  } while(0)

#define MHOLE_INSERT_BEFORE(p, ep) do { \
    ep->prev->next = p;                 \
    p->prev = ep->prev;                 \
    ep->prev = p;                       \
    p->next = ep;                       \
  } while(0)

#define MHOLE_INSERT_AFTER(p, ep) do {  \
    ep->next->prev = p;                 \
    p->next = ep->next;                 \
    ep->next = p;                       \
    p->prev = ep;                       \
  } while(0)
    

#endif /* _MEMORY_H */
