/*
 * Simple string library implementation for Sodex kernel
 */

#include <sys/types.h>

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

int pow(int x, int y)
{
  int result = 1;
  while (y > 0) {
    result *= x;
    y--;
  }
  return result;
}

int log(int x, int y)
{
  int result = 0;
  while (x >= y) {
    x /= y;
    result++;
  }
  return result;
}
