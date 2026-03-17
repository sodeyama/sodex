#ifndef _STDIO_H
#define _STDIO_H

#include <sys/types.h>
#include <stdarg.h>

void putc(char c);
void puts(char *str);
void printf(char *fmt, ...);
void printb(u_int8_t c);
void printb16(u_int16_t c);
void printb32(u_int32_t c);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* _STDIO_H */
