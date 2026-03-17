#include <sodex/const.h>
#include <string.h>

void *memmove(void *dest, const void *src, size_t n)
{
  unsigned char *dst = (unsigned char *)dest;
  const unsigned char *from = (const unsigned char *)src;
  size_t i;

  if (dst == from || n == 0)
    return dest;

  if (dst < from) {
    for (i = 0; i < n; i++)
      dst[i] = from[i];
  } else {
    for (i = n; i > 0; i--)
      dst[i - 1] = from[i - 1];
  }

  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
  const unsigned char *left = (const unsigned char *)s1;
  const unsigned char *right = (const unsigned char *)s2;
  size_t i;

  for (i = 0; i < n; i++) {
    if (left[i] != right[i])
      return (int)left[i] - (int)right[i];
  }

  return 0;
}

size_t strlen(const char* s)
{
  const char* str = s;
  for ( ; *s; s++);
  return (size_t)(s - str);
}

char* strchr(const char* s, int c)
{
  for (; *s != 0 && *s != c; s++);
  if (*s == (char)c) return (char*)s;
  else return NULL;
}

char* strrchr(const char* s, int c)
{
  const char* p = NULL;
  for (; *s; s++)
    if (*s == (char)c) p = s;
  return (char*)p;
}

int strcmp(const char* s1, const char* s2)
{
  for (; *s1 && *s2 && *s1 == *s2; s1++, s2++);
  if (*s1 != NULL && *s2 == NULL) return 1; // s1 is larger than s2
  if (*s1 == NULL && *s2 != NULL) return -1; // s1 is smaller than s2

  return (*s1 - *s2);
}

int strncmp(const char* s1, const char* s2, size_t n)
{
  int i;
  for (i = 0; i < n && *s1 && *s2 && *s1 == *s2; i++, s1++, s2++);
  if (i == n) return 0;
  if (*s1 != NULL && *s2 == NULL) return 1; // s1 is larger than s2
  if (*s1 == NULL && *s2 != NULL) return -1; // s1 is smaller than s2

  return (*s1 - *s2);
}

char* strcpy(char* dest, const char* src)
{
  char *p = dest;
  for (; *src; dest++, src++)
    *dest = *src;
  return p;
}

char* strncpy(char* dest, const char* src, size_t n)
{
  size_t i = 0;

  for (; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];
  for (; i < n; i++)
    dest[i] = '\0';
  return dest;
}
