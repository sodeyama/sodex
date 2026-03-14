#ifndef _PIPE_H
#define _PIPE_H

#include <fs.h>

#define PIPE_BUFFER_SIZE 4096

PUBLIC int pipe(int fd[2]);

#endif
