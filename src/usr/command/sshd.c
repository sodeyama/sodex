#include "../../include/ssh_server.h"
#include "../include/poll.h"
#include "../include/sleep.h"

static int sshd_fd_ready(int fd, short events)
{
  struct pollfd pfd;

  if (fd < 0)
    return 0;

  pfd.fd = fd;
  pfd.events = events;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) < 0)
    return 0;
  return (pfd.revents & events) != 0;
}

static void sshd_wait_once(void)
{
  if (ssh_userland_connection_pending())
    return;
  if (ssh_userland_listener_pending())
    return;
  if (sshd_fd_ready(ssh_userland_channel_fd(), POLLIN | POLLHUP))
    return;

  /* userland では blocking poll の wakeup 依存を避け、1 tick ごとに再評価する。 */
  sleep_ticks(1);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  ssh_userland_bootstrap();
  for (;;) {
    sshd_wait_once();
    ssh_userland_sync_tick();
    ssh_userland_refresh_runtime();
    ssh_server_tick();
  }

  return 0;
}
