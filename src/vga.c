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
#define VGA_COLOR_DEFAULT 0x02
#else
#include <kernel.h>
#include <vga.h>
#include <stdarg.h>
#endif

#include <display/console.h>
#include <display/vga_text_backend.h>

PRIVATE struct console_state kernel_console;
PRIVATE struct display_backend vga_console_backend;
PRIVATE int console_initialized = 0;

PRIVATE struct console_state *get_kernel_console()
{
  if (console_initialized == 0) {
    vga_text_backend_init(&vga_console_backend);
    console_init(&kernel_console, &vga_console_backend, VGA_COLOR_DEFAULT);
    console_initialized = 1;
  }
  return &kernel_console;
}

PUBLIC void _kputc(char c)
{
  console_write_char(get_kernel_console(), c);
}

PUBLIC void _kputs(char *str)
{
  console_write(get_kernel_console(), str);
}

PUBLIC void _pos_putc(int x, int y, char c)
{
  console_putc_at(get_kernel_console(), x, y, c);
}

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

PUBLIC void _kprintf(char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  _kvsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  _kputs(buf);
}

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
  console_clear(get_kernel_console());
}

PUBLIC void screen_scrollup()
{
  console_scroll_up(get_kernel_console());
}

PUBLIC void screen_pointset(int x, int y)
{
  console_set_cursor(get_kernel_console(), x, y);
}

PUBLIC int screen_cols(void)
{
  return get_kernel_console()->backend->cols;
}

PUBLIC int screen_rows(void)
{
  return get_kernel_console()->backend->rows;
}

PUBLIC void screen_setcolor(char color)
{
  console_set_color(get_kernel_console(), color);
}

PUBLIC void init_screen()
{
  struct console_state *console = get_kernel_console();

  console_reset(console);
  clr_screen();
  _kputs("Sodex  version 0.0.2 {2007/10/28}                  "
         "Copyright (C) 2007, Sodeyama\n");
  _kputs(" URL:http://d.hatena.ne.jp/sodex/ \n");
  _kputs("\n");
}

PUBLIC void screen_save_prompt()
{
  console_save_prompt(get_kernel_console());
}

PUBLIC void debug_print()
{
  struct console_state *console = get_kernel_console();
  int last_row = screen_rows() - 1;

  if (last_row < 0) {
    last_row = 0;
  }

  _pos_putc(0, last_row, (char)(console->cursor_x >> 24));
  _pos_putc(1, last_row, (char)(console->cursor_x >> 16));
  _pos_putc(2, last_row, (char)(console->cursor_x >> 8));
  _pos_putc(3, last_row, (char)console->cursor_x);
}

#ifdef TEST_BUILD
PUBLIC void test_vga_reset_console(void)
{
  struct console_state *console = get_kernel_console();

  console_reset(console);
  console_clear(console);
}

PUBLIC char test_vga_peek_char(int x, int y)
{
  return vga_text_backend_peek_char(x, y);
}

PUBLIC char test_vga_peek_color(int x, int y)
{
  return vga_text_backend_peek_color(x, y);
}
#endif
