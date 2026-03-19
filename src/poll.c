#include <poll.h>
#include <process.h>
#include <fs.h>
#include <pipe.h>
#include <socket.h>
#include <tty.h>

PRIVATE struct wait_queue *poll_waitq = 0;

PRIVATE short poll_scan_socket(int fd, short events)
{
  struct kern_socket *sk;
  short revents = 0;

  if (fd < 0 || fd >= MAX_SOCKETS)
    return 0;

  sk = &socket_table[fd];
  if (sk->state == SOCK_STATE_UNUSED)
    return 0;

  if (sk->state == SOCK_STATE_CLOSED)
    revents |= POLLHUP;

  if ((events & POLLIN) != 0) {
    if (sk->state == SOCK_STATE_LISTENING) {
      if (sk->backlog_count > 0)
        revents |= POLLIN;
    } else if (sk->rx_len > 0) {
      revents |= POLLIN;
    }
  }

  if ((events & POLLOUT) != 0) {
    if (sk->state != SOCK_STATE_CLOSED &&
        sk->state != SOCK_STATE_UNUSED &&
        (sk->type != SOCK_STREAM || sk->tx_pending == 0)) {
      revents |= POLLOUT;
    }
  }

  return revents;
}

PRIVATE short poll_scan_tty(struct file *file, short events)
{
  struct tty *tty;
  short revents = 0;

  if (file == 0)
    return 0;

  tty = (struct tty *)file->private_data;
  if (tty == 0)
    return 0;

  if (file->f_stdioflag == FLAG_TTY_MASTER) {
    if ((events & POLLIN) != 0 &&
        tty->master_rx.head != tty->master_rx.tail) {
      revents |= POLLIN;
    }
    if ((events & POLLOUT) != 0 && tty->slave_refs > 0)
      revents |= POLLOUT;
    if (tty->slave_refs <= 0)
      revents |= POLLHUP;
    return revents;
  }

  if (file->f_stdioflag == FLAG_TTY_SLAVE) {
    if ((events & POLLIN) != 0 &&
        tty->slave_rx.head != tty->slave_rx.tail) {
      revents |= POLLIN;
    }
    if ((events & POLLOUT) != 0 && tty->has_master)
      revents |= POLLOUT;
    if (!tty->has_master)
      revents |= POLLHUP;
  }

  return revents;
}

PRIVATE short poll_scan_fd(struct pollfd *pfd)
{
  int fd;
  struct file *file;

  if (pfd == 0)
    return 0;

  fd = pfd->fd;
  if (fd < 0)
    return 0;

  if (fd < MAX_SOCKETS && socket_table[fd].state != SOCK_STATE_UNUSED)
    return poll_scan_socket(fd, pfd->events);

  if (current == 0 || current->files == 0)
    return 0;
  if (fd >= FILEDESC_MAX)
    return 0;

  file = current->files->fs_fd[fd];
  if (file == 0)
    return 0;

  if (file->f_stdioflag == FLAG_TTY_MASTER ||
      file->f_stdioflag == FLAG_TTY_SLAVE) {
    return poll_scan_tty(file, pfd->events);
  }

  if (file->f_stdioflag == FLAG_PIPE_READ ||
      file->f_stdioflag == FLAG_PIPE_WRITE) {
    return pipe_poll_events(file, pfd->events);
  }

  return 0;
}

PRIVATE int poll_scan(struct pollfd *fds, int nfds)
{
  int i;
  int ready = 0;

  for (i = 0; i < nfds; i++) {
    short revents = poll_scan_fd(&fds[i]);

    fds[i].revents = revents;
    if (revents != 0)
      ready++;
  }

  return ready;
}

PUBLIC void poll_notify_all(void)
{
  wakeup(&poll_waitq);
}

PUBLIC int sys_poll(struct pollfd *fds, int nfds, int timeout_ticks)
{
  u_int32_t deadline = 0;

  if (fds == 0 && nfds > 0)
    return -1;
  if (nfds < 0)
    return -1;

  if (timeout_ticks > 0)
    deadline = kernel_tick + (u_int32_t)timeout_ticks;

  for (;;) {
    int ready = poll_scan(fds, nfds);

    if (ready > 0)
      return ready;
    if (timeout_ticks == 0)
      return 0;
    if (timeout_ticks < 0) {
      sleep_on(&poll_waitq);
      continue;
    }
    if ((int)(kernel_tick - deadline) >= 0)
      return 0;

    sleep_on_timeout(&poll_waitq, (u_int32_t)(deadline - kernel_tick));
  }
}
