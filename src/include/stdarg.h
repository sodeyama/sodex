#ifndef _STDARG_H
#define _STDARG_H

#include <sys/types.h>

typedef char* va_list;

#define __va_size(x) (sizeof(x) > sizeof(int32_t) ? sizeof(x):sizeof(int32_t))
//#define __va_size(x) ((sizeof(x) + sizeof(int)-1) & ~(sizeof(int) - 1))
#define va_start(ap, last) (ap = (va_list)((char *)(&last) + __va_size(last)))
#define va_arg(ap, type) (*(type *)((ap += __va_size(type))-(__va_size(type))))
#define va_end(ap) ((void)0)


#endif /* _STDARG_H */
