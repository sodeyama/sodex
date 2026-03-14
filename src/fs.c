#include <fs.h>
#include <vga.h>
#include <ext3fs.h>
#include <process.h>
#include <tty.h>

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
  if (file != NULL && file->f_ops != NULL && file->f_ops->close != NULL) {
    file->f_ops->close(file);
  }
  FD_TOFILE(fd, current) = NULL;
  return TRUE;
}

PUBLIC int fs_stdio_open(struct files_struct* ftask)
{
  return fs_stdio_open_tty(ftask, tty_get_console());
}

PUBLIC int fs_stdio_open_tty(struct files_struct* ftask, struct tty *tty)
{
  return tty_install_stdio(ftask, tty);
}
