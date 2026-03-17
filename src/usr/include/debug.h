#ifndef _USR_DEBUG_H
#define _USR_DEBUG_H

#include <sys/types.h>

int debug_write(const char *buf, size_t len);
void debug_printf(const char *fmt, ...);

#endif /* _USR_DEBUG_H */
