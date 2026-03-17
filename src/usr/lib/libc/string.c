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

char* strstr(const char *haystack, const char *needle)
{
  size_t nlen;

  if (*needle == '\0')
    return (char *)haystack;
  nlen = strlen(needle);
  for (; *haystack; haystack++) {
    if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
      return (char *)haystack;
  }
  return NULL;
}

static int to_lower(int c)
{
  if (c >= 'A' && c <= 'Z')
    return c + ('a' - 'A');
  return c;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
  size_t i;

  for (i = 0; i < n; i++) {
    int c1 = to_lower((unsigned char)s1[i]);
    int c2 = to_lower((unsigned char)s2[i]);
    if (c1 != c2)
      return c1 - c2;
    if (c1 == 0)
      return 0;
  }
  return 0;
}

char* strcat(char *dest, const char *src)
{
  char *p = dest;

  while (*p)
    p++;
  while (*src)
    *p++ = *src++;
  *p = '\0';
  return dest;
}

char* strncat(char *dest, const char *src, size_t n)
{
  char *p = dest;
  size_t i;

  while (*p)
    p++;
  for (i = 0; i < n && src[i] != '\0'; i++)
    p[i] = src[i];
  p[i] = '\0';
  return dest;
}

long strtol(const char *nptr, char **endptr, int base)
{
  const char *p = nptr;
  long result = 0;
  int negative = 0;
  int digit;

  /* skip whitespace */
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;

  /* sign */
  if (*p == '-') {
    negative = 1;
    p++;
  } else if (*p == '+') {
    p++;
  }

  /* auto-detect base */
  if (base == 0) {
    if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
      base = 16;
      p += 2;
    } else if (*p == '0') {
      base = 8;
      p++;
    } else {
      base = 10;
    }
  } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }

  while (*p) {
    if (*p >= '0' && *p <= '9')
      digit = *p - '0';
    else if (*p >= 'a' && *p <= 'f')
      digit = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F')
      digit = *p - 'A' + 10;
    else
      break;
    if (digit >= base)
      break;
    result = result * base + digit;
    p++;
  }

  if (endptr)
    *endptr = (char *)p;
  return negative ? -result : result;
}
