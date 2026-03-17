/*
 * Host-side unit tests for userland libc extensions.
 * Self-contained: implementations are included directly to avoid host libc conflicts.
 */
#include "test_framework.h"
#include <string.h>  /* host string.h */
#include <stdarg.h>

/* ========================================================================
 * Pull in our implementations with renamed symbols
 * ======================================================================== */

/* Provide types that Sodex headers expect */
typedef unsigned char  u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int   u_int32_t;
typedef int            int32_t;
#define NULL ((void*)0)
#define size_t unsigned long

/* Our custom strlen/strncmp needed by strstr */
static size_t sodex_strlen(const char *s)
{
  const char *p = s;
  for (; *p; p++);
  return (size_t)(p - s);
}

static int sodex_strncmp(const char *s1, const char *s2, size_t n)
{
  size_t i;
  for (i = 0; i < n && *s1 && *s2 && *s1 == *s2; i++, s1++, s2++);
  if (i == n) return 0;
  return *(unsigned char*)s1 - *(unsigned char*)s2;
}

/* --- strstr --- */
static char *sodex_strstr(const char *haystack, const char *needle)
{
  size_t nlen;
  if (*needle == '\0') return (char *)haystack;
  nlen = sodex_strlen(needle);
  for (; *haystack; haystack++) {
    if (*haystack == *needle && sodex_strncmp(haystack, needle, nlen) == 0)
      return (char *)haystack;
  }
  return (void*)0;
}

/* --- strncasecmp --- */
static int to_lower(int c)
{
  if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
  return c;
}

static int sodex_strncasecmp(const char *s1, const char *s2, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++) {
    int c1 = to_lower((unsigned char)s1[i]);
    int c2 = to_lower((unsigned char)s2[i]);
    if (c1 != c2) return c1 - c2;
    if (c1 == 0) return 0;
  }
  return 0;
}

/* --- strcat / strncat --- */
static char *sodex_strcat(char *dest, const char *src)
{
  char *p = dest;
  while (*p) p++;
  while (*src) *p++ = *src++;
  *p = '\0';
  return dest;
}

static char *sodex_strncat(char *dest, const char *src, size_t n)
{
  char *p = dest;
  size_t i;
  while (*p) p++;
  for (i = 0; i < n && src[i] != '\0'; i++)
    p[i] = src[i];
  p[i] = '\0';
  return dest;
}

/* --- strtol --- */
static long sodex_strtol(const char *nptr, char **endptr, int base)
{
  const char *p = nptr;
  long result = 0;
  int negative = 0;
  int digit;

  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (*p == '-') { negative = 1; p++; }
  else if (*p == '+') { p++; }

  if (base == 0) {
    if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (*p == '0') { base = 8; p++; }
    else { base = 10; }
  } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }

  while (*p) {
    if (*p >= '0' && *p <= '9') digit = *p - '0';
    else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
    else break;
    if (digit >= base) break;
    result = result * base + digit;
    p++;
  }

  if (endptr) *endptr = (char *)p;
  return negative ? -result : result;
}

/* --- vsnprintf / snprintf --- */
static int bp(char *buf, size_t sz, int pos, char c)
{
  if (buf && (size_t)pos < sz - 1) buf[pos] = c;
  return pos + 1;
}
static int bps(char *buf, size_t sz, int pos, const char *s)
{
  while (*s) pos = bp(buf, sz, pos, *s++);
  return pos;
}
static int bpud(char *buf, size_t sz, int pos, unsigned int val)
{
  char tmp[12]; int i = 0;
  if (val == 0) return bp(buf, sz, pos, '0');
  while (val > 0 && i < 12) { tmp[i++] = '0' + (val % 10); val /= 10; }
  while (i > 0) pos = bp(buf, sz, pos, tmp[--i]);
  return pos;
}
static int bphex(char *buf, size_t sz, int pos, unsigned int val)
{
  int shift, started = 0;
  if (val == 0) return bp(buf, sz, pos, '0');
  for (shift = 28; shift >= 0; shift -= 4) {
    int d = (val >> shift) & 0x0f;
    if (d || started) {
      started = 1;
      pos = bp(buf, sz, pos, d < 10 ? '0' + d : 'A' + d - 10);
    }
  }
  return pos;
}

static int sodex_vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap)
{
  int pos = 0;
  const char *p;
  if (sz == 0) return 0;
  for (p = fmt; *p; p++) {
    if (*p == '%') {
      p++;
      switch (*p) {
      case 'd': { int v = va_arg(ap, int);
        if (v < 0) { pos = bp(buf, sz, pos, '-'); v = -v; }
        pos = bpud(buf, sz, pos, (unsigned)v); break; }
      case 'u': { unsigned v = va_arg(ap, unsigned);
        pos = bpud(buf, sz, pos, v); break; }
      case 'x': { unsigned v = va_arg(ap, unsigned);
        pos = bphex(buf, sz, pos, v); break; }
      case 's': { const char *s = va_arg(ap, const char*);
        if (s) pos = bps(buf, sz, pos, s); break; }
      case 'c': { char c = (char)va_arg(ap, int);
        pos = bp(buf, sz, pos, c); break; }
      case '%': pos = bp(buf, sz, pos, '%'); break;
      case '\0': goto done;
      default: pos = bp(buf, sz, pos, '%'); pos = bp(buf, sz, pos, *p); break;
      }
    } else { pos = bp(buf, sz, pos, *p); }
  }
done:
  if (buf) { if ((size_t)pos < sz) buf[pos] = '\0'; else buf[sz-1] = '\0'; }
  return pos;
}

static int sodex_snprintf(char *buf, size_t sz, const char *fmt, ...)
{
  va_list ap; int ret;
  va_start(ap, fmt);
  ret = sodex_vsnprintf(buf, sz, fmt, ap);
  va_end(ap);
  return ret;
}

#undef size_t

/* ========================================================================
 * Tests
 * ======================================================================== */

/* === snprintf === */
TEST(snprintf_simple_string) {
    char buf[64];
    int n = sodex_snprintf(buf, sizeof(buf), "hello %s", "world");
    ASSERT_STR_EQ(buf, "hello world");
    ASSERT_EQ(n, 11);
}
TEST(snprintf_decimal) {
    char buf[64]; sodex_snprintf(buf, sizeof(buf), "%d", 42);
    ASSERT_STR_EQ(buf, "42");
}
TEST(snprintf_negative) {
    char buf[64]; sodex_snprintf(buf, sizeof(buf), "%d", -123);
    ASSERT_STR_EQ(buf, "-123");
}
TEST(snprintf_unsigned) {
    char buf[64]; sodex_snprintf(buf, sizeof(buf), "%u", 3000000000U);
    ASSERT_STR_EQ(buf, "3000000000");
}
TEST(snprintf_hex) {
    char buf[64]; sodex_snprintf(buf, sizeof(buf), "%x", 0xDEAD);
    ASSERT_STR_EQ(buf, "DEAD");
}
TEST(snprintf_zero) {
    char buf[64]; sodex_snprintf(buf, sizeof(buf), "%d", 0);
    ASSERT_STR_EQ(buf, "0");
}
TEST(snprintf_truncation) {
    char buf[8];
    int n = sodex_snprintf(buf, sizeof(buf), "hello world");
    ASSERT_EQ(n, 11);
    ASSERT_EQ(buf[7], '\0');
    ASSERT_EQ(buf[6], 'w');
}
TEST(snprintf_mixed) {
    char buf[128];
    sodex_snprintf(buf, sizeof(buf), "status=%d len=%u hex=%x str=%s", 200, 1024U, 0xFF, "ok");
    ASSERT_STR_EQ(buf, "status=200 len=1024 hex=FF str=ok");
}
TEST(snprintf_percent) {
    char buf[32]; sodex_snprintf(buf, sizeof(buf), "100%%");
    ASSERT_STR_EQ(buf, "100%");
}

/* === strstr === */
TEST(strstr_found) {
    const char *s = "HTTP/1.1 200 OK\r\n\r\nbody";
    char *p = sodex_strstr(s, "\r\n\r\n");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((long)(p - s), 15);
}
TEST(strstr_not_found) { ASSERT_NULL(sodex_strstr("hello world", "xyz")); }
TEST(strstr_empty_needle) {
    char *p = sodex_strstr("hello", "");
    ASSERT_NOT_NULL(p); ASSERT_STR_EQ(p, "hello");
}
TEST(strstr_at_end) {
    char *p = sodex_strstr("foobar", "bar");
    ASSERT_NOT_NULL(p); ASSERT_STR_EQ(p, "bar");
}

/* === strncasecmp === */
TEST(strncasecmp_equal) { ASSERT_EQ(sodex_strncasecmp("Content-Length", "content-length", 14), 0); }
TEST(strncasecmp_mixed) { ASSERT_EQ(sodex_strncasecmp("CONTENT-TYPE", "Content-Type", 12), 0); }
TEST(strncasecmp_not_equal) { ASSERT(sodex_strncasecmp("Content-Length", "Content-Type", 12) != 0); }
TEST(strncasecmp_partial) { ASSERT_EQ(sodex_strncasecmp("Content-Length: 42", "content-length: 99", 15), 0); }

/* === strtol === */
TEST(strtol_decimal) { ASSERT_EQ(sodex_strtol("12345", 0, 10), 12345); }
TEST(strtol_negative) { ASSERT_EQ(sodex_strtol("-42", 0, 10), -42); }
TEST(strtol_hex) { ASSERT_EQ(sodex_strtol("0xFF", 0, 0), 255); }
TEST(strtol_hex_explicit) { ASSERT_EQ(sodex_strtol("1A", 0, 16), 26); }
TEST(strtol_octal) { ASSERT_EQ(sodex_strtol("010", 0, 0), 8); }
TEST(strtol_endptr) {
    char *end;
    long v = sodex_strtol("200 OK", &end, 10);
    ASSERT_EQ(v, 200); ASSERT_STR_EQ(end, " OK");
}
TEST(strtol_zero) { ASSERT_EQ(sodex_strtol("0", 0, 10), 0); }
TEST(strtol_whitespace) { ASSERT_EQ(sodex_strtol("  42", 0, 10), 42); }

/* === strcat / strncat === */
TEST(strcat_basic) {
    char buf[32] = "hello";
    sodex_strcat(buf, " world");
    ASSERT_STR_EQ(buf, "hello world");
}
TEST(strncat_basic) {
    char buf[32] = "hello";
    sodex_strncat(buf, " world!!!", 6);
    ASSERT_STR_EQ(buf, "hello world");
}

int main(void) {
    printf("=== libc extension tests ===\n");
    RUN_TEST(snprintf_simple_string);
    RUN_TEST(snprintf_decimal);
    RUN_TEST(snprintf_negative);
    RUN_TEST(snprintf_unsigned);
    RUN_TEST(snprintf_hex);
    RUN_TEST(snprintf_zero);
    RUN_TEST(snprintf_truncation);
    RUN_TEST(snprintf_mixed);
    RUN_TEST(snprintf_percent);
    RUN_TEST(strstr_found);
    RUN_TEST(strstr_not_found);
    RUN_TEST(strstr_empty_needle);
    RUN_TEST(strstr_at_end);
    RUN_TEST(strncasecmp_equal);
    RUN_TEST(strncasecmp_mixed);
    RUN_TEST(strncasecmp_not_equal);
    RUN_TEST(strncasecmp_partial);
    RUN_TEST(strtol_decimal);
    RUN_TEST(strtol_negative);
    RUN_TEST(strtol_hex);
    RUN_TEST(strtol_hex_explicit);
    RUN_TEST(strtol_octal);
    RUN_TEST(strtol_endptr);
    RUN_TEST(strtol_zero);
    RUN_TEST(strtol_whitespace);
    RUN_TEST(strcat_basic);
    RUN_TEST(strncat_basic);
    TEST_REPORT();
}
