#ifdef TEST_BUILD
#ifndef PUBLIC
#define PUBLIC
#endif
#else
#include <sys/types.h>
#include <string.h>
#include <io.h>
#include <socket.h>
#endif

#include <ssh_server.h>

#ifndef TEST_BUILD
PRIVATE int ssh_listener_fd = -1;
PRIVATE int ssh_listener_port = 0;

PRIVATE void ssh_fill_bind_addr(struct sockaddr_in *addr, u_int16_t port)
{
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr = 0;
}

PRIVATE int ssh_create_listener(int port)
{
  struct sockaddr_in addr;
  int fd = kern_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd < 0)
    return -1;

  ssh_fill_bind_addr(&addr, (u_int16_t)port);
  if (kern_bind(fd, &addr) < 0) {
    kern_close_socket(fd);
    return -1;
  }
  if (kern_listen(fd, SOCK_ACCEPT_BACKLOG_SIZE) < 0) {
    kern_close_socket(fd);
    return -1;
  }

  admin_runtime_note_listener_ready(ADMIN_LISTENER_SSH);
  return fd;
}

PUBLIC void ssh_server_init(void)
{
  ssh_listener_fd = -1;
  ssh_listener_port = 0;
}

PUBLIC void ssh_server_tick(void)
{
  int port;

  if (!admin_runtime_ssh_enabled())
    return;

  port = admin_runtime_ssh_port();
  if (port <= 0)
    return;

  if (ssh_listener_fd < 0 || ssh_listener_port != port) {
    if (ssh_listener_fd >= 0)
      kern_close_socket(ssh_listener_fd);
    ssh_listener_fd = ssh_create_listener(port);
    ssh_listener_port = (ssh_listener_fd >= 0) ? port : 0;
  }
}
#else
PUBLIC void ssh_server_init(void) {}
PUBLIC void ssh_server_tick(void) {}
#endif
