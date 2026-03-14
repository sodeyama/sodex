#ifndef _KERNEL_WINSIZE_H
#define _KERNEL_WINSIZE_H

#include <sys/types.h>

struct winsize {
  u_int16_t cols;
  u_int16_t rows;
};

#endif /* _KERNEL_WINSIZE_H */
