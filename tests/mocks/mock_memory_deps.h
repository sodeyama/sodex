/*
 * Mock dependencies for compiling memory.c on host.
 * Replaces kernel headers: ld/page_linker.h, vga.h, sodex/const.h, sys/types.h
 *
 * IMPORTANT: This header must be included BEFORE memory.h so that
 * TEST_BUILD guards in memory.h skip kernel-specific includes.
 */
#ifndef MOCK_MEMORY_DEPS_H
#define MOCK_MEMORY_DEPS_H

/* Prevent kernel's stdarg.h and sys/types.h from being used */
#define _STDARG_H
#define _TYPES_H

#include <stdint.h>

/* Type definitions matching kernel types */
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;

/* Kernel macros */
#define PUBLIC
#define PRIVATE static
#define EXTERN extern
#ifndef NULL
#define NULL ((void*)0)
#endif
#define BLOCK_SIZE 4096

/* Page offset (0 for host testing) */
#define __PAGE_OFFSET 0

/* VGA stubs - use extern declaration to avoid pulling in stdio.h here */
int printf(const char *fmt, ...);
#define _kprintf printf
static inline void _kputs(const char *s) { while (*s) { (void)*s; s++; } }

/* memset declaration (provided by our string.o) */
void *memset(void *buf, int ch, unsigned int n);

#endif
