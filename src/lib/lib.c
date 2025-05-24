#include <lib.h>

PUBLIC inline size_t strlen(const char* s)
{
  const char* str = s;
  for ( ; *s; s++);
  return (size_t)(s - str);
}

PUBLIC inline char* strchr(const char* s, int c)
{
  for (; *s && *s != c; s++);
  if (*s == (char)c) return (char*)s;
  else return NULL;
}

PUBLIC inline char* strrchr(const char* s, int c)
{
  char* p = NULL;
  for (; *s; s++)
	if (*s == (char)c) p = s;
  return p;
}

PUBLIC inline int strcmp(const char* s1, const char* s2)
{
  for (; *s1 && *s2 && *s1 == *s2; s1++, s2++);
  if (*s1 != NULL && *s2 == NULL) return 1; // s1 is larger than s2
  if (*s1 == NULL && *s2 != NULL) return -1; // s1 is smaller than s2

  return (*s1 - *s2);
}

PUBLIC inline int strncmp(const char* s1, const char* s2, size_t n)
{
  int i;
  for (i = 0; i < n && *s1 && *s2 && *s1 == *s2; i++, s1++, s2++);
  if (i == n) return 0;
  if (*s1 != NULL && *s2 == NULL) return 1; // s1 is larger than s2
  if (*s1 == NULL && *s2 != NULL) return -1; // s1 is smaller than s2

  return (*s1 - *s2);
}

PUBLIC inline char* strcpy(char* dest, const char* src)
{
  char *p = dest;
  for (; *src; dest++, src++)
    *dest = *src;
  return p;
}

PUBLIC inline char* strncpy(char* dest, const char* src, size_t n)
{
  size_t i;
  for (;i < n; i++)
    *(dest+i) = *(src+i);
  return dest;
}

PUBLIC inline int pow(int x, int y)
{
  int ret = 1;
  int i;
  for (i = 0; i < y; i++)
	ret *= x;
  return ret;
}	

PUBLIC inline int log(int x, int y)
{
  int i, ret = 0;
  for (i=0; ;i++) {
    if (pow(x, i) == y)
      return i;
    else if (pow(x, i) > y)
      return -1;
  }
}
