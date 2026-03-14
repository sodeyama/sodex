#ifndef _USR_TTY_H
#define _USR_TTY_H

#include <sys/types.h>

#define INPUT_MODE_CONSOLE 0
#define INPUT_MODE_RAW     1

int openpty(void);
pid_t execve_pty(const char *filename, char *const argv[], int master_fd);
int set_input_mode(int mode);

#endif
