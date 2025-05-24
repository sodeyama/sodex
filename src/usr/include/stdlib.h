#ifndef _STDLIB_H
#define _STDLIB_H

#include <sodex/const.h>
#include <sys/types.h>
#include <ext3fs.h>
#include <process.h>
#include <signal.h>

#define CEIL(a, b) ((a&(~(b-1)))+b)

extern ssize_t write(int fd, const void *buf, size_t count);
extern int open(const char *pathname, int flags, mode_t mode);
extern ssize_t read(int fd, void *buf, size_t count);
extern int execve(const char *filename, char *const argv[],
                  char *const envp[]);
extern void exit(int status);
extern off_t lseek(int fildes, off_t offset, int whence);
extern int mkdir(const char *pathname, mode_t mode);
extern int creat(const char *pathname, mode_t mode);
extern ext3_dentry* getdentry();
extern struct task_struct* get_pstat();
extern int _brk(void* end_data_segment);
extern pid_t waitpid(pid_t pid, int *status, int options);
extern pid_t fork(void);
extern sighandler_t signal(int signum, sighandler_t sighandler);
extern int kill(pid_t pid, int sig);

int atoi(const char* nptr);


#endif /* _STDLIB_H */
