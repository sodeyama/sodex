/*
 * stdlib.h - BearSSL compatibility stub
 * Minimal definitions for x86intrin.h / mm_malloc.h
 */
#ifndef _BEARSSL_COMPAT_STDLIB_H
#define _BEARSSL_COMPAT_STDLIB_H

#include <stddef.h>

/* mm_malloc.h needs these but we never actually call them */
void *malloc(size_t size);
void free(void *ptr);

#endif
