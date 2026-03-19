#ifndef _PIPE_H
#define _PIPE_H

#include <fs.h>
#include <poll.h>

#define PIPE_BUFFER_SIZE 4096

PUBLIC int pipe(int fd[2]);
PUBLIC short pipe_poll_events(struct file *file, short events);

#endif
