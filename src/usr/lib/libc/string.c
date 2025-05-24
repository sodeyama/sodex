#include <sodex/const.h>
#include <string.h>

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
  char* p = NULL;
  for (; *s; s++)
	if (*s == (char)c) p = s;
  return p;
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
  size_t i;
  for (;i < n; i++)
    *(dest+i) = *(src+i);
  return dest;
}

