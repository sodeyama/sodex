#ifndef _USR_POLL_H
#define _USR_POLL_H

#include <sys/types.h>

#define POLLIN  0x0001
#define POLLOUT 0x0004
#define POLLHUP 0x0010

struct pollfd {
  int fd;
  short events;
  short revents;
};

int poll(struct pollfd *fds, int nfds, int timeout_ticks);

#endif /* _USR_POLL_H */
