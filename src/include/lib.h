#ifndef _LIB_H
#define _LIB_H

#include <sodex/const.h>
#include <sys/types.h>

#define CEIL(a, b) ((a&(~(b-1)))+b)

PUBLIC inline size_t strlen(const char* s);
PUBLIC inline char* strchr(const char* s, int c);
PUBLIC inline char* strrchr(const char* s, int c);
PUBLIC inline int strcmp(const char* s1, const char* s2);
PUBLIC inline int strncmp(const char* s1, const char* s2, size_t n);
PUBLIC inline char* strcpy(char* dest, const char* src);
PUBLIC inline char* strncpy(char* dest, const char* src, size_t n);

PUBLIC inline int pow(int x, int y);
PUBLIC inline int log(int x, int y);

#endif 
