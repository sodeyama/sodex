#include <fs.h>
#include <vga.h>
#include <ext3fs.h>
#include <process.h>
#include <tty.h>
#include <memory.h>

PUBLIC int files_find_free_fd(struct files_struct *files, int start_fd)
{
  int fd;

  if (files == NULL)
    return -1;

  if (start_fd < 0)
    start_fd = 0;
  for (fd = start_fd; fd < FILEDESC_MAX; fd++) {
    if (files->fs_fd[fd] == NULL)
      return fd;
  }
  for (fd = 0; fd < start_fd; fd++) {
    if (files->fs_fd[fd] == NULL)
      return fd;
  }
  return -1;
}

PUBLIC int files_alloc_fd(struct files_struct *files, struct file *file)
{
  int fd;

  if (files == NULL || file == NULL)
    return -1;

  fd = files_find_free_fd(files, 0);
  if (fd < 0)
    return -1;

  files->fs_fd[fd] = file;
  files->fs_freefd = fd + 1;
  return fd;
}

PUBLIC struct file *file_get(struct file *file)
{
  if (file != NULL)
    file->f_refcount++;
  return file;
}

PUBLIC int file_put(struct file *file)
{
  if (file == NULL)
    return TRUE;

  if (file->f_refcount > 0)
    file->f_refcount--;
  if (file->f_refcount > 0)
    return TRUE;

  if (file->f_ops != NULL && file->f_ops->close != NULL)
    file->f_ops->close(file);
  kfree(file);
  return TRUE;
}

PUBLIC int files_dup(struct files_struct *files, int oldfd)
{
  struct file *file;

  if (files == NULL || oldfd < 0 || oldfd >= FILEDESC_MAX)
    return -1;

  file = files->fs_fd[oldfd];
  if (file == NULL)
    return -1;

  return files_alloc_fd(files, file_get(file));
}

PUBLIC int files_clone(struct files_struct *dst, struct files_struct *src)
{
  int fd;

  if (dst == NULL || src == NULL)
    return FALSE;

  memset(dst, 0, sizeof(struct files_struct));
  for (fd = 0; fd < FILEDESC_MAX; fd++) {
    if (src->fs_fd[fd] != NULL)
      dst->fs_fd[fd] = file_get(src->fs_fd[fd]);
  }
  dst->fs_freefd = src->fs_freefd;
  return TRUE;
}

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
  struct file* file = FD_TOFILE(fd, current);
  if (fd < 0 || fd >= FILEDESC_MAX)
    return FALSE;

  FD_TOFILE(fd, current) = NULL;
  return file_put(file);
}

PUBLIC int fs_stdio_open(struct files_struct* ftask)
{
  return fs_stdio_open_tty(ftask, tty_get_console());
}

PUBLIC int fs_stdio_open_tty(struct files_struct* ftask, struct tty *tty)
{
  return tty_install_stdio(ftask, tty);
}
