#ifndef _POLL_H
#define _POLL_H

#include <sodex/const.h>
#include <sys/types.h>

#define POLLIN  0x0001
#define POLLOUT 0x0004
#define POLLHUP 0x0010

struct pollfd {
  int fd;
  short events;
  short revents;
};

PUBLIC int sys_poll(struct pollfd *fds, int nfds, int timeout_ticks);
PUBLIC void poll_notify_all(void);

#endif /* _POLL_H */
