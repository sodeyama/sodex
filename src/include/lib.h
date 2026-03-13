#ifndef _LIB_H
#define _LIB_H

#include <sodex/const.h>
#include <sys/types.h>

#define CEIL(a, b) ((((a) + ((b) - 1)) & ~((b) - 1)))

PUBLIC int pow(int x, int y);
PUBLIC int logn(int x, int y);

#endif
