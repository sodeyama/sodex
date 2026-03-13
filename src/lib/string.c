/*
 * String and memory utility functions for Sodex kernel
 */

#ifdef TEST_BUILD
typedef unsigned int size_t;
#else
#include <sys/types.h>
#endif

size_t strlen(const char* s)
{
  size_t len = 0;
  while (*s++) len++;
  return len;
}

int strcmp(const char* s1, const char* s2)
{
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0) return 0;
  return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src)
{
  char* ret = dest;
  while ((*dest++ = *src++));
  return ret;
}

char* strncpy(char* dest, const char* src, size_t n)
{
  char* ret = dest;
  while (n && (*dest++ = *src++)) n--;
  while (n--) *dest++ = 0;
  return ret;
}

char* strchr(const char* s, int c)
{
  while (*s) {
    if (*s == (char)c) return (char*)s;
    s++;
  }
  return (c == '\0') ? (char*)s : 0;
}

char* strrchr(const char* s, int c)
{
  const char* last = 0;
  while (*s) {
    if (*s == (char)c) last = s;
    s++;
  }
  if (c == '\0') return (char*)s;
  return (char*)last;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
  const unsigned char* p1 = s1;
  const unsigned char* p2 = s2;
  while (n--) {
    if (*p1 != *p2) return *p1 - *p2;
    p1++;
    p2++;
  }
  return 0;
}
