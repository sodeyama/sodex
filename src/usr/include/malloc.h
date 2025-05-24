#ifndef _MALLOC_H
#define _MALLOC_H

#include <sodex/const.h>
#include <sys/types.h>

#define MAX_MHOLES 512
#define MIN_MEMSIZE 32
#define SBRK_RESERVE_SIZE 1048576
#define INITIAL_MEMSIZE 1048576 //1MB

typedef struct _MemHole {
  u_int32_t base;
  u_int32_t size;
  struct _MemHole*  prev;
  struct _MemHole*  next;
} MemHole;

MemHole memhole[MAX_MHOLES];

MemHole muse_list;
MemHole mfree_list;
MemHole mhole_list;

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

void* malloc(size_t size);
void free(void* ptr);

#endif
