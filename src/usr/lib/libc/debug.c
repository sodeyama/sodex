#include <stdarg.h>
#include <stdio.h>
#include <debug.h>

void debug_printf(const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  int len;

  va_start(ap, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (len > 0)
    debug_write(buf, (size_t)len);
}
