#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sodex/const.h>
#include <sys/types.h>

#define SIGHUP       1
#define SIGINT       2
#define SIGQUIT      3
#define SIGILL       4
#define SIGTRAP      5
#define SIGABRT      6
#define SIGIOT       6
#define SIGUNUSED    7
#define SIGFPE       8
#define SIGKILL      9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22

#define MAX_SIGNALS 32

typedef void (*sighandler_t)(int);

struct sigaction {
  void (*sa_handler)(int);
  u_int32_t    sa_mask;
};

#endif
