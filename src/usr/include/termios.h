#ifndef _USR_TERMIOS_H
#define _USR_TERMIOS_H

#include <sys/types.h>

#define ICANON 0x0001
#define ECHO   0x0002
#define ISIG   0x0004

#define TCSANOW 0

struct termios {
  u_int32_t c_lflag;
};

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

#endif /* _USR_TERMIOS_H */
