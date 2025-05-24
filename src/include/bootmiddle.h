#ifndef _BOOTMIDDLE_H
#define _BOOTMIDDLE_H

#include <sodex/const.h>
#include <sys/types.h>

PUBLIC u_int16_t MEMORY_SIZE;

//#define MEMSIZE ((u_int16_t*)(&MEMORY_SIZE))
//#define MEMSIZE_OFFSET 0x90000/2
//#define MAXMEMSIZE (u_int32_t)(MEMSIZE[MEMSIZE_OFFSET])*1024*1024
#define MAXMEMSIZE (((u_int16_t*)(0xc0090000))[0])


#endif /* _BOOTMIDDLE_H */
