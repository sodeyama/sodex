#ifdef TEST_BUILD
#include <string.h>

#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef PRIVATE
#define PRIVATE static
#endif
#else
#include <sys/types.h>
#include <string.h>
#include <io.h>
#include <socket.h>
#include <execve.h>
#include <process.h>
#include <rs232c.h>
#include <tty.h>
EXTERN volatile u_int32_t kernel_tick;
#endif

#include <debug_shell_server.h>

#define DEBUG_SHELL_PREFACE_MAX ADMIN_TEXT_REQUEST_MAX
#define DEBUG_SHELL_AUDIT_LINE_SIZE 96

PRIVATE void debug_shell_copy_string(char *dest, int cap, const char *src)
{
  int i = 0;

  if (dest == 0 || cap <= 0)
    return;
  if (src == 0) {
    dest[0] = '\0';
    return;
  }

  while (src[i] != '\0' && i < cap - 1) {
    dest[i] = src[i];
    i++;
  }
  dest[i] = '\0';
}

PRIVATE void debug_shell_trim_line(const char *line, int len,
                                   char *out, int out_cap)
{
  int start = 0;
  int end = len;
  int pos = 0;

  if (out == 0 || out_cap <= 0)
    return;

  while (start < len && (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }
  while (end > start &&
         (line[end - 1] == '\r' || line[end - 1] == '\n' ||
          line[end - 1] == ' ' || line[end - 1] == '\t')) {
    end--;
  }

  while (start < end && pos < out_cap - 1) {
    out[pos++] = line[start++];
  }
  out[pos] = '\0';
}

PUBLIC int debug_shell_parse_preface(const char *line, int len,
                                     char *token, int token_cap)
{
  char trimmed[DEBUG_SHELL_PREFACE_MAX];
  char *value;

  if (line == 0 || token == 0 || token_cap <= 0)
    return -1;

  token[0] = '\0';
  debug_shell_trim_line(line, len, trimmed, sizeof(trimmed));
  if (strncmp(trimmed, "TOKEN ", 6) != 0)
    return -1;

  value = trimmed + 6;
  while (*value == ' ') {
    value++;
  }
  if (*value == '\0' || strchr(value, ' ') != 0 || strchr(value, '\t') != 0)
    return -1;

  debug_shell_copy_string(token, token_cap, value);
  return 0;
}

#ifndef TEST_BUILD
#define DEBUG_SHELL_AUTH_TIMEOUT_TICKS 500
#define DEBUG_SHELL_IDLE_TIMEOUT_TICKS 3000
#define DEBUG_SHELL_CLOSE_DRAIN_TICKS 60
#define DEBUG_SHELL_IO_CHUNK 256
#define DEBUG_SHELL_WINSIZE_COLS 80
#define DEBUG_SHELL_WINSIZE_ROWS 25

struct debug_shell_connection {
  int in_use;
  int fd;
  int authenticated;
  int start_pending;
  int start_in_progress;
  int closing;
  int close_started_tick;
  int close_initiated;
  u_int32_t peer_addr;
  u_int32_t accepted_tick;
  u_int32_t last_activity_tick;
  int length;
  char buffer[DEBUG_SHELL_PREFACE_MAX];
  struct tty *tty;
  pid_t shell_pid;
};

PRIVATE int debug_shell_listener_fd = -1;
PRIVATE int debug_shell_listener_port = 0;
PRIVATE struct debug_shell_connection debug_shell_conn;
PRIVATE struct tty *debug_shell_cleanup_tty = 0;
PRIVATE pid_t debug_shell_cleanup_pid = -1;

PRIVATE void debug_shell_format_ip(u_int32_t peer_addr, char *buf, int cap)
{
  u_int8_t *raw = (u_int8_t *)&peer_addr;
  int pos = 0;
  int part;

  if (buf == 0 || cap <= 0)
    return;

  buf[0] = '\0';
  for (part = 0; part < 4; part++) {
    int value = raw[part];
    char digits[4];
    int len = 0;

    if (part > 0 && pos < cap - 1)
      buf[pos++] = '.';
    if (value == 0) {
      digits[len++] = '0';
    } else {
      while (value > 0 && len < (int)sizeof(digits)) {
        digits[len++] = (char)('0' + (value % 10));
        value /= 10;
      }
    }
    while (len > 0 && pos < cap - 1) {
      buf[pos++] = digits[--len];
    }
  }
  buf[pos] = '\0';
}

PRIVATE void debug_shell_audit_peer(const char *prefix, u_int32_t peer_addr)
{
  char message[DEBUG_SHELL_AUDIT_LINE_SIZE];
  char ipbuf[24];

  debug_shell_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  message[0] = '\0';
  debug_shell_copy_string(message, sizeof(message), prefix);
  debug_shell_copy_string(message + strlen(message),
                          (int)sizeof(message) - (int)strlen(message),
                          " peer=");
  debug_shell_copy_string(message + strlen(message),
                          (int)sizeof(message) - (int)strlen(message),
                          ipbuf);
  admin_runtime_audit_line(message);
}

PRIVATE void debug_shell_audit_start(u_int32_t peer_addr, pid_t pid)
{
  char message[DEBUG_SHELL_AUDIT_LINE_SIZE];
  char ipbuf[24];
  char pidbuf[16];
  int value = (int)pid;
  int len = 0;

  debug_shell_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  if (value == 0) {
    pidbuf[len++] = '0';
  } else {
    char digits[16];
    while (value > 0 && len < (int)sizeof(digits)) {
      digits[len++] = (char)('0' + (value % 10));
      value /= 10;
    }
    {
      int i;
      for (i = 0; i < len; i++) {
        pidbuf[i] = digits[len - i - 1];
      }
    }
  }
  pidbuf[len] = '\0';

  message[0] = '\0';
  debug_shell_copy_string(message, sizeof(message), "debug_shell_start peer=");
  debug_shell_copy_string(message + strlen(message),
                          (int)sizeof(message) - (int)strlen(message),
                          ipbuf);
  debug_shell_copy_string(message + strlen(message),
                          (int)sizeof(message) - (int)strlen(message),
                          " pid=");
  debug_shell_copy_string(message + strlen(message),
                          (int)sizeof(message) - (int)strlen(message),
                          pidbuf);
  admin_runtime_audit_line(message);
}

PRIVATE void debug_shell_audit_close(u_int32_t peer_addr, const char *reason)
{
  char message[DEBUG_SHELL_AUDIT_LINE_SIZE];
  char ipbuf[24];

  debug_shell_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  message[0] = '\0';
  debug_shell_copy_string(message, sizeof(message), "debug_shell_close peer=");
  debug_shell_copy_string(message + strlen(message),
                          (int)sizeof(message) - (int)strlen(message),
                          ipbuf);
  if (reason != 0 && reason[0] != '\0') {
    debug_shell_copy_string(message + strlen(message),
                            (int)sizeof(message) - (int)strlen(message),
                            " reason=");
    debug_shell_copy_string(message + strlen(message),
                            (int)sizeof(message) - (int)strlen(message),
                            reason);
  }
  admin_runtime_audit_line(message);
}

PRIVATE void debug_shell_fill_bind_addr(struct sockaddr_in *addr, u_int16_t port)
{
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr = 0;
}

PRIVATE void debug_shell_reset_connection(struct debug_shell_connection *conn)
{
  memset(conn, 0, sizeof(struct debug_shell_connection));
  conn->fd = -1;
}

PRIVATE int debug_shell_extract_line_length(const char *buffer, int length)
{
  int i;

  for (i = 0; i < length; i++) {
    if (buffer[i] == '\n')
      return i + 1;
  }
  return -1;
}

PRIVATE int debug_shell_create_listener(int port)
{
  struct sockaddr_in addr;
  int fd = kern_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  char message[DEBUG_SHELL_AUDIT_LINE_SIZE];

  if (fd < 0)
    return -1;

  debug_shell_fill_bind_addr(&addr, (u_int16_t)port);
  if (kern_bind(fd, &addr) < 0) {
    kern_close_socket(fd);
    return -1;
  }
  if (kern_listen(fd, SOCK_ACCEPT_BACKLOG_SIZE) < 0) {
    kern_close_socket(fd);
    return -1;
  }

  message[0] = '\0';
  debug_shell_copy_string(message, sizeof(message),
                          "listener_ready kind=debug_shell port=");
  {
    char portbuf[8];
    int value = port;
    int len = 0;

    if (value == 0) {
      portbuf[len++] = '0';
    } else {
      char digits[8];
      while (value > 0 && len < (int)sizeof(digits)) {
        digits[len++] = (char)('0' + (value % 10));
        value /= 10;
      }
      {
        int i;
        for (i = 0; i < len; i++) {
          portbuf[i] = digits[len - i - 1];
        }
      }
    }
    portbuf[len] = '\0';
    debug_shell_copy_string(message + strlen(message),
                            (int)sizeof(message) - (int)strlen(message),
                            portbuf);
  }
  admin_runtime_audit_line(message);
  return fd;
}

PRIVATE void debug_shell_send_and_close(struct debug_shell_connection *conn,
                                        const char *response)
{
  if (conn == 0 || conn->fd < 0)
    return;
  if (response != 0 && socket_table[conn->fd].tx_pending == 0)
    kern_send(conn->fd, (void *)response, (int)strlen(response), 0);
  conn->closing = TRUE;
  conn->close_started_tick = kernel_tick;
  conn->close_initiated = FALSE;
}

PRIVATE void debug_shell_queue_response(struct debug_shell_connection *conn,
                                        const char *response)
{
  struct kern_socket *sk;
  int len;

  if (conn == 0 || conn->fd < 0 || response == 0)
    return;

  sk = &socket_table[conn->fd];
  if (sk->state == SOCK_STATE_UNUSED)
    return;

  len = (int)strlen(response);
  if (len > SOCK_TXBUF_SIZE)
    len = SOCK_TXBUF_SIZE;
  memcpy(sk->tx_buf, response, len);
  sk->tx_len = (u_int16_t)len;
  sk->tx_pending = 1;
}

PRIVATE void debug_shell_release_connection(struct debug_shell_connection *conn,
                                            const char *reason)
{
  if (conn == 0 || !conn->in_use)
    return;

  if (conn->shell_pid > 0) {
    sys_kill(conn->shell_pid, SIGKILL);
    debug_shell_cleanup_pid = conn->shell_pid;
    debug_shell_cleanup_tty = conn->tty;
    conn->shell_pid = -1;
    conn->tty = 0;
  }
  if (conn->fd >= 0)
    kern_close_socket(conn->fd);
  if (conn->tty != 0)
    tty_release(conn->tty);

  debug_shell_audit_close(conn->peer_addr, reason);
  debug_shell_reset_connection(conn);
}

PRIVATE void debug_shell_poll_cleanup(void)
{
  if (debug_shell_cleanup_pid <= 0)
    return;
  if (process_has_pid(debug_shell_cleanup_pid))
    return;

  if (debug_shell_cleanup_tty != 0)
    tty_release(debug_shell_cleanup_tty);
  debug_shell_cleanup_tty = 0;
  debug_shell_cleanup_pid = -1;
}

PRIVATE int debug_shell_start_session(struct debug_shell_connection *conn)
{
  struct tty *tty;
  char *shell_argv[2];
  pid_t pid;

  tty = tty_alloc_pty();
  if (tty == 0)
    return -1;

  conn->start_in_progress = TRUE;
  tty_set_winsize(tty, DEBUG_SHELL_WINSIZE_COLS, DEBUG_SHELL_WINSIZE_ROWS);
  shell_argv[0] = "eshell";
  shell_argv[1] = 0;
  pid = kernel_execve_tty("/usr/bin/eshell", shell_argv, tty);
  if (pid < 0) {
    conn->start_in_progress = FALSE;
    tty_release(tty);
    return -1;
  }

  conn->tty = tty;
  conn->shell_pid = pid;
  conn->authenticated = TRUE;
  conn->start_pending = FALSE;
  conn->start_in_progress = FALSE;
  conn->last_activity_tick = kernel_tick;
  debug_shell_audit_start(conn->peer_addr, pid);
  debug_shell_queue_response(conn, "OK shell\n");
  return 0;
}

PRIVATE void debug_shell_accept_pending_connections(void)
{
  for (;;) {
    struct sockaddr_in peer;
    int child_fd;
    int auth_result;

    disableInterrupt();
    child_fd = socket_try_accept(debug_shell_listener_fd, &peer);
    enableInterrupt();
    if (child_fd < 0)
      return;

    auth_result = admin_authorize_peer(peer.sin_addr, 0);
    if (auth_result != ADMIN_AUTH_ALLOW) {
      kern_send(child_fd,
                auth_result == ADMIN_AUTH_THROTTLED
                    ? "ERR throttled\n"
                    : "ERR forbidden\n",
                auth_result == ADMIN_AUTH_THROTTLED ? 14 : 14, 0);
      kern_close_socket(child_fd);
      continue;
    }

    if (debug_shell_conn.in_use) {
      debug_shell_audit_peer("reject_busy_debug_shell", peer.sin_addr);
      kern_send(child_fd, "ERR busy\n", 9, 0);
      kern_close_socket(child_fd);
      continue;
    }

    debug_shell_conn.in_use = TRUE;
    debug_shell_conn.fd = child_fd;
    debug_shell_conn.peer_addr = peer.sin_addr;
    debug_shell_conn.accepted_tick = kernel_tick;
    debug_shell_conn.last_activity_tick = kernel_tick;
    debug_shell_conn.length = 0;
    debug_shell_conn.authenticated = FALSE;
    debug_shell_conn.start_pending = FALSE;
    debug_shell_conn.start_in_progress = FALSE;
    debug_shell_conn.closing = FALSE;
    debug_shell_conn.tty = 0;
    debug_shell_conn.shell_pid = -1;
    debug_shell_audit_peer("accept_debug_shell", peer.sin_addr);
  }
}

PRIVATE void debug_shell_handle_preface(struct debug_shell_connection *conn)
{
  int line_len;
  char token[ADMIN_TOKEN_MAX];
  struct admin_request req;
  int auth_result;

  line_len = debug_shell_extract_line_length(conn->buffer, conn->length);
  if (line_len < 0)
    return;

  if (debug_shell_parse_preface(conn->buffer, line_len,
                                token, sizeof(token)) < 0) {
    debug_shell_audit_peer("invalid_debug_shell", conn->peer_addr);
    debug_shell_send_and_close(conn, "ERR invalid_preface\n");
    return;
  }

  memset(&req, 0, sizeof(req));
  req.required_role = ADMIN_ROLE_CONTROL;
  debug_shell_copy_string(req.token, sizeof(req.token), token);
  auth_result = admin_authorize_request_detailed(&req, conn->peer_addr, 0);
  if (auth_result != ADMIN_AUTH_ALLOW) {
    debug_shell_send_and_close(conn,
                               auth_result == ADMIN_AUTH_THROTTLED
                                   ? "ERR throttled\n"
                                   : "ERR unauthorized\n");
    return;
  }

  /* 最小 bridge として、upgrade 後の同一 packet 残り入力は後回しにする。 */
  conn->length = 0;
  conn->buffer[0] = '\0';
  conn->start_pending = TRUE;
  conn->last_activity_tick = kernel_tick;
}

PRIVATE void debug_shell_pump_socket_to_tty(struct debug_shell_connection *conn)
{
  char chunk[DEBUG_SHELL_IO_CHUNK];
  int read_len;

  if (conn->start_pending || conn->start_in_progress)
    return;

  for (;;) {
    disableInterrupt();
    read_len = rxbuf_read_direct(conn->fd, (u_int8_t *)chunk, sizeof(chunk), 0);
    enableInterrupt();
    if (read_len <= 0)
      break;

    conn->last_activity_tick = kernel_tick;
    if (!conn->authenticated) {
      if (conn->length + read_len >= DEBUG_SHELL_PREFACE_MAX) {
        debug_shell_audit_peer("too_large_debug_shell", conn->peer_addr);
        debug_shell_send_and_close(conn, "ERR too_large\n");
        return;
      }
      memcpy(conn->buffer + conn->length, chunk, read_len);
      conn->length += read_len;
      conn->buffer[conn->length] = '\0';
      debug_shell_handle_preface(conn);
      if (conn->closing)
        return;
      continue;
    }

    tty_master_write(conn->tty, chunk, (size_t)read_len);
  }
}

PRIVATE void debug_shell_pump_tty_to_socket(struct debug_shell_connection *conn)
{
  char chunk[DEBUG_SHELL_IO_CHUNK];
  int read_len;

  if (!conn->authenticated || conn->tty == 0)
    return;
  if (socket_table[conn->fd].tx_pending != 0)
    return;

  read_len = (int)tty_master_read(conn->tty, chunk, sizeof(chunk));
  if (read_len <= 0)
    return;

  conn->last_activity_tick = kernel_tick;
  kern_send(conn->fd, chunk, read_len, 0);
}

PRIVATE void debug_shell_poll_connection(struct debug_shell_connection *conn)
{
  u_int32_t timeout_ticks;

  if (conn == 0 || !conn->in_use)
    return;

  if (socket_table[conn->fd].state == SOCK_STATE_CLOSED) {
    debug_shell_release_connection(conn, "disconnect");
    return;
  }

  timeout_ticks = conn->authenticated ? DEBUG_SHELL_IDLE_TIMEOUT_TICKS
                                      : DEBUG_SHELL_AUTH_TIMEOUT_TICKS;
  if (!conn->closing &&
      (int)(kernel_tick - conn->last_activity_tick) > (int)timeout_ticks) {
    debug_shell_audit_peer("timeout_debug_shell", conn->peer_addr);
    debug_shell_send_and_close(conn, "ERR timeout\n");
  }

  if (conn->closing) {
    if (!conn->close_initiated &&
        (int)(kernel_tick - conn->close_started_tick) >=
            DEBUG_SHELL_CLOSE_DRAIN_TICKS) {
      socket_begin_close(conn->fd);
      conn->close_initiated = TRUE;
    }
    return;
  }

  if (conn->start_in_progress)
    return;

  if (conn->start_pending) {
    if (debug_shell_start_session(conn) < 0) {
      debug_shell_audit_peer("debug_shell_spawn_failed", conn->peer_addr);
      conn->start_pending = FALSE;
      conn->start_in_progress = FALSE;
      debug_shell_send_and_close(conn, "ERR unavailable\n");
    }
    return;
  }

  debug_shell_pump_socket_to_tty(conn);
  if (conn->closing)
    return;
  debug_shell_pump_tty_to_socket(conn);
}

PUBLIC void debug_shell_server_init(void)
{
  debug_shell_listener_fd = -1;
  debug_shell_listener_port = 0;
  debug_shell_reset_connection(&debug_shell_conn);
  debug_shell_cleanup_tty = 0;
  debug_shell_cleanup_pid = -1;
}

PUBLIC void debug_shell_server_tick(void)
{
  int port;

  if (!admin_runtime_debug_shell_enabled())
    return;

  port = admin_runtime_debug_shell_port();
  if (port <= 0)
    return;

  if (debug_shell_listener_fd < 0 || debug_shell_listener_port != port) {
    if (debug_shell_listener_fd >= 0)
      kern_close_socket(debug_shell_listener_fd);
    debug_shell_listener_fd = debug_shell_create_listener(port);
    debug_shell_listener_port = (debug_shell_listener_fd >= 0) ? port : 0;
    if (debug_shell_listener_fd < 0)
      return;
  }

  debug_shell_poll_cleanup();
  debug_shell_accept_pending_connections();
  debug_shell_poll_connection(&debug_shell_conn);
}
#else
PUBLIC void debug_shell_server_init(void) {}
PUBLIC void debug_shell_server_tick(void) {}
#endif
