#ifndef _FS_H
#define _FS_H

#include <sys/types.h>
#include <sodex/list.h>
#include <ext3fs.h>
#include <process.h>

#define O_ACCMODE   00000003
#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR      00000002
#define O_CREAT     00000100
#define O_EXCL      00000200
#define O_NOCTTY    00000400
#define O_TRUNC     00001000
#define O_APPEND    00002000
#define O_NONBLOCK  00004000
#define O_SYNC      00010000
#define FASYNC      00020000
#define O_DIRECT    00040000    /* direct disk access hint */
#define O_LARGEFILE 00100000
#define O_DIRECTORY 00200000    /* must be a directory */
#define O_NOFOLLOW  00400000
#define O_NOATIME   01000000
#define O_NDELAY    O_NONBLOCK

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

#define PATHNAME_MAX 4096

#define FILEDESC_MAX 32

#define FS_OPEN_FAIL -1

#define MKDIR_SUCCESS 0
#define MKDIR_FAIL    -1

#define STDIN_FILENO	0
#define STDOUT_FILENO	1
#define STDERR_FILENO	2

#define FLAG_STDIN		0
#define FLAG_STDOUT		1
#define FLAG_STDERR		2
#define FLAG_FILE		3

#define FD_TOINODE(fd, task) (task->files->fs_fd[fd]->f_dentry->d_inode)
#define FD_TODENTRY(fd, task) (task->files->fs_fd[fd]->f_dentry)
#define FD_TOFILE(fd, task) (task->files->fs_fd[fd])

struct files_struct {
  struct file *fs_fd[FILEDESC_MAX];
  int fs_freefd;
};

struct file {
  struct _ext3_dentry  *f_dentry;
  mode_t                f_mode;
  u_int32_t				f_flags;
  off_t                 f_pos;
  u_int32_t             f_pid;
  u_int32_t             f_uid;
  u_int32_t             f_euid;
  u_int32_t             f_error;
  u_int32_t				f_stdioflag;
};

PUBLIC off_t lseek(int fd, off_t offset, int whence);
PUBLIC int close(int fd);
PUBLIC int fs_stdio_open();

#endif
