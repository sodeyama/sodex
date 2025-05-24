#ifndef _STDIO_H
#define _STDIO_H

#include <sys/types.h>

void putc(char c);
void puts(char *str);
void printf(char *fmt, ...);
void printb(u_int8_t c);
void printb16(u_int16_t c);
void printb32(u_int32_t c);

#endif /* _STDIO_H */
