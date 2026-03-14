#include <pipe.h>
#include <memory.h>
#include <process.h>

struct pipe_info {
  char *buffer;
  size_t read_pos;
  size_t write_pos;
  size_t data_size;
  int read_open;
  int write_open;
};

PRIVATE ssize_t pipe_read(struct file *file, void *buf, size_t count);
PRIVATE ssize_t pipe_write(struct file *file, const void *buf, size_t count);
PRIVATE int pipe_close(struct file *file);
PRIVATE struct file *pipe_new_file(int stdioflag, struct pipe_info *pipe);

PRIVATE const struct file_ops g_pipe_read_ops = {
  pipe_read,
  NULL,
  pipe_close,
};

PRIVATE const struct file_ops g_pipe_write_ops = {
  NULL,
  pipe_write,
  pipe_close,
};

PRIVATE ssize_t pipe_read(struct file *file, void *buf, size_t count)
{
  struct pipe_info *pipe;
  size_t total;
  size_t first_chunk;

  if (file == NULL || buf == NULL)
    return -1;

  pipe = (struct pipe_info *)file->private_data;
  if (pipe == NULL)
    return -1;
  if (pipe->data_size == 0)
    return pipe->write_open == 0 ? 0 : 0;

  total = count;
  if (total > pipe->data_size)
    total = pipe->data_size;

  first_chunk = PIPE_BUFFER_SIZE - pipe->read_pos;
  if (first_chunk > total)
    first_chunk = total;
  memcpy(buf, pipe->buffer + pipe->read_pos, first_chunk);
  if (total > first_chunk)
    memcpy((char*)buf + first_chunk, pipe->buffer, total - first_chunk);

  pipe->read_pos = (pipe->read_pos + total) % PIPE_BUFFER_SIZE;
  pipe->data_size -= total;
  return (ssize_t)total;
}

PRIVATE ssize_t pipe_write(struct file *file, const void *buf, size_t count)
{
  struct pipe_info *pipe;
  size_t total;
  size_t first_chunk;

  if (file == NULL || buf == NULL)
    return -1;

  pipe = (struct pipe_info *)file->private_data;
  if (pipe == NULL)
    return -1;
  if (pipe->read_open == 0)
    return -1;

  total = count;
  if (total > PIPE_BUFFER_SIZE - pipe->data_size)
    total = PIPE_BUFFER_SIZE - pipe->data_size;
  if (total == 0)
    return 0;

  first_chunk = PIPE_BUFFER_SIZE - pipe->write_pos;
  if (first_chunk > total)
    first_chunk = total;
  memcpy(pipe->buffer + pipe->write_pos, buf, first_chunk);
  if (total > first_chunk)
    memcpy(pipe->buffer, (char*)buf + first_chunk, total - first_chunk);

  pipe->write_pos = (pipe->write_pos + total) % PIPE_BUFFER_SIZE;
  pipe->data_size += total;
  return (ssize_t)total;
}

PRIVATE int pipe_close(struct file *file)
{
  struct pipe_info *pipe;

  if (file == NULL)
    return TRUE;

  pipe = (struct pipe_info *)file->private_data;
  if (pipe == NULL)
    return TRUE;

  if (file->f_stdioflag == FLAG_PIPE_READ && pipe->read_open > 0)
    pipe->read_open--;
  if (file->f_stdioflag == FLAG_PIPE_WRITE && pipe->write_open > 0)
    pipe->write_open--;

  if (pipe->read_open == 0 && pipe->write_open == 0) {
    if (pipe->buffer != NULL)
      kfree(pipe->buffer);
    kfree(pipe);
  }
  return TRUE;
}

PRIVATE struct file *pipe_new_file(int stdioflag, struct pipe_info *pipe)
{
  struct file *file;

  file = kalloc(sizeof(struct file));
  if (file == NULL)
    return NULL;

  memset(file, 0, sizeof(struct file));
  file->f_stdioflag = stdioflag;
  file->f_refcount = 1;
  file->private_data = pipe;
  if (stdioflag == FLAG_PIPE_READ)
    file->f_ops = &g_pipe_read_ops;
  else
    file->f_ops = &g_pipe_write_ops;
  return file;
}

PUBLIC int pipe(int fd[2])
{
  struct pipe_info *pipe;
  struct file *read_file;
  struct file *write_file;
  int read_fd;
  int write_fd;

  if (fd == NULL || current == NULL || current->files == NULL)
    return -1;

  pipe = kalloc(sizeof(struct pipe_info));
  if (pipe == NULL)
    return -1;
  memset(pipe, 0, sizeof(struct pipe_info));

  pipe->buffer = kalloc(PIPE_BUFFER_SIZE);
  if (pipe->buffer == NULL) {
    kfree(pipe);
    return -1;
  }
  memset(pipe->buffer, 0, PIPE_BUFFER_SIZE);
  pipe->read_open = 1;
  pipe->write_open = 1;

  read_file = pipe_new_file(FLAG_PIPE_READ, pipe);
  write_file = pipe_new_file(FLAG_PIPE_WRITE, pipe);
  if (read_file == NULL || write_file == NULL) {
    if (read_file != NULL)
      kfree(read_file);
    if (write_file != NULL)
      kfree(write_file);
    kfree(pipe->buffer);
    kfree(pipe);
    return -1;
  }

  read_fd = files_alloc_fd(current->files, read_file);
  if (read_fd < 0) {
    kfree(read_file);
    kfree(write_file);
    kfree(pipe->buffer);
    kfree(pipe);
    return -1;
  }

  write_fd = files_alloc_fd(current->files, write_file);
  if (write_fd < 0) {
    current->files->fs_fd[read_fd] = NULL;
    kfree(read_file);
    kfree(write_file);
    kfree(pipe->buffer);
    kfree(pipe);
    return -1;
  }

  fd[0] = read_fd;
  fd[1] = write_fd;
  return 0;
}
