#ifndef _EXECVE_H
#define _EXECVE_H

#include <sys/types.h>

#define ARGV_MAX_NUMS 4
#define ARGV_MAX_LEN 16

PUBLIC void kernel_execve(const char *filename, char *const argv[],
                          char *const envp[]);
PUBLIC pid_t sys_execve(const char *filename, char *const argv[],
                        char *const envp[]);
PUBLIC pid_t sys_fork();

#endif
