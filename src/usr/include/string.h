#ifndef _STRING_H
#define _STRING_H

#include <sys/types.h>

void *memset(void *buf, int ch, size_t n);
void *memcpy(void *dest, void *src, size_t n);
size_t strlen(const char* s);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);

#endif /* _STRING_H */
