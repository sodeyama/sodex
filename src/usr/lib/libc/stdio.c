#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Helper: emit char to buffer, respecting capacity */
static int buf_putc(char *buf, size_t size, int pos, char c)
{
  if (buf && (size_t)pos < size - 1)
    buf[pos] = c;
  return pos + 1;
}

/* Helper: emit string to buffer */
static int buf_puts(char *buf, size_t size, int pos, const char *s)
{
  while (*s)
    pos = buf_putc(buf, size, pos, *s++);
  return pos;
}

/* Helper: emit unsigned decimal to buffer */
static int buf_putud(char *buf, size_t size, int pos, u_int32_t val)
{
  char tmp[12];
  int i = 0;
  if (val == 0)
    return buf_putc(buf, size, pos, '0');
  while (val > 0 && i < (int)sizeof(tmp)) {
    tmp[i++] = '0' + (val % 10);
    val /= 10;
  }
  while (i > 0)
    pos = buf_putc(buf, size, pos, tmp[--i]);
  return pos;
}

/* Helper: emit hex to buffer (full 32-bit, no leading zero suppression) */
static int buf_puthex(char *buf, size_t size, int pos, u_int32_t val)
{
  char tmp[8];
  int i = 0;
  int started = 0;
  int shift;
  if (val == 0)
    return buf_putc(buf, size, pos, '0');
  for (shift = 28; shift >= 0; shift -= 4) {
    int digit = (val >> shift) & 0x0f;
    if (digit != 0 || started) {
      started = 1;
      if (digit < 10)
        pos = buf_putc(buf, size, pos, '0' + digit);
      else
        pos = buf_putc(buf, size, pos, 'A' + digit - 10);
    }
  }
  (void)tmp;
  (void)i;
  return pos;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  int pos = 0;
  const char *p;

  if (size == 0)
    return 0;

  for (p = fmt; *p != '\0'; p++) {
    if (*p == '%') {
      p++;
      switch (*p) {
      case 'd': {
        int val = va_arg(ap, int);
        if (val < 0) {
          pos = buf_putc(buf, size, pos, '-');
          val = -val;
        }
        pos = buf_putud(buf, size, pos, (u_int32_t)val);
        break;
      }
      case 'u': {
        u_int32_t val = va_arg(ap, u_int32_t);
        pos = buf_putud(buf, size, pos, val);
        break;
      }
      case 'x': {
        u_int32_t val = va_arg(ap, u_int32_t);
        pos = buf_puthex(buf, size, pos, val);
        break;
      }
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (s)
          pos = buf_puts(buf, size, pos, s);
        break;
      }
      case 'c': {
        char c = (char)va_arg(ap, int);
        pos = buf_putc(buf, size, pos, c);
        break;
      }
      case '%':
        pos = buf_putc(buf, size, pos, '%');
        break;
      case '\0':
        goto done;
      default:
        pos = buf_putc(buf, size, pos, '%');
        pos = buf_putc(buf, size, pos, *p);
        break;
      }
    } else {
      pos = buf_putc(buf, size, pos, *p);
    }
  }
done:
  if (buf) {
    if ((size_t)pos < size)
      buf[pos] = '\0';
    else
      buf[size - 1] = '\0';
  }
  return pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  int ret;
  va_start(ap, fmt);
  ret = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return ret;
}

void putc(char c) {
  write(1, &c, 1);
}

void puts(char *str)
{
  //write(1, str, strlen(str));
  char *p = str;
  while (*p != '\0') {
    putc(*p);
    p++;
  }
}

void printf(char *fmt, ...)
{
  char *p;
  va_list ap;

  va_start(ap, fmt);
  for (p = fmt; *p != '\0'; p++) {
    if (*p == '%') {
      switch(*(++p)) {
      case 'x':
        {
          u_int32_t x = va_arg(ap, u_int32_t);
          if ((x & 0xffffff00) == 0) {
            printb(x);
          } else if ((x & 0xffff0000) == 0) {
            printb16(x);
          } else {
            printb32(x);
          }
        }
        break;
      case 'd':
        {
          int val = va_arg(ap, int);
          char buf[12];
          int i = 0;
          if (val < 0) { putc('-'); val = -val; }
          if (val == 0) { putc('0'); break; }
          while (val > 0 && i < (int)sizeof(buf)) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
          }
          while (i > 0) putc(buf[--i]);
        }
        break;
      case 'u':
        {
          u_int32_t val = va_arg(ap, u_int32_t);
          char buf[12];
          int i = 0;
          if (val == 0) { putc('0'); break; }
          while (val > 0 && i < (int)sizeof(buf)) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
          }
          while (i > 0) putc(buf[--i]);
        }
        break;
      case 'c':
        {
          char c = va_arg(ap, char);
          putc(c);
        }
        break;
      case 's':
        {
          char* s = va_arg(ap, char*);
          puts(s);
        }
        break;
      case '%':
        putc('%');
        break;
      }
    } else {
      putc(*p);
    }
  }
  va_end(ap);
}

void printb(u_int8_t c)
{
  char high, low;
  high = (c >> 4) & 0xf;
  low  = c & 0xf;
  if (high >= 0 && high <= 9)
    high += 0x30;
  else
    high += 0x37;
  putc(high);
  
  if (low >= 0 && low <= 9)
    low += 0x30;
  else
    low += 0x37;
  putc(low);
}

void printb16(u_int16_t c)
{
  u_int8_t high, low;
  high = (c >> 8) & 0xff;
  low  = c & 0xff;

  printb(high);
  printb(low);
}

void printb32(u_int32_t c)
{
  u_int16_t high, low;
  high = (c >> 16) & 0xffff;
  low  = c & 0xffff;

  printb16(high);
  printb16(low);
}
