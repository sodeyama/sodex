#include <fs.h>
#include <vga.h>
#include <ext3fs.h>
#include <process.h>

PUBLIC off_t lseek(int fd, off_t offset, int whence)
{
  struct file* file = FD_TOFILE(fd, current);
  if (file == NULL) {
    _kprintf("%s:translate fd to file error\n", __func__);
    return 0;
  }

  switch (whence) {
  case SEEK_SET:
    file->f_pos = offset;
    break;
  case SEEK_CUR:
    file->f_pos += offset;
    break;
  case SEEK_END:
    {
      ext3_inode* inode = FD_TOINODE(fd, current);
      file->f_pos = inode->i_size - offset;
    }
    break;
  }
  return file->f_pos;
}

PUBLIC int close(int fd)
{
  FD_TOFILE(fd, current) = NULL;
  return TRUE;
}

PUBLIC int fs_stdio_open(struct files_struct* ftask)
{
  struct file* fs_stdin = kalloc(sizeof(struct file));
  memset(fs_stdin, 0, sizeof(struct file));
  fs_stdin->f_stdioflag = FLAG_STDIN;
  ftask->fs_fd[ftask->fs_freefd++] = fs_stdin;

  struct file* fs_stdout = kalloc(sizeof(struct file));
  memset(fs_stdout, 0, sizeof(struct file));
  fs_stdout->f_stdioflag = FLAG_STDOUT;
  ftask->fs_fd[ftask->fs_freefd++] = fs_stdout;

  struct file* fs_stderr = kalloc(sizeof(struct file));
  memset(fs_stderr, 0, sizeof(struct file));
  fs_stderr->f_stdioflag = FLAG_STDOUT;
  ftask->fs_fd[ftask->fs_freefd++] = fs_stderr;   
}
