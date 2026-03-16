#ifndef _EXECVE_H
#define _EXECVE_H

#include <sys/types.h>

struct tty;

#define ARGV_MAX_NUMS 32
#define ARGV_MAX_LEN 128

PUBLIC void kernel_execve(const char *filename, char *const argv[],
                          char *const envp[]);
PUBLIC pid_t kernel_execve_tty(const char *filename, char *const argv[],
                               struct tty *stdio_tty);
PUBLIC pid_t sys_execve(const char *filename, char *const argv[],
                        char *const envp[]);
PUBLIC pid_t sys_execve_pty(const char *filename, char *const argv[],
                            int master_fd);
PUBLIC pid_t sys_fork();

#endif
