#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>


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
