#ifndef MOCK_VGA_H
#define MOCK_VGA_H

#include <stdio.h>
#include <stdarg.h>

#define _kprintf printf
#define _kputs(s) fputs(s, stdout)
#define _kputc(c) putchar(c)

#endif
