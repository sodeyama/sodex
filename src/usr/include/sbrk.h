#ifndef _SBRK_H
#define _SBRK_H

#include <sys/types.h>

extern u_int32_t last_alloc_addr;

void *sbrk(intptr_t increment);

#endif
