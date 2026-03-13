/*
 *  @File        vga.c
 *  @Brief       VGA API using VRAM of 0xb8000
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/19  update: 2007/05/03
 *      
 *  Copyright (C) 2007 Sodex
 */

#ifdef TEST_BUILD
#include <stdint.h>
#include <stdarg.h>
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
#define PUBLIC
#define PRIVATE static
#else
#include <kernel.h>
#include <vga.h>
#include <key.h>
#include <stdarg.h>
#include <descriptor.h>
#endif

#ifndef TEST_BUILD
PRIVATE int screenX = 0;
PRIVATE int screenY = 0;
PRIVATE int promptX = 0;
PRIVATE int promptY = 0;
//char gColor = 0x7;
//char gColor = 0x9a;
//char gColor = 0x30;
char gColor = 0x2;

PUBLIC void _pos_putc(int x, int y, char c)
{
  _poscolor_printc(x, y, gColor, c);
}

PUBLIC void _kputc(char c)
{
  if (c == '\n') {
    screenX = 0;
    if (screenY + 2> SCREEN_HEIGHT) {
      screen_scrollup();
    } else
      screenY++;
    return;
  } else if (c == KEY_BACK) {
    if (screenY > promptY || (screenY == promptY && screenX > promptX)) {
      _poscolor_printc(--screenX, screenY, gColor, 0);
    }
    return;
  }

  _poscolor_printc(screenX, screenY, gColor, c);
  
  if (screenX >= SCREEN_WIDTH-1) {
    screenY++;
    if (screenY + 1 > SCREEN_HEIGHT) {
      screen_scrollup();
    }
  }
  screenX = (screenX + 1)%SCREEN_WIDTH;
}

PUBLIC void _kputs(char *str)
{
  char *p = str;
  while (*p != '\0') {
    _kputc(*p);
    p++;
  }
}

#endif /* !TEST_BUILD */

/*
 * _snprintb: Write hex representation of a byte to buffer.
 * Returns number of chars written.
 */
PRIVATE int _snprintb(char *buf, int remaining, u_int8_t c)
{
  if (remaining < 2) return 0;
  char high = (c >> 4) & 0xf;
  char low  = c & 0xf;
  buf[0] = (high <= 9) ? high + '0' : high + '7';
  buf[1] = (low  <= 9) ? low  + '0' : low  + '7';
  return 2;
}

/*
 * _kvsnprintf: Format string into buffer (kernel vsnprintf).
 * Supports: %x (hex), %d (decimal), %c (char), %s (string), %% (literal %).
 * Returns number of chars written (not including null terminator).
 */
PUBLIC int _kvsnprintf(char *buf, int size, const char *fmt, va_list ap)
{
  int pos = 0;
  const char *p;

  for (p = fmt; *p != '\0' && pos < size - 1; p++) {
    if (*p == '%') {
      switch(*(++p)) {
      case 'x':
        {
          u_int32_t x = va_arg(ap, u_int32_t);
          if ((x & 0xffffff00) == 0) {
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)x);
          } else if ((x & 0xffff0000) == 0) {
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)(x >> 8));
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)x);
          } else {
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)(x >> 24));
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)(x >> 16));
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)(x >> 8));
            pos += _snprintb(buf + pos, size - 1 - pos, (u_int8_t)x);
          }
        }
        break;
      case 'd':
        {
          int val = va_arg(ap, int);
          char numbuf[12];
          int len = 0;
          unsigned int uval;
          if (val < 0) {
            if (pos < size - 1) buf[pos++] = '-';
            uval = (unsigned int)(-val);
          } else {
            uval = (unsigned int)val;
          }
          /* Convert to string (reverse) */
          if (uval == 0) {
            numbuf[len++] = '0';
          } else {
            while (uval > 0) {
              numbuf[len++] = '0' + (uval % 10);
              uval /= 10;
            }
          }
          /* Copy reversed digits */
          int i;
          for (i = len - 1; i >= 0 && pos < size - 1; i--) {
            buf[pos++] = numbuf[i];
          }
        }
        break;
      case 'c':
        {
          char c = (char)va_arg(ap, int);
          if (pos < size - 1) buf[pos++] = c;
        }
        break;
      case 's':
        {
          char* s = va_arg(ap, char*);
          while (*s && pos < size - 1) {
            buf[pos++] = *s++;
          }
        }
        break;
      case '%':
        if (pos < size - 1) buf[pos++] = '%';
        break;
      default:
        break;
      }
    } else {
      buf[pos++] = *p;
    }
  }
  buf[pos] = '\0';
  return pos;
}

#ifndef TEST_BUILD
PUBLIC void _kprintf(char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  _kvsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  _kputs(buf);
}
#endif

#ifndef TEST_BUILD
PUBLIC void _kprintb(u_int8_t c)
{
  char high, low;
  high = (c >> 4) & 0xf;
  low  = c & 0xf;
  if (high >= 0 && high <= 9)
    high += 0x30;
  else
    high += 0x37;
  _kputc(high);
  
  if (low >= 0 && low <= 9)
    low += 0x30;
  else
    low += 0x37;
  _kputc(low);
}

PUBLIC void _kprintb16(u_int16_t c)
{
  u_int8_t high, low;
  high = (c >> 8) & 0xff;
  low  = c & 0xff;

  _kprintb(high);
  _kprintb(low);
}

PUBLIC void _kprintb32(u_int32_t c)
{
  u_int16_t high, low;
  high = (c >> 16) & 0xffff;
  low  = c & 0xffff;

  _kprintb16(high);
  _kprintb16(low);
}

PUBLIC void clr_screen()
{
  int x, y;

  for (y=0; y <= SCREEN_HEIGHT; ++y)
    for (x=0; x <= SCREEN_WIDTH; ++x)
      _pos_putc(x, y, 0);
}

PUBLIC void screen_scrollup()
{
  int i, j;
  for (i = SCREEN_WIDTH, j=0; i <= VRAMMAX; ++i, ++j)
    VRAM[2 * j] = VRAM[2 * i];
  for (i = 0; i < SCREEN_WIDTH*2; ++i)
    VRAM[2*SCREEN_WIDTH*SCREEN_HEIGHT+i] = 0;
  screenY = SCREEN_HEIGHT - 1;
  if (promptY > 0)
    promptY--;
}

PUBLIC void screen_pointset(int x, int y)
{
  screenX = x;
  screenY = y;
}

PUBLIC void screen_setcolor(char color)
{
  gColor = color;
}

PUBLIC void init_screen()
{
  clr_screen();
  _kputs("Sodex  version 0.0.2 {2007/10/28}                  "
         "Copyright (C) 2007, Sodeyama\n");
  _kputs(" URL:http://d.hatena.ne.jp/sodex/ \n");
  _kputs("\n");
}

PUBLIC void screen_save_prompt()
{
  promptX = screenX;
  promptY = screenY;
}

PUBLIC void debug_print()
{
  _pos_putc(0, 74, (char)(screenX>>24));
  _pos_putc(1, 75, (char)(screenX>>16));
  _pos_putc(2, 76, (char)(screenX>>8));
  _pos_putc(3, 77, (char)screenX);
}
#endif /* !TEST_BUILD */
