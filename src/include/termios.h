#ifndef _KERNEL_TERMIOS_H
#define _KERNEL_TERMIOS_H

#include <sys/types.h>

#define ICANON 0x0001
#define ECHO   0x0002
#define ISIG   0x0004
#define ECHONL 0x0008

#define TCSANOW 0

struct termios {
  u_int32_t c_lflag;
};

#endif /* _KERNEL_TERMIOS_H */
