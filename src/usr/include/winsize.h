#ifndef _USR_WINSIZE_H
#define _USR_WINSIZE_H

#include <sys/types.h>

struct winsize {
  u_int16_t cols;
  u_int16_t rows;
};

int get_winsize(int fd, struct winsize *winsize);
int set_winsize(int fd, const struct winsize *winsize);

#endif /* _USR_WINSIZE_H */
