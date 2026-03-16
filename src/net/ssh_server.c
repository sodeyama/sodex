#ifdef TEST_BUILD
#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef PRIVATE
#define PRIVATE static
#endif
#elif defined(USERLAND_SSHD_BUILD)
#include <sodex/const.h>
#include <admin_server.h>
#include <server_audit.h>
#include <server_runtime_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <signal.h>
#include <sys/socket.h>
#include "../usr/include/tty.h"
#include "../usr/include/winsize.h"
#include "../usr/include/poll.h"
#include <debug.h>
#include <sleep.h>
#include "../usr/include/termios.h"

#define MAX_SOCKETS 16
#define SOCK_STATE_UNUSED     0
#define SOCK_STATE_CREATED    1
#define SOCK_STATE_BOUND      2
#define SOCK_STATE_LISTENING  3
#define SOCK_STATE_CONNECTED  4
#define SOCK_STATE_CLOSED     5
#define SOCK_ACCEPT_BACKLOG_SIZE 4
#define TASK_ZOMBIE 3
#define SSH_USERLAND_AUDIT_LINE_SIZE 96

struct sockaddr_in {
  u_int16_t sin_family;
  u_int16_t sin_port;
  u_int32_t sin_addr;
};

struct kern_socket {
  int state;
  int type;
  int protocol;
  u_int8_t tx_pending;
};

struct tty {
  int active;
  int fd;
};

volatile u_int32_t kernel_tick = 0;
PRIVATE struct kern_socket socket_table[MAX_SOCKETS];
PRIVATE struct tty ssh_userland_ttys[2];
PRIVATE struct admin_ssh_config ssh_userland_runtime;
PRIVATE int ssh_userland_config_loaded = FALSE;
PRIVATE int ssh_userland_listener_ready = FALSE;
PRIVATE void ssh_audit_state(const char *prefix, int value);
extern u_int32_t get_kernel_tick(void);
extern struct task_struct *getpstat(void);

PRIVATE void disableInterrupt(void) {}
PRIVATE void enableInterrupt(void) {}
PRIVATE void network_poll(void) {}

PRIVATE u_int16_t htons(u_int16_t hostshort)
{
  return (u_int16_t)(((hostshort & 0x00ffU) << 8) |
                     ((hostshort & 0xff00U) >> 8));
}

#define memcmp ssh_userland_memcmp
#define network_fill_gateway_addr ssh_userland_network_fill_gateway_addr

PRIVATE int ssh_userland_memcmp(const void *lhs, const void *rhs, size_t len)
{
  const u_int8_t *a = (const u_int8_t *)lhs;
  const u_int8_t *b = (const u_int8_t *)rhs;
  size_t i;

  for (i = 0; i < len; i++) {
    if (a[i] != b[i])
      return (int)a[i] - (int)b[i];
  }
  return 0;
}

PRIVATE void ssh_userland_copy_string(char *dest, int cap, const char *src)
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

PRIVATE void ssh_userland_reset_config(void)
{
  memset(&ssh_userland_runtime, 0, sizeof(ssh_userland_runtime));
  ssh_userland_listener_ready = FALSE;
}

PRIVATE void ssh_userland_trim_line(const char *line, int len,
                                    char *out, int out_cap)
{
  int start = 0;
  int end = len;
  int pos = 0;

  if (out == 0 || out_cap <= 0) {
    return;
  }

  while (start < end &&
         (line[start] == ' ' || line[start] == '\t' ||
          line[start] == '\r' || line[start] == '\n')) {
    start++;
  }
  while (end > start &&
         (line[end - 1] == ' ' || line[end - 1] == '\t' ||
          line[end - 1] == '\r' || line[end - 1] == '\n')) {
    end--;
  }

  while (start < end && pos < out_cap - 1) {
    out[pos++] = line[start++];
  }
  out[pos] = '\0';
}

PRIVATE int ssh_userland_parse_ip(const char *text, u_int32_t *out)
{
  u_int8_t parts[4];
  int index = 0;
  int value = 0;
  int have_digit = FALSE;

  if (text == 0 || out == 0)
    return -1;

  memset(parts, 0, sizeof(parts));
  while (*text != '\0') {
    if (*text >= '0' && *text <= '9') {
      value = value * 10 + (*text - '0');
      if (value > 255)
        return -1;
      have_digit = TRUE;
    } else if (*text == '.') {
      if (!have_digit || index >= 3)
        return -1;
      parts[index++] = (u_int8_t)value;
      value = 0;
      have_digit = FALSE;
    } else {
      return -1;
    }
    text++;
  }

  if (!have_digit || index != 3)
    return -1;

  parts[index] = (u_int8_t)value;
  *out = ((u_int32_t)parts[0]) |
         ((u_int32_t)parts[1] << 8) |
         ((u_int32_t)parts[2] << 16) |
         ((u_int32_t)parts[3] << 24);
  return 0;
}

PRIVATE void ssh_userland_apply_config_line(const char *line)
{
  char trimmed[256];
  char key[64];
  char value[192];
  char *sep;
  int key_len;

  ssh_userland_trim_line(line, (int)strlen(line), trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0' || trimmed[0] == '#')
    return;

  sep = strchr(trimmed, '=');
  if (sep == 0)
    return;

  key_len = (int)(sep - trimmed);
  if (key_len <= 0 || key_len >= (int)sizeof(key))
    return;
  memcpy(key, trimmed, (size_t)key_len);
  key[key_len] = '\0';
  ssh_userland_trim_line(sep + 1, (int)strlen(sep + 1), value, sizeof(value));

  if (strcmp(key, "allow_ip") == 0) {
    u_int32_t allow_ip;

    if (ssh_userland_parse_ip(value, &allow_ip) == 0)
      ssh_userland_runtime.allow_ip = allow_ip;
    return;
  }
  if (strcmp(key, "ssh_port") == 0) {
    ssh_userland_runtime.ssh_port = (u_int16_t)atoi(value);
    return;
  }
  if (strcmp(key, "ssh_password") == 0) {
    ssh_userland_copy_string(ssh_userland_runtime.ssh_password,
                             sizeof(ssh_userland_runtime.ssh_password), value);
    return;
  }
  if (strcmp(key, "ssh_signer_port") == 0) {
    ssh_userland_runtime.ssh_signer_port = (u_int16_t)atoi(value);
    return;
  }
  if (strcmp(key, "ssh_hostkey_ed25519_seed") == 0) {
    ssh_userland_copy_string(ssh_userland_runtime.ssh_hostkey_ed25519_seed,
                             sizeof(ssh_userland_runtime.ssh_hostkey_ed25519_seed),
                             value);
    return;
  }
  if (strcmp(key, "ssh_hostkey_ed25519_public") == 0) {
    ssh_userland_copy_string(ssh_userland_runtime.ssh_hostkey_ed25519_public,
                             sizeof(ssh_userland_runtime.ssh_hostkey_ed25519_public),
                             value);
    return;
  }
  if (strcmp(key, "ssh_hostkey_ed25519_secret") == 0) {
    ssh_userland_copy_string(ssh_userland_runtime.ssh_hostkey_ed25519_secret,
                             sizeof(ssh_userland_runtime.ssh_hostkey_ed25519_secret),
                             value);
    return;
  }
  if (strcmp(key, "ssh_rng_seed") == 0) {
    ssh_userland_copy_string(ssh_userland_runtime.ssh_rng_seed,
                             sizeof(ssh_userland_runtime.ssh_rng_seed), value);
  }
}

PRIVATE int ssh_userland_load_config(void)
{
  if (ssh_userland_config_loaded)
    return 0;

  ssh_userland_reset_config();
  if (get_server_ssh_config(&ssh_userland_runtime) < 0)
    return -1;

  ssh_userland_config_loaded = TRUE;
  return 0;
}

PRIVATE int ssh_userland_has_hostkey(void)
{
  return (ssh_userland_runtime.ssh_hostkey_ed25519_public[0] != '\0' &&
          ssh_userland_runtime.ssh_hostkey_ed25519_secret[0] != '\0') ||
         ssh_userland_runtime.ssh_hostkey_ed25519_seed[0] != '\0';
}

PUBLIC int server_runtime_ssh_enabled(void)
{
  ssh_userland_load_config();
  return ssh_userland_runtime.ssh_port != 0 &&
         ssh_userland_runtime.ssh_password[0] != '\0' &&
         ssh_userland_has_hostkey() &&
         ssh_userland_runtime.ssh_rng_seed[0] != '\0';
}

PUBLIC int server_runtime_ssh_port(void)
{
  ssh_userland_load_config();
  return (int)ssh_userland_runtime.ssh_port;
}

PUBLIC const char *server_runtime_ssh_password(void)
{
  ssh_userland_load_config();
  return ssh_userland_runtime.ssh_password;
}

PUBLIC int server_runtime_ssh_signer_port(void)
{
  ssh_userland_load_config();
  return (int)ssh_userland_runtime.ssh_signer_port;
}

PUBLIC const char *server_runtime_ssh_hostkey_ed25519_seed(void)
{
  ssh_userland_load_config();
  return ssh_userland_runtime.ssh_hostkey_ed25519_seed;
}

PUBLIC const char *server_runtime_ssh_hostkey_ed25519_public(void)
{
  ssh_userland_load_config();
  return ssh_userland_runtime.ssh_hostkey_ed25519_public;
}

PUBLIC const char *server_runtime_ssh_hostkey_ed25519_secret(void)
{
  ssh_userland_load_config();
  return ssh_userland_runtime.ssh_hostkey_ed25519_secret;
}

PUBLIC const char *server_runtime_ssh_rng_seed(void)
{
  ssh_userland_load_config();
  return ssh_userland_runtime.ssh_rng_seed;
}

PUBLIC void server_audit_line(const char *line)
{
  if (line == 0 || line[0] == '\0')
    return;

  debug_write("AUDIT ", 6);
  debug_write(line, strlen(line));
  debug_write("\n", 1);
}

PUBLIC void server_audit_note_listener_ready(int listener_kind)
{
  char message[SSH_USERLAND_AUDIT_LINE_SIZE];
  int pos = 0;

  if (listener_kind != ADMIN_LISTENER_SSH || ssh_userland_listener_ready)
    return;

  ssh_userland_listener_ready = TRUE;
  ssh_userland_copy_string(message, sizeof(message), "listener_ready kind=ssh port=");
  pos = (int)strlen(message);
  if (server_runtime_ssh_port() >= 10000) {
    message[pos++] = (char)('0' + (server_runtime_ssh_port() / 10000) % 10);
  }
  if (server_runtime_ssh_port() >= 1000) {
    message[pos++] = (char)('0' + (server_runtime_ssh_port() / 1000) % 10);
  }
  if (server_runtime_ssh_port() >= 100) {
    message[pos++] = (char)('0' + (server_runtime_ssh_port() / 100) % 10);
  }
  if (server_runtime_ssh_port() >= 10) {
    message[pos++] = (char)('0' + (server_runtime_ssh_port() / 10) % 10);
  }
  message[pos++] = (char)('0' + (server_runtime_ssh_port() % 10));
  message[pos] = '\0';
  server_audit_line(message);
}

PUBLIC int admin_authorize_peer(u_int32_t peer_addr, u_int32_t *retry_after_ticks)
{
  ssh_userland_load_config();
  if (retry_after_ticks != 0)
    *retry_after_ticks = 0;
  if (ssh_userland_runtime.allow_ip != 0 &&
      peer_addr != ssh_userland_runtime.allow_ip) {
    return ADMIN_AUTH_DENY;
  }
  return ADMIN_AUTH_ALLOW;
}

PUBLIC int kern_socket(int domain, int type, int protocol)
{
  int fd = socket(domain, type, protocol);

  if (fd >= 0 && fd < MAX_SOCKETS) {
    memset(&socket_table[fd], 0, sizeof(socket_table[fd]));
    socket_table[fd].state = SOCK_STATE_CREATED;
    socket_table[fd].type = type;
    socket_table[fd].protocol = protocol;
  }
  return fd;
}

PUBLIC int kern_bind(int sockfd, struct sockaddr_in *addr)
{
  int ret = bind(sockfd, (const struct sockaddr *)addr, sizeof(*addr));

  if (ret == 0 && sockfd >= 0 && sockfd < MAX_SOCKETS)
    socket_table[sockfd].state = SOCK_STATE_BOUND;
  return ret;
}

PUBLIC int kern_listen(int sockfd, int backlog)
{
  int ret = listen(sockfd, backlog);

  if (ret == 0 && sockfd >= 0 && sockfd < MAX_SOCKETS)
    socket_table[sockfd].state = SOCK_STATE_LISTENING;
  return ret;
}

PRIVATE void ssh_userland_refresh_socket(int sockfd)
{
  struct pollfd pfd;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return;
  if (socket_table[sockfd].state == SOCK_STATE_UNUSED)
    return;

  pfd.fd = sockfd;
  pfd.events = POLLIN | POLLOUT | POLLHUP;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) < 0)
    return;
  if ((pfd.revents & POLLOUT) != 0)
    socket_table[sockfd].tx_pending = 0;
  if ((pfd.revents & POLLHUP) != 0)
    socket_table[sockfd].state = SOCK_STATE_CLOSED;
}

PUBLIC int socket_try_accept(int sockfd, struct sockaddr_in *addr)
{
  int child_fd;

  child_fd = accept_nowait(sockfd, (struct sockaddr *)addr, 0);
  if (child_fd < 0) {
    return child_fd;
  }
  server_audit_line("ssh_accept_nowait_ok");
  if (child_fd >= 0 && child_fd < MAX_SOCKETS) {
    memset(&socket_table[child_fd], 0, sizeof(socket_table[child_fd]));
    socket_table[child_fd].state = SOCK_STATE_CONNECTED;
    socket_table[child_fd].type = SOCK_STREAM;
    socket_table[child_fd].protocol = IPPROTO_TCP;
  }
  return child_fd;
}

PUBLIC int kern_accept(int sockfd, struct sockaddr_in *addr)
{
  int child_fd;

  child_fd = accept(sockfd, (struct sockaddr *)addr, 0);
  if (child_fd < 0)
    return child_fd;
  if (child_fd >= 0 && child_fd < MAX_SOCKETS) {
    memset(&socket_table[child_fd], 0, sizeof(socket_table[child_fd]));
    socket_table[child_fd].state = SOCK_STATE_CONNECTED;
    socket_table[child_fd].type = SOCK_STREAM;
    socket_table[child_fd].protocol = IPPROTO_TCP;
  }
  return child_fd;
}

PUBLIC int kern_connect(int sockfd, struct sockaddr_in *addr)
{
  int ret = connect(sockfd, (const struct sockaddr *)addr, sizeof(*addr));

  if (ret == 0 && sockfd >= 0 && sockfd < MAX_SOCKETS)
    socket_table[sockfd].state = SOCK_STATE_CONNECTED;
  return ret;
}

PUBLIC int kern_send(int sockfd, void *buf, int len, int flags)
{
  int ret = send_msg(sockfd, buf, len, flags);

  if (ret >= 0 && sockfd >= 0 && sockfd < MAX_SOCKETS &&
      socket_table[sockfd].type == SOCK_STREAM) {
    socket_table[sockfd].tx_pending = 1;
  }
  return ret;
}

PUBLIC int kern_recv(int sockfd, void *buf, int len, int flags)
{
  int ret = recv_msg(sockfd, buf, len, flags);

  if (ret == 0 && sockfd >= 0 && sockfd < MAX_SOCKETS &&
      socket_table[sockfd].state != SOCK_STATE_UNUSED) {
    socket_table[sockfd].state = SOCK_STATE_CLOSED;
    socket_table[sockfd].tx_pending = 0;
  }
  ssh_userland_refresh_socket(sockfd);
  return ret;
}

PUBLIC int kern_sendto(int sockfd, void *buf, int len, int flags,
                       struct sockaddr_in *addr)
{
  return sendto(sockfd, buf, len, flags, (const struct sockaddr *)addr,
                sizeof(*addr));
}

PUBLIC int kern_recvfrom(int sockfd, void *buf, int len, int flags,
                         struct sockaddr_in *addr)
{
  int ret = recvfrom(sockfd, buf, len, flags, (struct sockaddr *)addr, 0);

  if (ret == 0 && sockfd >= 0 && sockfd < MAX_SOCKETS &&
      socket_table[sockfd].state != SOCK_STATE_UNUSED) {
    socket_table[sockfd].state = SOCK_STATE_CLOSED;
    socket_table[sockfd].tx_pending = 0;
  }
  ssh_userland_refresh_socket(sockfd);
  return ret;
}

PUBLIC int rxbuf_read_direct(int sockfd, u_int8_t *buf, u_int16_t maxlen,
                             struct sockaddr_in *from)
{
  int ret;

  ret = kern_recvfrom(sockfd, buf, (int)maxlen, 0, from);
  if (ret > 0)
    ssh_audit_state("ssh_rx_direct_len", ret);
  return ret;
}

PUBLIC int socket_begin_close(int sockfd)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return -1;

  closesocket(sockfd);
  socket_table[sockfd].state = SOCK_STATE_CLOSED;
  socket_table[sockfd].tx_pending = 0;
  return 0;
}

PUBLIC int kern_close_socket(int sockfd)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return -1;
  if (socket_table[sockfd].state == SOCK_STATE_UNUSED)
    return 0;

  closesocket(sockfd);
  memset(&socket_table[sockfd], 0, sizeof(socket_table[sockfd]));
  socket_table[sockfd].state = SOCK_STATE_UNUSED;
  return 0;
}

PUBLIC void ssh_userland_network_fill_gateway_addr(struct sockaddr_in *addr,
                                                   u_int16_t port)
{
  if (addr == 0)
    return;

  memset(addr, 0, sizeof(*addr));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  if (ssh_userland_runtime.allow_ip != 0) {
    addr->sin_addr = ssh_userland_runtime.allow_ip;
  } else {
    addr->sin_addr = ((u_int32_t)10) |
                     ((u_int32_t)0 << 8) |
                     ((u_int32_t)2 << 16) |
                     ((u_int32_t)2 << 24);
  }
}

PUBLIC struct tty *tty_alloc_pty(void)
{
  int i;

  for (i = 0; i < (int)(sizeof(ssh_userland_ttys) / sizeof(ssh_userland_ttys[0])); i++) {
    int fd;

    if (ssh_userland_ttys[i].active)
      continue;
    fd = openpty();
    if (fd < 0)
      return 0;
    ssh_userland_ttys[i].active = TRUE;
    ssh_userland_ttys[i].fd = fd;
    return &ssh_userland_ttys[i];
  }
  return 0;
}

PUBLIC void tty_release(struct tty *tty)
{
  if (tty == 0 || !tty->active)
    return;
  close(tty->fd);
  tty->fd = -1;
  tty->active = FALSE;
}

PUBLIC int tty_set_winsize(struct tty *tty, u_int16_t cols, u_int16_t rows)
{
  struct winsize winsize;

  if (tty == 0 || !tty->active)
    return -1;

  winsize.cols = cols;
  winsize.rows = rows;
  return set_winsize(tty->fd, &winsize);
}

PUBLIC pid_t tty_get_foreground_pid(struct tty *tty)
{
  if (tty == 0 || !tty->active)
    return 0;
  return get_foreground_pid(tty->fd);
}

PUBLIC ssize_t tty_master_read(struct tty *tty, void *buf, size_t count)
{
  if (tty == 0 || !tty->active)
    return 0;
  return read(tty->fd, buf, count);
}

PUBLIC ssize_t tty_master_write(struct tty *tty, const void *buf, size_t count)
{
  if (tty == 0 || !tty->active)
    return 0;
  return write(tty->fd, buf, count);
}

PUBLIC pid_t kernel_execve_tty(const char *filename, char *const argv[],
                               struct tty *tty)
{
  if (tty == 0 || !tty->active)
    return -1;
  return execve_pty(filename, argv, tty->fd);
}

PUBLIC int process_has_pid(pid_t pid)
{
  struct task_struct *proc;
  struct dlist_set *list;

  if (pid < 0)
    return FALSE;

  proc = (struct task_struct *)getpstat();
  if (proc == 0)
    return FALSE;

  list = &proc->run_list;
  while (TRUE) {
    struct task_struct *task =
        dlist_entry(list, struct task_struct, run_list);

    if (task->pid == pid) {
      if (task->state == TASK_ZOMBIE) {
        waitpid(pid, 0, 0);
        return FALSE;
      }
      return TRUE;
    }
    list = list->next;
    if (list == &proc->run_list)
      break;
  }
  return FALSE;
}

PUBLIC int sys_kill(pid_t pid, int sig)
{
  return kill(pid, sig);
}
#else
#include <sys/types.h>
#include <string.h>
#include <io.h>
#include <socket.h>
#include <execve.h>
#include <process.h>
#include <signal.h>
#include <tty.h>
#include <uip.h>
#include <network_config.h>
#endif

#include <admin_server.h>
#include <ssh_server.h>
#include <ssh_packet_core.h>
#include <ssh_auth_core.h>
#include <ssh_channel_core.h>
#include <ssh_crypto.h>
#include <ssh_runtime_policy.h>

#ifndef TEST_BUILD

#define SSH_BANNER "SSH-2.0-SodexSSH_0.1\r\n"
#define SSH_CLOSE_DRAIN_TICKS 40
#define SSH_RX_BUFFER_SIZE 4096
#define SSH_PACKET_PLAIN_MAX 2048
#define SSH_PAYLOAD_MAX 1792
#define SSH_OUTBOX_MAX 4
#define SSH_PACKET_OUT_MAX 1460
#define SSH_BANNER_MAX 128
#define SSH_AUDIT_LINE_SIZE 96
#define SSH_CHANNEL_WINDOW 65536U
#define SSH_CHANNEL_MAX_PACKET 1024U
#define SSH_DEFAULT_COLS 80
#define SSH_DEFAULT_ROWS 25
#define SSH_SIGNER_MAGIC "SIG1"
#define SSH_SIGNER_MAGIC_BYTES 4
#define SSH_SIGNER_REQUEST_BYTES \
  (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_SHA256_BYTES)
#define SSH_SIGNER_RESPONSE_BYTES \
  (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_ED25519_SIGNATURE_BYTES)
#define SSH_CURVE25519_MAGIC "KEX1"
#define SSH_CURVE25519_REQUEST_BYTES \
  (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES + \
   SSH_CRYPTO_CURVE25519_BYTES)
#define SSH_CURVE25519_RESPONSE_BYTES \
  (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES + \
   SSH_CRYPTO_CURVE25519_BYTES)
#define SSH_SIGNER_RECV_RETRIES 32

#define SSH_MSG_DISCONNECT 1
#define SSH_MSG_IGNORE 2
#define SSH_MSG_UNIMPLEMENTED 3
#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_SERVICE_ACCEPT 6
#define SSH_MSG_KEXINIT 20
#define SSH_MSG_NEWKEYS 21
#define SSH_MSG_KEX_ECDH_INIT 30
#define SSH_MSG_KEX_ECDH_REPLY 31
#define SSH_MSG_USERAUTH_REQUEST 50
#define SSH_MSG_USERAUTH_FAILURE 51
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_GLOBAL_REQUEST 80
#define SSH_MSG_REQUEST_SUCCESS 81
#define SSH_MSG_REQUEST_FAILURE 82
#define SSH_MSG_CHANNEL_OPEN 90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_OPEN_FAILURE 92
#define SSH_MSG_CHANNEL_WINDOW_ADJUST 93
#define SSH_MSG_CHANNEL_DATA 94
#define SSH_MSG_CHANNEL_EOF 96
#define SSH_MSG_CHANNEL_CLOSE 97
#define SSH_MSG_CHANNEL_REQUEST 98
#define SSH_MSG_CHANNEL_SUCCESS 99
#define SSH_MSG_CHANNEL_FAILURE 100

struct ssh_out_packet {
  int len;
  int activate_tx_crypto;
  u_int8_t data[SSH_PACKET_OUT_MAX];
};

struct ssh_channel_state {
  int open;
  int pty_requested;
  int shell_start_pending;
  int shell_start_in_progress;
  int shell_reply_pending;
  int prompt_kick_pending;
  u_int32_t shell_start_tick;
  u_int32_t shell_started_tick;
  int tx_prev_cr;
  int close_sent;
  int eof_sent;
  int exit_status_sent;
  int peer_close;
  u_int32_t peer_id;
  u_int32_t local_id;
  u_int32_t peer_window;
  u_int32_t peer_max_packet;
  u_int16_t cols;
  u_int16_t rows;
  struct tty *tty;
  pid_t shell_pid;
};

struct ssh_connection {
  int in_use;
  int fd;
  int closing;
  int close_initiated;
  u_int32_t close_started_tick;
  u_int32_t peer_addr;
  u_int32_t accepted_tick;
  u_int32_t last_activity_tick;
  int banner_sent;
  int banner_received;
  int kexinit_sent;
  int kexinit_received;
  int newkeys_sent;
  int newkeys_received;
  int tx_encrypted;
  int rx_encrypted;
  int auth_done;
  int auth_failures;
  int auth_pending_failure;
  int auth_pending_close;
  u_int32_t auth_delay_until_tick;
  int auth_identity_set;
  int userauth_service_ready;
  char username[32];
  char auth_service[32];
  char banner_buf[SSH_BANNER_MAX];
  int banner_len;
  u_int8_t rx_buf[SSH_RX_BUFFER_SIZE];
  int rx_len;
  u_int32_t tx_seq;
  u_int32_t rx_seq;
  struct ssh_aes_ctr_ctx tx_cipher;
  struct ssh_aes_ctr_ctx rx_cipher;
  u_int8_t tx_mac_key[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t rx_mac_key[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t client_kexinit[SSH_PAYLOAD_MAX];
  int client_kexinit_len;
  u_int8_t server_kexinit[SSH_PAYLOAD_MAX];
  int server_kexinit_len;
  u_int8_t session_id[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t exchange_hash[SSH_CRYPTO_SHA256_BYTES];
  int session_id_set;
  struct ssh_out_packet outbox[SSH_OUTBOX_MAX];
  int out_head;
  int out_tail;
  int out_count;
  struct ssh_channel_state channel;
};

PRIVATE int ssh_listener_fd = -1;
PRIVATE int ssh_listener_port = 0;
PRIVATE int ssh_signer_fd = -1;
PRIVATE struct ssh_connection ssh_conn;
PRIVATE struct tty *ssh_cleanup_tty = 0;
PRIVATE pid_t ssh_cleanup_pid = -1;
PRIVATE u_int8_t ssh_hostkey_public[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES];
PRIVATE u_int8_t ssh_hostkey_secret[SSH_CRYPTO_ED25519_SECRETKEY_BYTES];
PRIVATE u_int8_t ssh_runtime_rng_seed[SSH_CRYPTO_SEED_BYTES];
PRIVATE u_int32_t ssh_runtime_rng_state[4];
PRIVATE u_int8_t ssh_kex_hostkey_blob[128];
PRIVATE u_int8_t ssh_kex_signature_blob[128];
PRIVATE u_int8_t ssh_kex_reply[256];
PRIVATE u_int8_t ssh_kex_server_secret[SSH_CRYPTO_CURVE25519_BYTES];
PRIVATE u_int8_t ssh_kex_server_public[SSH_CRYPTO_CURVE25519_BYTES];
PRIVATE u_int8_t ssh_kex_shared_secret[SSH_CRYPTO_CURVE25519_BYTES];
PRIVATE u_int8_t ssh_kex_signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES];
PRIVATE u_int8_t ssh_kex_hash_input[SSH_PAYLOAD_MAX * 2 + 512];
struct ssh_key_material_workspace {
  u_int8_t input[256];
  u_int8_t rx_iv[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t tx_iv[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t rx_key[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t tx_key[SSH_CRYPTO_SHA256_BYTES];
};
PRIVATE struct ssh_key_material_workspace ssh_key_material_ws;
PRIVATE u_int8_t ssh_queue_packet_buf[SSH_PACKET_OUT_MAX];
PRIVATE u_int8_t ssh_queue_hmac_input[4 + SSH_PACKET_PLAIN_MAX];
PRIVATE u_int8_t ssh_decode_packet_buf[SSH_PACKET_PLAIN_MAX];
PRIVATE u_int8_t ssh_decode_payload_buf[SSH_PAYLOAD_MAX];
PRIVATE u_int8_t ssh_channel_payload_buf[SSH_PAYLOAD_MAX];
PRIVATE int ssh_runtime_loaded = FALSE;
PRIVATE int ssh_tick_active = FALSE;
PRIVATE void ssh_close_signer_socket(void);
PRIVATE void ssh_interrupt_foreground(struct ssh_connection *conn);

PRIVATE u_int32_t ssh_runtime_random_rotl(u_int32_t value, int bits)
{
  return (value << bits) | (value >> (32 - bits));
}

PRIVATE u_int32_t ssh_runtime_random_load_u32(const u_int8_t *buf)
{
  return (u_int32_t)buf[0] |
         ((u_int32_t)buf[1] << 8) |
         ((u_int32_t)buf[2] << 16) |
         ((u_int32_t)buf[3] << 24);
}

PRIVATE void ssh_runtime_random_reset(void)
{
  ssh_runtime_rng_state[0] =
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 0) ^
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 16) ^
      0x9e3779b9U;
  ssh_runtime_rng_state[1] =
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 4) ^
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 20) ^
      0x243f6a88U;
  ssh_runtime_rng_state[2] =
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 8) ^
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 24) ^
      0xb7e15162U;
  ssh_runtime_rng_state[3] =
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 12) ^
      ssh_runtime_random_load_u32(ssh_runtime_rng_seed + 28) ^
      0xdeadbeefU;
  if ((ssh_runtime_rng_state[0] |
       ssh_runtime_rng_state[1] |
       ssh_runtime_rng_state[2] |
       ssh_runtime_rng_state[3]) == 0) {
    ssh_runtime_rng_state[0] = 0x9e3779b9U;
    ssh_runtime_rng_state[1] = 0x243f6a88U;
    ssh_runtime_rng_state[2] = 0xb7e15162U;
    ssh_runtime_rng_state[3] = 0xdeadbeefU;
  }
}

PRIVATE u_int32_t ssh_runtime_random_next_u32(void)
{
  u_int32_t result;
  u_int32_t t;

  result = ssh_runtime_random_rotl(ssh_runtime_rng_state[1] * 5U, 7) * 9U;
  t = ssh_runtime_rng_state[1] << 9;

  ssh_runtime_rng_state[2] ^= ssh_runtime_rng_state[0];
  ssh_runtime_rng_state[3] ^= ssh_runtime_rng_state[1];
  ssh_runtime_rng_state[1] ^= ssh_runtime_rng_state[2];
  ssh_runtime_rng_state[0] ^= ssh_runtime_rng_state[3];
  ssh_runtime_rng_state[2] ^= t;
  ssh_runtime_rng_state[3] =
      ssh_runtime_random_rotl(ssh_runtime_rng_state[3], 11);
  return result;
}

PRIVATE void ssh_runtime_random_fill(u_int8_t *out, int len)
{
  int written = 0;

  if (out == 0 || len <= 0)
    return;

  while (written < len) {
    u_int32_t value = ssh_runtime_random_next_u32();

    out[written++] = (u_int8_t)value;
    if (written < len)
      out[written++] = (u_int8_t)(value >> 8);
    if (written < len)
      out[written++] = (u_int8_t)(value >> 16);
    if (written < len)
      out[written++] = (u_int8_t)(value >> 24);
  }
}

PRIVATE void ssh_copy_string(char *dest, int cap, const char *src)
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

PRIVATE void ssh_format_ip(u_int32_t peer_addr, char *buf, int cap)
{
  u_int8_t *raw = (u_int8_t *)&peer_addr;
  int pos = 0;
  int part;

  if (buf == 0 || cap <= 0)
    return;

  buf[0] = '\0';
  for (part = 0; part < 4; part++) {
    char digits[4];
    int value = raw[part];
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

PRIVATE void ssh_audit_peer(const char *prefix, u_int32_t peer_addr)
{
  char message[SSH_AUDIT_LINE_SIZE];
  char ipbuf[24];

  ssh_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  message[0] = '\0';
  ssh_copy_string(message, sizeof(message), prefix);
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  " peer=");
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  ipbuf);
  server_audit_line(message);
}

PRIVATE void ssh_audit_start(u_int32_t peer_addr, pid_t pid)
{
  char message[SSH_AUDIT_LINE_SIZE];
  char ipbuf[24];
  char pidbuf[16];
  int value = (int)pid;
  int len = 0;

  ssh_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
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
  ssh_copy_string(message, sizeof(message), "ssh_session_start peer=");
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  ipbuf);
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  " pid=");
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  pidbuf);
  server_audit_line(message);
}

PRIVATE void ssh_audit_close(u_int32_t peer_addr, const char *reason)
{
  char message[SSH_AUDIT_LINE_SIZE];
  char ipbuf[24];

  ssh_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  message[0] = '\0';
  ssh_copy_string(message, sizeof(message), "ssh_close peer=");
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  ipbuf);
  if (reason != 0 && reason[0] != '\0') {
    ssh_copy_string(message + strlen(message),
                    (int)sizeof(message) - (int)strlen(message),
                    " reason=");
    ssh_copy_string(message + strlen(message),
                    (int)sizeof(message) - (int)strlen(message),
                    reason);
  }
  server_audit_line(message);
}

PRIVATE void ssh_audit_state(const char *prefix, int value)
{
  char message[SSH_AUDIT_LINE_SIZE];
  char digits[16];
  int current = value;
  int negative = FALSE;
  int len = 0;

  if (prefix == 0 || prefix[0] == '\0')
    return;

  if (current < 0) {
    negative = TRUE;
    current = -current;
  }
  if (current == 0) {
    digits[len++] = '0';
  } else {
    while (current > 0 && len < (int)sizeof(digits)) {
      digits[len++] = (char)('0' + (current % 10));
      current /= 10;
    }
  }

  message[0] = '\0';
  ssh_copy_string(message, sizeof(message), prefix);
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  "=");
  if (negative) {
    ssh_copy_string(message + strlen(message),
                    (int)sizeof(message) - (int)strlen(message),
                    "-");
  }
  while (len > 0) {
    char digit_text[2];

    digit_text[0] = digits[--len];
    digit_text[1] = '\0';
    ssh_copy_string(message + strlen(message),
                    (int)sizeof(message) - (int)strlen(message),
                    digit_text);
  }
  server_audit_line(message);
}

PRIVATE int ssh_socket_tx_pending(int sockfd)
{
#ifdef USERLAND_SSHD_BUILD
  struct pollfd pfd;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return FALSE;
  if (socket_table[sockfd].state == SOCK_STATE_UNUSED ||
      socket_table[sockfd].state == SOCK_STATE_CLOSED) {
    socket_table[sockfd].tx_pending = 0;
    return FALSE;
  }

  pfd.fd = sockfd;
  pfd.events = POLLOUT | POLLHUP;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) < 0) {
    server_audit_line("ssh_tx_poll_error");
    return socket_table[sockfd].tx_pending != 0;
  }
  if ((pfd.revents & POLLHUP) != 0) {
    socket_table[sockfd].state = SOCK_STATE_CLOSED;
    socket_table[sockfd].tx_pending = 0;
    return FALSE;
  }
  if ((pfd.revents & POLLOUT) != 0) {
    socket_table[sockfd].tx_pending = 0;
    return FALSE;
  }

  socket_table[sockfd].tx_pending = 1;
  return TRUE;
#else
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return FALSE;
  return socket_table[sockfd].tx_pending != 0;
#endif
}

PRIVATE int ssh_socket_rx_ready(int sockfd)
{
#ifdef USERLAND_SSHD_BUILD
  struct pollfd pfd;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return FALSE;
  if (socket_table[sockfd].state == SOCK_STATE_UNUSED ||
      socket_table[sockfd].state == SOCK_STATE_CLOSED) {
    return FALSE;
  }

  pfd.fd = sockfd;
  pfd.events = POLLIN | POLLHUP;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) < 0)
    return FALSE;
  if ((pfd.revents & POLLHUP) != 0) {
    ssh_userland_refresh_socket(sockfd);
    return FALSE;
  }
  return (pfd.revents & POLLIN) != 0;
#else
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return FALSE;
  return socket_table[sockfd].rx_len > 0;
#endif
}

PRIVATE void ssh_fill_bind_addr(struct sockaddr_in *addr, u_int16_t port)
{
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr = 0;
}

PRIVATE int ssh_reset_channel(struct ssh_channel_state *channel)
{
  memset(channel, 0, sizeof(*channel));
  channel->cols = SSH_DEFAULT_COLS;
  channel->rows = SSH_DEFAULT_ROWS;
  channel->peer_max_packet = SSH_CHANNEL_MAX_PACKET;
  channel->local_id = 0;
  channel->shell_pid = -1;
  return 0;
}

PRIVATE void ssh_reset_connection(struct ssh_connection *conn)
{
  memset(conn, 0, sizeof(*conn));
  conn->fd = -1;
  ssh_reset_channel(&conn->channel);
}

PRIVATE int ssh_load_runtime_config(void)
{
  u_int8_t seed[SSH_CRYPTO_SEED_BYTES];

  if (server_runtime_ssh_hostkey_ed25519_public()[0] != '\0' &&
      server_runtime_ssh_hostkey_ed25519_secret()[0] != '\0') {
    if (ssh_crypto_hex_to_bytes(server_runtime_ssh_hostkey_ed25519_public(),
                                ssh_hostkey_public,
                                sizeof(ssh_hostkey_public)) < 0) {
      ssh_runtime_loaded = FALSE;
      return -1;
    }
    if (ssh_crypto_hex_to_bytes(server_runtime_ssh_hostkey_ed25519_secret(),
                                ssh_hostkey_secret,
                                sizeof(ssh_hostkey_secret)) < 0) {
      ssh_runtime_loaded = FALSE;
      return -1;
    }
  } else {
    if (ssh_crypto_hex_to_bytes(server_runtime_ssh_hostkey_ed25519_seed(),
                                seed, sizeof(seed)) < 0) {
      ssh_runtime_loaded = FALSE;
      return -1;
    }
    if (ssh_crypto_ed25519_seed_keypair(ssh_hostkey_public, ssh_hostkey_secret,
                                        seed) < 0) {
      ssh_runtime_loaded = FALSE;
      return -1;
    }
  }
  if (ssh_crypto_hex_to_bytes(server_runtime_ssh_rng_seed(),
                              ssh_runtime_rng_seed,
                              sizeof(ssh_runtime_rng_seed)) < 0) {
    ssh_runtime_loaded = FALSE;
    return -1;
  }
  ssh_runtime_loaded = TRUE;
  return 0;
}

PRIVATE int ssh_create_listener(int port)
{
  struct sockaddr_in addr;
  int fd = kern_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd < 0)
    return -1;

  ssh_fill_bind_addr(&addr, (u_int16_t)port);
  ssh_audit_state("ssh_bind_port", port);
  ssh_audit_state("ssh_bind_port_raw", addr.sin_port);
  if (kern_bind(fd, &addr) < 0) {
    kern_close_socket(fd);
    return -1;
  }
  if (kern_listen(fd, SOCK_ACCEPT_BACKLOG_SIZE) < 0) {
    kern_close_socket(fd);
    return -1;
  }

  server_audit_note_listener_ready(ADMIN_LISTENER_SSH);
  return fd;
}

PRIVATE int ssh_send_banner(struct ssh_connection *conn)
{
  if (conn->banner_sent)
    return 0;
  if (ssh_socket_tx_pending(conn->fd)) {
    ssh_audit_state("ssh_banner_tx_pending", socket_table[conn->fd].tx_pending);
    return 0;
  }
  ssh_audit_state("ssh_send_banner_fd", conn->fd);
  if (kern_send(conn->fd, (void *)SSH_BANNER, (int)strlen(SSH_BANNER), 0) < 0) {
    server_audit_line("ssh_send_banner_failed");
    return -1;
  }
  conn->banner_sent = TRUE;
  conn->last_activity_tick = kernel_tick;
  server_audit_line("ssh_send_banner_ok");
  return 0;
}

PRIVATE int ssh_outbox_push(struct ssh_connection *conn,
                            const u_int8_t *data, int len,
                            int activate_tx_crypto)
{
  struct ssh_out_packet *packet;

  if (len <= 0 || len > SSH_PACKET_OUT_MAX)
    return -1;
  if (conn->out_count >= SSH_OUTBOX_MAX)
    return -1;

  packet = &conn->outbox[conn->out_head];
  memcpy(packet->data, data, (size_t)len);
  packet->len = len;
  packet->activate_tx_crypto = activate_tx_crypto;
  conn->out_head = (conn->out_head + 1) % SSH_OUTBOX_MAX;
  conn->out_count++;
  return 0;
}

PRIVATE int ssh_queue_payload(struct ssh_connection *conn,
                              const u_int8_t *payload, int payload_len,
                              int activate_tx_crypto)
{
  u_int8_t mac[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t seqbuf[4];
  struct ssh_aes_ctr_ctx cipher_copy;
  int block_size = conn->tx_encrypted ? 16 : 8;
  int padding_len = 4;
  int packet_length;
  int plain_len;
  int total_len;

  while (((payload_len + 1 + padding_len + 4) % block_size) != 0) {
    padding_len++;
  }

  packet_length = payload_len + padding_len + 1;
  plain_len = packet_length + 4;
  total_len = plain_len + (conn->tx_encrypted ? SSH_CRYPTO_SHA256_BYTES : 0);
  if (total_len > (int)sizeof(ssh_queue_packet_buf))
    return -1;

  ssh_write_u32_be(ssh_queue_packet_buf, (u_int32_t)packet_length);
  ssh_queue_packet_buf[4] = (u_int8_t)padding_len;
  if (payload_len > 0)
    memcpy(ssh_queue_packet_buf + 5, payload, (size_t)payload_len);
  ssh_runtime_random_fill(ssh_queue_packet_buf + 5 + payload_len,
                          padding_len);

  if (conn->tx_encrypted) {
    ssh_write_u32_be(seqbuf, conn->tx_seq);
    if (plain_len > SSH_PACKET_PLAIN_MAX)
      return -1;
    memcpy(ssh_queue_hmac_input, seqbuf, 4);
    memcpy(ssh_queue_hmac_input + 4, ssh_queue_packet_buf, (size_t)plain_len);
    ssh_crypto_hmac_sha256(mac, conn->tx_mac_key, sizeof(conn->tx_mac_key),
                           ssh_queue_hmac_input, (size_t)(plain_len + 4));
    cipher_copy = conn->tx_cipher;
    ssh_crypto_aes128_ctr_xcrypt(&cipher_copy, ssh_queue_packet_buf,
                                 (size_t)plain_len);
    conn->tx_cipher = cipher_copy;
    memcpy(ssh_queue_packet_buf + plain_len, mac, sizeof(mac));
  }

  conn->tx_seq++;
  return ssh_outbox_push(conn, ssh_queue_packet_buf,
                         total_len, activate_tx_crypto);
}

PRIVATE void ssh_queue_close(struct ssh_connection *conn, const char *reason)
{
  if (!conn->closing) {
    conn->closing = TRUE;
    conn->close_initiated = FALSE;
    conn->close_started_tick = kernel_tick;
    ssh_audit_close(conn->peer_addr, reason);
  }
}

PRIVATE void ssh_release_connection(struct ssh_connection *conn)
{
  if (conn == 0 || !conn->in_use)
    return;

  if (conn->channel.shell_pid > 0) {
    sys_kill(conn->channel.shell_pid, SIGKILL);
    ssh_cleanup_pid = conn->channel.shell_pid;
    ssh_cleanup_tty = conn->channel.tty;
    conn->channel.shell_pid = -1;
    conn->channel.tty = 0;
  } else if (conn->channel.tty != 0) {
    tty_release(conn->channel.tty);
    conn->channel.tty = 0;
  }

  if (conn->fd >= 0)
    kern_close_socket(conn->fd);
  ssh_close_signer_socket();
  ssh_reset_connection(conn);
}

PRIVATE void ssh_poll_cleanup(void)
{
  if (ssh_cleanup_pid <= 0)
    return;
  if (process_has_pid(ssh_cleanup_pid))
    return;

  if (ssh_cleanup_tty != 0)
    tty_release(ssh_cleanup_tty);
  ssh_cleanup_tty = 0;
  ssh_cleanup_pid = -1;
}

PRIVATE int ssh_build_kexinit_payload(struct ssh_connection *conn)
{
  struct ssh_writer writer;
  u_int8_t cookie[16];

  server_audit_line("ssh_kexinit_build_enter");
  ssh_writer_init(&writer, conn->server_kexinit, sizeof(conn->server_kexinit));
  ssh_writer_put_byte(&writer, SSH_MSG_KEXINIT);
  ssh_runtime_random_fill(cookie, sizeof(cookie));
  server_audit_line("ssh_kexinit_cookie_done");
  ssh_writer_put_data(&writer, cookie, sizeof(cookie));
  ssh_writer_put_cstring(&writer, "curve25519-sha256,curve25519-sha256@libssh.org");
  ssh_writer_put_cstring(&writer, "ssh-ed25519");
  ssh_writer_put_cstring(&writer, "aes128-ctr");
  ssh_writer_put_cstring(&writer, "aes128-ctr");
  ssh_writer_put_cstring(&writer, "hmac-sha2-256");
  ssh_writer_put_cstring(&writer, "hmac-sha2-256");
  ssh_writer_put_cstring(&writer, "none");
  ssh_writer_put_cstring(&writer, "none");
  ssh_writer_put_cstring(&writer, "");
  ssh_writer_put_cstring(&writer, "");
  ssh_writer_put_bool(&writer, FALSE);
  ssh_writer_put_u32(&writer, 0);
  if (writer.error)
    return -1;
  conn->server_kexinit_len = writer.len;
  server_audit_line("ssh_kexinit_build_done");
  return 0;
}

PRIVATE int ssh_queue_kexinit(struct ssh_connection *conn)
{
  if (conn->kexinit_sent)
    return 0;
  server_audit_line("ssh_kexinit_queue_enter");
  if (ssh_build_kexinit_payload(conn) < 0)
    return -1;
  if (ssh_queue_payload(conn, conn->server_kexinit,
                        conn->server_kexinit_len, FALSE) < 0) {
    return -1;
  }
  server_audit_line("ssh_kexinit_queue_done");
  ssh_audit_state("ssh_kexinit_out_count", conn->out_count);
  conn->kexinit_sent = TRUE;
  ssh_audit_state("ssh_kexinit_len", conn->server_kexinit_len);
  return 0;
}

PRIVATE int ssh_parse_client_kexinit(struct ssh_connection *conn,
                                     const u_int8_t *payload, int payload_len)
{
  struct ssh_reader reader;
  const u_int8_t *list = 0;
  int list_len = 0;
  int flag;
  int i;

  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  if (payload_len > (int)sizeof(conn->client_kexinit))
    return -1;
  memcpy(conn->client_kexinit, payload, (size_t)payload_len);
  conn->client_kexinit_len = payload_len;
  reader.pos += 16;

  for (i = 0; i < 10; i++) {
    ssh_reader_get_string(&reader, &list, &list_len);
    if (reader.error)
      return -1;
    if (i == 0) {
      if (!(ssh_namelist_has(list, list_len, "curve25519-sha256") ||
            ssh_namelist_has(list, list_len, "curve25519-sha256@libssh.org"))) {
        return -1;
      }
    } else if (i == 1) {
      if (!ssh_namelist_has(list, list_len, "ssh-ed25519"))
        return -1;
    } else if (i == 2 || i == 3) {
      if (!ssh_namelist_has(list, list_len, "aes128-ctr"))
        return -1;
    } else if (i == 4 || i == 5) {
      if (!ssh_namelist_has(list, list_len, "hmac-sha2-256"))
        return -1;
    } else if (i == 6 || i == 7) {
      if (!ssh_namelist_has(list, list_len, "none"))
        return -1;
    }
  }

  flag = ssh_reader_get_bool(&reader);
  ssh_reader_get_u32(&reader);
  if (reader.error || flag)
    return -1;

  conn->kexinit_received = TRUE;
  return 0;
}

PRIVATE int ssh_build_hostkey_blob(u_int8_t *buf, int cap)
{
  struct ssh_writer writer;

  ssh_writer_init(&writer, buf, cap);
  ssh_writer_put_cstring(&writer, "ssh-ed25519");
  ssh_writer_put_string(&writer, ssh_hostkey_public, sizeof(ssh_hostkey_public));
  if (writer.error)
    return -1;
  return writer.len;
}

PRIVATE int ssh_signer_recv_exact(int fd, u_int8_t *buf, int len)
{
  int read_len;

#ifdef USERLAND_SSHD_BUILD
  struct pollfd pfd;
  int wait_result;

  pfd.fd = fd;
  pfd.events = POLLIN | POLLHUP;
  pfd.revents = 0;
  wait_result = poll(&pfd, 1, SSH_SIGNER_RECV_RETRIES);
  if (wait_result <= 0 || (pfd.revents & POLLIN) == 0) {
    ssh_audit_state("ssh_signer_wait", wait_result);
    ssh_audit_state("ssh_signer_wait_revents", pfd.revents);
    return -1;
  }
  read_len = recvfrom_nowait(fd, buf, len, 0, 0);
  if (read_len < 0) {
    ssh_audit_state("ssh_signer_err", read_len);
    return -1;
  }
  ssh_audit_state("ssh_signer_read_len", read_len);
  return read_len == len ? 0 : -1;
#else
  int retry = 0;

  while (retry < SSH_SIGNER_RECV_RETRIES) {
    read_len = kern_recv(fd, buf, len, 0);

    if (read_len < 0)
      return -1;
    if (read_len > 0)
      ssh_audit_state("ssh_signer_read_len", read_len);
    if (read_len == 0) {
      retry++;
      continue;
    }
    return read_len == len ? 0 : -1;
  }

  return -1;
#endif
}

PRIVATE void ssh_close_signer_socket(void)
{
  if (ssh_signer_fd < 0)
    return;
  kern_close_socket(ssh_signer_fd);
  ssh_signer_fd = -1;
}

PRIVATE int ssh_get_signer_socket(void)
{
  if (server_runtime_ssh_signer_port() <= 0) {
    ssh_close_signer_socket();
    return -1;
  }
  if (ssh_signer_fd >= 0)
    return ssh_signer_fd;

  ssh_signer_fd = kern_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  return ssh_signer_fd;
}

PRIVATE int ssh_request_host_signature(
    u_int8_t signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES],
    const u_int8_t hash[SSH_CRYPTO_SHA256_BYTES])
{
  u_int8_t request[SSH_SIGNER_REQUEST_BYTES];
  u_int8_t response[SSH_SIGNER_RESPONSE_BYTES];
#ifdef USERLAND_SSHD_BUILD
  memcpy(request, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES, hash, SSH_CRYPTO_SHA256_BYTES);
  if (ssh_signer_roundtrip(server_runtime_ssh_signer_port(),
                           request, sizeof(request),
                           response, sizeof(response)) < 0)
    return -1;
  if (memcmp(response, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES) != 0)
    return -1;

  memcpy(signature,
         response + SSH_SIGNER_MAGIC_BYTES,
         SSH_CRYPTO_ED25519_SIGNATURE_BYTES);
  return 0;
#else
  struct sockaddr_in signer_addr;
  int fd;
  int result = -1;

  fd = ssh_get_signer_socket();
  if (fd < 0)
    return -1;

  network_fill_gateway_addr(&signer_addr,
                            (u_int16_t)server_runtime_ssh_signer_port());
  memcpy(request, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES, hash, SSH_CRYPTO_SHA256_BYTES);
  if (kern_sendto(fd, request, sizeof(request), 0, &signer_addr) !=
      (int)sizeof(request)) {
    goto done;
  }
  if (ssh_signer_recv_exact(fd, response, sizeof(response)) < 0)
    goto done;
  if (memcmp(response, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES) != 0)
    goto done;

  memcpy(signature,
         response + SSH_SIGNER_MAGIC_BYTES,
         SSH_CRYPTO_ED25519_SIGNATURE_BYTES);
  result = 0;

done:
  if (result < 0)
    ssh_close_signer_socket();
  return result;
#endif
}

PRIVATE int ssh_request_host_curve25519(
    u_int8_t server_public[SSH_CRYPTO_CURVE25519_BYTES],
    u_int8_t shared_secret[SSH_CRYPTO_CURVE25519_BYTES],
    const u_int8_t server_secret[SSH_CRYPTO_CURVE25519_BYTES],
    const u_int8_t client_public[SSH_CRYPTO_CURVE25519_BYTES])
{
  u_int8_t request[SSH_CURVE25519_REQUEST_BYTES];
  u_int8_t response[SSH_CURVE25519_RESPONSE_BYTES];
#ifdef USERLAND_SSHD_BUILD
  memcpy(request, SSH_CURVE25519_MAGIC, SSH_SIGNER_MAGIC_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES,
         server_secret,
         SSH_CRYPTO_CURVE25519_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES,
         client_public,
         SSH_CRYPTO_CURVE25519_BYTES);
  if (ssh_signer_roundtrip(server_runtime_ssh_signer_port(),
                           request, sizeof(request),
                           response, sizeof(response)) < 0)
    return -1;
  if (memcmp(response, SSH_CURVE25519_MAGIC, SSH_SIGNER_MAGIC_BYTES) != 0)
    return -1;

  memcpy(server_public,
         response + SSH_SIGNER_MAGIC_BYTES,
         SSH_CRYPTO_CURVE25519_BYTES);
  memcpy(shared_secret,
         response + SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES,
         SSH_CRYPTO_CURVE25519_BYTES);
  return 0;
#else
  struct sockaddr_in signer_addr;
  int fd;
  int result = -1;

  fd = ssh_get_signer_socket();
  if (fd < 0)
    return -1;

  network_fill_gateway_addr(&signer_addr,
                            (u_int16_t)server_runtime_ssh_signer_port());
  memcpy(request, SSH_CURVE25519_MAGIC, SSH_SIGNER_MAGIC_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES,
         server_secret,
         SSH_CRYPTO_CURVE25519_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES,
         client_public,
         SSH_CRYPTO_CURVE25519_BYTES);
  if (kern_sendto(fd, request, sizeof(request), 0, &signer_addr) !=
      (int)sizeof(request)) {
    goto done;
  }
  if (ssh_signer_recv_exact(fd, response, sizeof(response)) < 0)
    goto done;
  if (memcmp(response, SSH_CURVE25519_MAGIC, SSH_SIGNER_MAGIC_BYTES) != 0)
    goto done;

  memcpy(server_public,
         response + SSH_SIGNER_MAGIC_BYTES,
         SSH_CRYPTO_CURVE25519_BYTES);
  memcpy(shared_secret,
         response + SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES,
         SSH_CRYPTO_CURVE25519_BYTES);
  result = 0;

done:
  if (result < 0)
    ssh_close_signer_socket();
  return result;
#endif
}

PRIVATE void ssh_audit_sign_mode(const char *mode, int signer_port)
{
  char message[SSH_AUDIT_LINE_SIZE];
  char portbuf[16];
  int len = 0;

  message[0] = '\0';
  ssh_copy_string(message, sizeof(message), "ssh_kex_sign mode=");
  ssh_copy_string(message + strlen(message),
                  (int)sizeof(message) - (int)strlen(message),
                  mode);
  if (signer_port > 0) {
    char digits[16];
    int value = signer_port;

    if (value == 0) {
      portbuf[len++] = '0';
    } else {
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
    ssh_copy_string(message + strlen(message),
                    (int)sizeof(message) - (int)strlen(message),
                    " port=");
    ssh_copy_string(message + strlen(message),
                    (int)sizeof(message) - (int)strlen(message),
                    portbuf);
  }
  server_audit_line(message);
}

PRIVATE const char *ssh_protocol_close_reason(const u_int8_t *payload,
                                              int payload_len)
{
  if (payload == 0 || payload_len <= 0)
    return "protocol_error";

  switch (payload[0]) {
  case SSH_MSG_KEXINIT:
    return "kexinit_invalid";
  case SSH_MSG_KEX_ECDH_INIT:
    return "kex_failed";
  case SSH_MSG_NEWKEYS:
    return "newkeys_invalid";
  default:
    return "protocol_error";
  }
}

PRIVATE void ssh_derive_key_material(struct ssh_connection *conn,
                                     const u_int8_t *shared_secret,
                                     int shared_len)
{
  struct ssh_key_material_workspace *ws = &ssh_key_material_ws;
  int input_len;

  ssh_copy_string(conn->username, sizeof(conn->username), "root");

  input_len = 0;
  {
    struct ssh_writer writer;

    ssh_writer_init(&writer, ws->input, sizeof(ws->input));
    ssh_writer_put_mpint(&writer, shared_secret, shared_len);
    ssh_writer_put_data(&writer, conn->exchange_hash, sizeof(conn->exchange_hash));
    ssh_writer_put_byte(&writer, 'A');
    ssh_writer_put_data(&writer, conn->session_id, sizeof(conn->session_id));
    input_len = writer.len;
  }
  ssh_crypto_sha256(ws->rx_iv, ws->input, (size_t)input_len);

  {
    struct ssh_writer writer;

    ssh_writer_init(&writer, ws->input, sizeof(ws->input));
    ssh_writer_put_mpint(&writer, shared_secret, shared_len);
    ssh_writer_put_data(&writer, conn->exchange_hash, sizeof(conn->exchange_hash));
    ssh_writer_put_byte(&writer, 'B');
    ssh_writer_put_data(&writer, conn->session_id, sizeof(conn->session_id));
    input_len = writer.len;
  }
  ssh_crypto_sha256(ws->tx_iv, ws->input, (size_t)input_len);

  {
    struct ssh_writer writer;

    ssh_writer_init(&writer, ws->input, sizeof(ws->input));
    ssh_writer_put_mpint(&writer, shared_secret, shared_len);
    ssh_writer_put_data(&writer, conn->exchange_hash, sizeof(conn->exchange_hash));
    ssh_writer_put_byte(&writer, 'C');
    ssh_writer_put_data(&writer, conn->session_id, sizeof(conn->session_id));
    input_len = writer.len;
  }
  ssh_crypto_sha256(ws->rx_key, ws->input, (size_t)input_len);

  {
    struct ssh_writer writer;

    ssh_writer_init(&writer, ws->input, sizeof(ws->input));
    ssh_writer_put_mpint(&writer, shared_secret, shared_len);
    ssh_writer_put_data(&writer, conn->exchange_hash, sizeof(conn->exchange_hash));
    ssh_writer_put_byte(&writer, 'D');
    ssh_writer_put_data(&writer, conn->session_id, sizeof(conn->session_id));
    input_len = writer.len;
  }
  ssh_crypto_sha256(ws->tx_key, ws->input, (size_t)input_len);

  ssh_crypto_aes128_ctr_init(&conn->rx_cipher, ws->rx_key, ws->rx_iv);
  ssh_crypto_aes128_ctr_init(&conn->tx_cipher, ws->tx_key, ws->tx_iv);

  {
    struct ssh_writer writer;

    ssh_writer_init(&writer, ws->input, sizeof(ws->input));
    ssh_writer_put_mpint(&writer, shared_secret, shared_len);
    ssh_writer_put_data(&writer, conn->exchange_hash, sizeof(conn->exchange_hash));
    ssh_writer_put_byte(&writer, 'E');
    ssh_writer_put_data(&writer, conn->session_id, sizeof(conn->session_id));
    input_len = writer.len;
  }
  ssh_crypto_sha256(conn->rx_mac_key, ws->input, (size_t)input_len);

  {
    struct ssh_writer writer;

    ssh_writer_init(&writer, ws->input, sizeof(ws->input));
    ssh_writer_put_mpint(&writer, shared_secret, shared_len);
    ssh_writer_put_data(&writer, conn->exchange_hash, sizeof(conn->exchange_hash));
    ssh_writer_put_byte(&writer, 'F');
    ssh_writer_put_data(&writer, conn->session_id, sizeof(conn->session_id));
    input_len = writer.len;
  }
  ssh_crypto_sha256(conn->tx_mac_key, ws->input, (size_t)input_len);
}

PRIVATE int ssh_handle_kex_init(struct ssh_connection *conn,
                                const u_int8_t *payload, int payload_len)
{
  const u_int8_t *client_public;
  int client_public_len;
  int hostkey_len;
  int signature_len;
  int hash_input_len;
  int use_remote_signer;
  struct ssh_reader reader;
  struct ssh_writer writer;

  use_remote_signer = (server_runtime_ssh_signer_port() > 0) ? TRUE : FALSE;

  ssh_audit_sign_mode("kex_start", server_runtime_ssh_signer_port());
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &client_public, &client_public_len);
  if (reader.error || client_public_len != SSH_CRYPTO_CURVE25519_BYTES)
    return -1;
  server_audit_line("ssh_kex_client_public_ok");

  ssh_crypto_random_seed(ssh_runtime_rng_seed);
  ssh_runtime_random_reset();
  ssh_runtime_random_fill(ssh_kex_server_secret,
                          sizeof(ssh_kex_server_secret));
  if (use_remote_signer) {
    if (ssh_request_host_curve25519(ssh_kex_server_public, ssh_kex_shared_secret,
                                    ssh_kex_server_secret, client_public) < 0) {
      server_audit_line("ssh_kex_curve_remote_failed");
      return -1;
    }
  } else {
    if (ssh_crypto_curve25519_public_key(ssh_kex_server_public,
                                         ssh_kex_server_secret) < 0)
      return -1;
    if (ssh_crypto_curve25519_shared(ssh_kex_shared_secret,
                                     ssh_kex_server_secret,
                                     client_public) < 0) {
      server_audit_line("ssh_kex_curve_local_failed");
      return -1;
    }
  }
  server_audit_line("ssh_kex_curve_done");

  hostkey_len = ssh_build_hostkey_blob(ssh_kex_hostkey_blob,
                                       sizeof(ssh_kex_hostkey_blob));
  if (hostkey_len < 0)
    return -1;
  server_audit_line("ssh_kex_hostkey_blob_done");

  ssh_writer_init(&writer, ssh_kex_hash_input, sizeof(ssh_kex_hash_input));
  ssh_writer_put_string(&writer,
                        (const u_int8_t *)conn->banner_buf,
                        (int)strlen(conn->banner_buf));
  ssh_writer_put_string(&writer,
                        (const u_int8_t *)(SSH_BANNER),
                        (int)strlen(SSH_BANNER) - 2);
  ssh_writer_put_string(&writer, conn->client_kexinit, conn->client_kexinit_len);
  ssh_writer_put_string(&writer, conn->server_kexinit, conn->server_kexinit_len);
  ssh_writer_put_string(&writer, ssh_kex_hostkey_blob, hostkey_len);
  ssh_writer_put_string(&writer, client_public, client_public_len);
  ssh_writer_put_string(&writer,
                        ssh_kex_server_public,
                        sizeof(ssh_kex_server_public));
  ssh_writer_put_mpint(&writer,
                       ssh_kex_shared_secret,
                       sizeof(ssh_kex_shared_secret));
  if (writer.error)
    return -1;
  hash_input_len = writer.len;

  ssh_crypto_sha256(conn->exchange_hash,
                    ssh_kex_hash_input,
                    (size_t)hash_input_len);
  server_audit_line("ssh_kex_hash_done");
  if (!conn->session_id_set) {
    memcpy(conn->session_id, conn->exchange_hash, sizeof(conn->session_id));
    conn->session_id_set = TRUE;
  }
  if (use_remote_signer) {
    ssh_audit_sign_mode("remote", server_runtime_ssh_signer_port());
    if (ssh_request_host_signature(ssh_kex_signature, conn->exchange_hash) < 0) {
      ssh_audit_sign_mode("remote_fail", server_runtime_ssh_signer_port());
      return -1;
    }
  } else {
    ssh_audit_sign_mode("local", 0);
    if (ssh_crypto_ed25519_sign(ssh_kex_signature, ssh_hostkey_secret,
                                conn->exchange_hash,
                                sizeof(conn->exchange_hash)) < 0) {
      ssh_audit_sign_mode("local_fail", 0);
      return -1;
    }
  }
  ssh_audit_sign_mode("sign_done", server_runtime_ssh_signer_port());

  ssh_writer_init(&writer, ssh_kex_signature_blob, sizeof(ssh_kex_signature_blob));
  ssh_writer_put_cstring(&writer, "ssh-ed25519");
  ssh_writer_put_string(&writer,
                        ssh_kex_signature,
                        sizeof(ssh_kex_signature));
  if (writer.error)
    return -1;
  signature_len = writer.len;
  ssh_audit_sign_mode("sig_blob_done", server_runtime_ssh_signer_port());

  ssh_derive_key_material(conn,
                          ssh_kex_shared_secret,
                          sizeof(ssh_kex_shared_secret));
  ssh_audit_sign_mode("derive_done", server_runtime_ssh_signer_port());

  ssh_writer_init(&writer, ssh_kex_reply, sizeof(ssh_kex_reply));
  ssh_writer_put_byte(&writer, SSH_MSG_KEX_ECDH_REPLY);
  ssh_writer_put_string(&writer, ssh_kex_hostkey_blob, hostkey_len);
  ssh_writer_put_string(&writer,
                        ssh_kex_server_public,
                        sizeof(ssh_kex_server_public));
  ssh_writer_put_string(&writer, ssh_kex_signature_blob, signature_len);
  if (writer.error)
    return -1;
  if (ssh_queue_payload(conn, ssh_kex_reply, writer.len, FALSE) < 0)
    return -1;
  ssh_audit_state("ssh_reply_payload_len", writer.len);
  ssh_audit_sign_mode("reply_done", server_runtime_ssh_signer_port());

  ssh_kex_reply[0] = SSH_MSG_NEWKEYS;
  if (ssh_queue_payload(conn, ssh_kex_reply, 1, TRUE) < 0)
    return -1;
  ssh_audit_state("ssh_newkeys_payload_len", 1);
  ssh_audit_sign_mode("newkeys_done", server_runtime_ssh_signer_port());
  conn->newkeys_sent = TRUE;
  server_audit_line("ssh_kex_return");
  return 0;
}

PRIVATE int ssh_queue_service_accept(struct ssh_connection *conn,
                                     const char *service_name)
{
  u_int8_t payload[64];
  struct ssh_writer writer;

  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_SERVICE_ACCEPT);
  ssh_writer_put_cstring(&writer, service_name);
  if (writer.error)
    return -1;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_queue_userauth_success(struct ssh_connection *conn)
{
  u_int8_t payload[1];

  payload[0] = SSH_MSG_USERAUTH_SUCCESS;
  return ssh_queue_payload(conn, payload, sizeof(payload), FALSE);
}

PRIVATE int ssh_queue_userauth_failure(struct ssh_connection *conn)
{
  u_int8_t payload[64];
  struct ssh_writer writer;

  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_USERAUTH_FAILURE);
  ssh_writer_put_cstring(&writer, "password");
  ssh_writer_put_bool(&writer, FALSE);
  if (writer.error)
    return -1;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE void ssh_load_auth_identity(const struct ssh_connection *conn,
                                    struct ssh_auth_identity *identity)
{
  if (identity == 0)
    return;
  ssh_auth_identity_reset(identity);
  if (conn == 0 || !conn->auth_identity_set)
    return;
  identity->set = TRUE;
  ssh_copy_string(identity->username, sizeof(identity->username), conn->username);
  ssh_copy_string(identity->service, sizeof(identity->service), conn->auth_service);
}

PRIVATE void ssh_store_auth_identity(struct ssh_connection *conn,
                                     const struct ssh_auth_identity *identity)
{
  if (conn == 0 || identity == 0 || !identity->set)
    return;
  conn->auth_identity_set = TRUE;
  ssh_copy_string(conn->username, sizeof(conn->username), identity->username);
  ssh_copy_string(conn->auth_service, sizeof(conn->auth_service), identity->service);
}

PRIVATE int ssh_auth_delay_pending(struct ssh_connection *conn)
{
  if (conn == 0)
    return FALSE;
  return ssh_auth_failure_delay_pending(kernel_tick, conn->auth_delay_until_tick);
}

PRIVATE int ssh_flush_auth_pending(struct ssh_connection *conn)
{
  if (conn == 0 || conn->auth_delay_until_tick == 0)
    return 0;
  if (ssh_auth_delay_pending(conn))
    return 0;

  conn->auth_delay_until_tick = 0;
  if (conn->auth_pending_close) {
    conn->auth_pending_close = FALSE;
    ssh_audit_peer("ssh_auth_retry_limit", conn->peer_addr);
    ssh_queue_close(conn, "auth_retry_limit");
    return 1;
  }
  if (conn->auth_pending_failure) {
    conn->auth_pending_failure = FALSE;
    return ssh_queue_userauth_failure(conn);
  }
  return 0;
}

PRIVATE int ssh_handle_auth_failure(struct ssh_connection *conn)
{
  conn->auth_failures++;
  ssh_audit_peer("ssh_auth_failure", conn->peer_addr);
  ssh_audit_state("ssh_auth_fail_count", conn->auth_failures);
  conn->auth_delay_until_tick = ssh_auth_failure_delay_until(kernel_tick);
  conn->auth_pending_close =
      ssh_auth_failure_should_close(conn->auth_failures) ? TRUE : FALSE;
  conn->auth_pending_failure = conn->auth_pending_close ? FALSE : TRUE;
  ssh_audit_state("ssh_auth_fail_delay_until", (int)conn->auth_delay_until_tick);
  return 0;
}

PRIVATE int ssh_queue_channel_open_confirmation(struct ssh_connection *conn)
{
  u_int8_t payload[32];
  struct ssh_writer writer;

  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_OPEN_CONFIRMATION);
  ssh_writer_put_u32(&writer, conn->channel.peer_id);
  ssh_writer_put_u32(&writer, conn->channel.local_id);
  ssh_writer_put_u32(&writer, SSH_CHANNEL_WINDOW);
  ssh_writer_put_u32(&writer, SSH_CHANNEL_MAX_PACKET);
  if (writer.error)
    return -1;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_queue_channel_open_failure(struct ssh_connection *conn,
                                           u_int32_t recipient,
                                           const char *reason)
{
  u_int8_t payload[128];
  struct ssh_writer writer;

  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_OPEN_FAILURE);
  ssh_writer_put_u32(&writer, recipient);
  ssh_writer_put_u32(&writer, 1);
  ssh_writer_put_cstring(&writer, reason);
  ssh_writer_put_cstring(&writer, "");
  if (writer.error)
    return -1;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_queue_channel_status(struct ssh_connection *conn, int success)
{
  u_int8_t payload[8];
  struct ssh_writer writer;

  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, success ? SSH_MSG_CHANNEL_SUCCESS
                                       : SSH_MSG_CHANNEL_FAILURE);
  ssh_writer_put_u32(&writer, conn->channel.peer_id);
  if (writer.error)
    return -1;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_start_pending_shell(struct ssh_connection *conn)
{
  struct tty *tty;
  char *shell_argv[2];
  pid_t pid;

  if (!conn->channel.shell_start_pending ||
      conn->channel.shell_start_in_progress) {
    return 0;
  }
  if ((int)(kernel_tick - conn->channel.shell_start_tick) < 0)
    return 0;
  if (conn->channel.tty != 0 && conn->channel.shell_pid > 0) {
    conn->channel.shell_start_pending = FALSE;
    conn->channel.shell_start_tick = 0;
    if (conn->channel.shell_reply_pending) {
      conn->channel.shell_reply_pending = FALSE;
      return ssh_queue_channel_status(conn, TRUE);
    }
    return 1;
  }

  server_audit_line("ssh_spawn_begin");
  tty = tty_alloc_pty();
  if (tty == 0) {
    conn->channel.shell_start_pending = FALSE;
    conn->channel.shell_start_tick = 0;
    if (conn->channel.shell_reply_pending) {
      conn->channel.shell_reply_pending = FALSE;
      return ssh_queue_channel_status(conn, FALSE);
    }
    return -1;
  }

  conn->channel.shell_start_in_progress = TRUE;
  tty_set_winsize(tty, conn->channel.cols, conn->channel.rows);
  shell_argv[0] = "eshell";
  shell_argv[1] = 0;
  pid = kernel_execve_tty("/usr/bin/eshell", shell_argv, tty);
  if (pid < 0) {
    conn->channel.shell_start_pending = FALSE;
    conn->channel.shell_start_in_progress = FALSE;
    conn->channel.shell_start_tick = 0;
    tty_release(tty);
    if (conn->channel.shell_reply_pending) {
      conn->channel.shell_reply_pending = FALSE;
      return ssh_queue_channel_status(conn, FALSE);
    }
    return -1;
  }

  conn->channel.tty = tty;
  conn->channel.shell_pid = pid;
  conn->channel.shell_start_pending = FALSE;
  conn->channel.shell_start_in_progress = FALSE;
  conn->channel.shell_start_tick = 0;
  conn->channel.shell_started_tick = kernel_tick;
  conn->channel.prompt_kick_pending = TRUE;
  server_audit_line("ssh_spawn_ok");
  ssh_audit_start(conn->peer_addr, pid);
  if (conn->channel.shell_reply_pending) {
    conn->channel.shell_reply_pending = FALSE;
    if (ssh_queue_channel_status(conn, TRUE) < 0)
      return -1;
  }
  return 1;
}

PRIVATE int ssh_queue_request_failure(struct ssh_connection *conn)
{
  u_int8_t payload[1];

  payload[0] = SSH_MSG_REQUEST_FAILURE;
  return ssh_queue_payload(conn, payload, sizeof(payload), FALSE);
}

PRIVATE int ssh_queue_channel_data(struct ssh_connection *conn,
                                   const u_int8_t *data, int len)
{
  struct ssh_writer writer;

  ssh_writer_init(&writer, ssh_channel_payload_buf,
                  sizeof(ssh_channel_payload_buf));
  ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_DATA);
  ssh_writer_put_u32(&writer, conn->channel.peer_id);
  ssh_writer_put_string(&writer, data, len);
  if (writer.error)
    return -1;
  return ssh_queue_payload(conn, ssh_channel_payload_buf,
                           writer.len, FALSE);
}

PRIVATE int ssh_queue_channel_eof(struct ssh_connection *conn)
{
  u_int8_t payload[8];
  struct ssh_writer writer;

  if (conn->channel.eof_sent)
    return 0;
  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_EOF);
  ssh_writer_put_u32(&writer, conn->channel.peer_id);
  if (writer.error)
    return -1;
  conn->channel.eof_sent = TRUE;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_queue_channel_close(struct ssh_connection *conn)
{
  u_int8_t payload[8];
  struct ssh_writer writer;

  if (conn->channel.close_sent)
    return 0;
  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_CLOSE);
  ssh_writer_put_u32(&writer, conn->channel.peer_id);
  if (writer.error)
    return -1;
  conn->channel.close_sent = TRUE;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_queue_exit_status(struct ssh_connection *conn, u_int32_t status)
{
  u_int8_t payload[64];
  struct ssh_writer writer;

  if (conn->channel.exit_status_sent)
    return 0;
  ssh_writer_init(&writer, payload, sizeof(payload));
  ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_REQUEST);
  ssh_writer_put_u32(&writer, conn->channel.peer_id);
  ssh_writer_put_cstring(&writer, "exit-status");
  ssh_writer_put_bool(&writer, FALSE);
  ssh_writer_put_u32(&writer, status);
  if (writer.error)
    return -1;
  conn->channel.exit_status_sent = TRUE;
  return ssh_queue_payload(conn, payload, writer.len, FALSE);
}

PRIVATE int ssh_spawn_shell(struct ssh_connection *conn)
{
  if (conn->channel.tty != 0 && conn->channel.shell_pid > 0)
    return 0;
  if (conn->channel.shell_start_pending || conn->channel.shell_start_in_progress)
    return 0;

  conn->channel.shell_start_pending = TRUE;
  conn->channel.shell_start_tick = kernel_tick + 1;
  return 0;
}

PRIVATE int ssh_handle_service_request(struct ssh_connection *conn,
                                       const u_int8_t *payload, int payload_len)
{
  char service_name[SSH_AUTH_TEXT_MAX];

  if (ssh_auth_parse_service_request(payload, payload_len,
                                     service_name, sizeof(service_name)) < 0) {
    return -1;
  }

  if (strcmp(service_name, "ssh-userauth") == 0) {
    conn->userauth_service_ready = TRUE;
    return ssh_queue_service_accept(conn, "ssh-userauth");
  }
  if (conn->auth_done && strcmp(service_name, "ssh-connection") == 0) {
    return ssh_queue_service_accept(conn, "ssh-connection");
  }
  return -1;
}

PRIVATE int ssh_handle_userauth_request(struct ssh_connection *conn,
                                        const u_int8_t *payload, int payload_len)
{
  struct ssh_auth_request request;
  struct ssh_auth_identity identity;
  const char *expected_password = server_runtime_ssh_password();

  if (!conn->userauth_service_ready)
    return -1;
  if (conn->auth_pending_failure || conn->auth_pending_close ||
      ssh_auth_delay_pending(conn)) {
    return 0;
  }

  if (ssh_auth_parse_request(payload, payload_len, &request) < 0)
    return -1;
  ssh_load_auth_identity(conn, &identity);
  if (!identity.set) {
    if (ssh_auth_identity_capture(&identity, &request) < 0)
      return -1;
    ssh_store_auth_identity(conn, &identity);
  } else if (!ssh_auth_identity_matches(&identity, &request)) {
    ssh_queue_close(conn, "auth_identity_changed");
    return 0;
  }

  if (ssh_auth_password_request_matches(&request,
                                        "root",
                                        "ssh-connection",
                                        expected_password)) {
    conn->auth_done = TRUE;
    conn->auth_pending_failure = FALSE;
    conn->auth_pending_close = FALSE;
    conn->auth_delay_until_tick = 0;
    ssh_copy_string(conn->username, sizeof(conn->username), request.username);
    ssh_audit_peer("ssh_auth_success", conn->peer_addr);
    return ssh_queue_userauth_success(conn);
  }

  return ssh_handle_auth_failure(conn);
}

PRIVATE int ssh_handle_channel_open(struct ssh_connection *conn,
                                    const u_int8_t *payload, int payload_len)
{
  struct ssh_channel_open_request request;

  if (!conn->auth_done)
    return -1;
  if (ssh_channel_parse_open(payload, payload_len, &request) < 0)
    return -1;

  if (conn->channel.open || strcmp(request.type, "session") != 0) {
    return ssh_queue_channel_open_failure(conn, request.peer_id, "session_only");
  }

  conn->channel.open = TRUE;
  conn->channel.peer_id = request.peer_id;
  conn->channel.peer_window = request.peer_window;
  conn->channel.peer_max_packet = request.peer_max_packet;
  conn->channel.local_id = 0;
  conn->channel.cols = SSH_DEFAULT_COLS;
  conn->channel.rows = SSH_DEFAULT_ROWS;
  server_audit_line("ssh_channel_open_ok");
  return ssh_queue_channel_open_confirmation(conn);
}

PRIVATE int ssh_handle_channel_request(struct ssh_connection *conn,
                                       const u_int8_t *payload, int payload_len)
{
  struct ssh_channel_request request;

  if (ssh_channel_parse_request(payload, payload_len, &request) < 0 ||
      !conn->channel.open || request.recipient != conn->channel.local_id) {
    return -1;
  }

  if (request.kind == SSH_CHANNEL_REQUEST_PTY) {
    server_audit_line("ssh_pty_req");
    conn->channel.cols = request.cols;
    conn->channel.rows = request.rows;
    conn->channel.pty_requested = TRUE;
    if (conn->channel.tty != 0)
      tty_set_winsize(conn->channel.tty, conn->channel.cols, conn->channel.rows);
    return request.want_reply ? ssh_queue_channel_status(conn, TRUE) : 0;
  }

  if (request.kind == SSH_CHANNEL_REQUEST_WINDOW_CHANGE) {
    server_audit_line("ssh_window_change");
    conn->channel.cols = request.cols;
    conn->channel.rows = request.rows;
    if (conn->channel.tty != 0)
      tty_set_winsize(conn->channel.tty, conn->channel.cols, conn->channel.rows);
    return request.want_reply ? ssh_queue_channel_status(conn, TRUE) : 0;
  }

  if (request.kind == SSH_CHANNEL_REQUEST_SHELL) {
    server_audit_line("ssh_shell_req");
    if (ssh_spawn_shell(conn) < 0)
      return request.want_reply ? ssh_queue_channel_status(conn, FALSE) : -1;
    conn->channel.shell_reply_pending = request.want_reply ? TRUE : FALSE;
    return 0;
  }

  return request.want_reply ? ssh_queue_channel_status(conn, FALSE) : 0;
}

PRIVATE int ssh_handle_channel_data(struct ssh_connection *conn,
                                    const u_int8_t *payload, int payload_len)
{
  struct ssh_channel_data_request request;

  if (ssh_channel_parse_data(payload, payload_len, &request) < 0 ||
      !conn->channel.open || request.recipient != conn->channel.local_id) {
    return -1;
  }
  if (conn->channel.tty == 0)
    return 0;

  conn->last_activity_tick = kernel_tick;
  {
    int i;

    for (i = 0; i < request.data_len; i++) {
      if (request.data[i] == 0x03)
        ssh_interrupt_foreground(conn);
      tty_master_write(conn->channel.tty, request.data + i, 1);
    }
  }
  return 0;
}

PRIVATE int ssh_handle_payload(struct ssh_connection *conn,
                               const u_int8_t *payload, int payload_len)
{
  struct ssh_reader reader;
  u_int32_t recipient;

  if (payload_len <= 0)
    return -1;

  switch (payload[0]) {
  case SSH_MSG_DISCONNECT:
    ssh_queue_close(conn, "peer_disconnect");
    return 0;
  case SSH_MSG_IGNORE:
  case SSH_MSG_UNIMPLEMENTED:
    return 0;
  case SSH_MSG_KEXINIT:
    return ssh_parse_client_kexinit(conn, payload, payload_len);
  case SSH_MSG_KEX_ECDH_INIT: {
    int status;

    disableInterrupt();
    status = ssh_handle_kex_init(conn, payload, payload_len);
    enableInterrupt();
    return status;
  }
  case SSH_MSG_NEWKEYS:
    server_audit_line("ssh_newkeys_rx");
    conn->newkeys_received = TRUE;
    conn->rx_encrypted = TRUE;
    return 0;
  case SSH_MSG_SERVICE_REQUEST:
    return ssh_handle_service_request(conn, payload, payload_len);
  case SSH_MSG_USERAUTH_REQUEST:
    return ssh_handle_userauth_request(conn, payload, payload_len);
  case SSH_MSG_GLOBAL_REQUEST:
    ssh_reader_init(&reader, payload + 1, payload_len - 1);
    ssh_reader_get_string(&reader, (const u_int8_t **)&payload, &payload_len);
    if (ssh_reader_get_bool(&reader))
      return ssh_queue_request_failure(conn);
    return 0;
  case SSH_MSG_CHANNEL_OPEN:
    return ssh_handle_channel_open(conn, payload, payload_len);
  case SSH_MSG_CHANNEL_WINDOW_ADJUST:
    ssh_reader_init(&reader, payload + 1, payload_len - 1);
    recipient = ssh_reader_get_u32(&reader);
    if (recipient != conn->channel.local_id)
      return -1;
    conn->channel.peer_window += ssh_reader_get_u32(&reader);
    return reader.error ? -1 : 0;
  case SSH_MSG_CHANNEL_DATA:
    return ssh_handle_channel_data(conn, payload, payload_len);
  case SSH_MSG_CHANNEL_EOF:
    return 0;
  case SSH_MSG_CHANNEL_CLOSE:
    conn->channel.peer_close = TRUE;
    if (ssh_queue_channel_close(conn) < 0)
      return -1;
    ssh_queue_close(conn, "peer_close");
    return 0;
  case SSH_MSG_CHANNEL_REQUEST:
    return ssh_handle_channel_request(conn, payload, payload_len);
  default:
    return -1;
  }
}

PRIVATE int ssh_try_decode_plain_packet(struct ssh_connection *conn,
                                        u_int8_t *payload, int *payload_len)
{
  return ssh_try_decode_plain_packet_buffer(conn->rx_buf, &conn->rx_len,
                                            &conn->rx_seq,
                                            SSH_PACKET_PLAIN_MAX,
                                            payload, payload_len);
}

PRIVATE int ssh_try_decode_encrypted_packet(struct ssh_connection *conn,
                                            u_int8_t *payload, int *payload_len)
{
  u_int8_t peek[16];
  u_int8_t mac[SSH_CRYPTO_SHA256_BYTES];
  u_int8_t seqbuf[4];
  struct ssh_aes_ctr_ctx cipher_copy;
  u_int32_t packet_length;
  int plain_len;
  int total_len;
  int padding_len;

  if (conn->rx_len < 16)
    return 0;

  cipher_copy = conn->rx_cipher;
  memcpy(peek, conn->rx_buf, sizeof(peek));
  ssh_crypto_aes128_ctr_xcrypt(&cipher_copy, peek, sizeof(peek));
  packet_length = ssh_read_u32_be(peek);
  plain_len = (int)packet_length + 4;
  total_len = plain_len + SSH_CRYPTO_SHA256_BYTES;
  if (packet_length < 5 || plain_len > SSH_PACKET_PLAIN_MAX)
    return -1;
  if (conn->rx_len < total_len)
    return 0;

  memcpy(ssh_decode_packet_buf, conn->rx_buf, (size_t)plain_len);
  cipher_copy = conn->rx_cipher;
  ssh_crypto_aes128_ctr_xcrypt(&cipher_copy, ssh_decode_packet_buf,
                               (size_t)plain_len);
  ssh_write_u32_be(seqbuf, conn->rx_seq);
  memcpy(ssh_queue_hmac_input, seqbuf, 4);
  memcpy(ssh_queue_hmac_input + 4, ssh_decode_packet_buf, (size_t)plain_len);
  ssh_crypto_hmac_sha256(mac, conn->rx_mac_key, sizeof(conn->rx_mac_key),
                         ssh_queue_hmac_input, (size_t)(plain_len + 4));
  if (memcmp(mac, conn->rx_buf + plain_len, sizeof(mac)) != 0)
    return -1;

  conn->rx_cipher = cipher_copy;
  padding_len = ssh_decode_packet_buf[4];
  if (padding_len < 4 || (int)packet_length - padding_len - 1 < 0)
    return -1;
  *payload_len = (int)packet_length - padding_len - 1;
  memcpy(payload, ssh_decode_packet_buf + 5, (size_t)*payload_len);
  ssh_move_bytes(conn->rx_buf, conn->rx_buf + total_len, conn->rx_len - total_len);
  conn->rx_len -= total_len;
  conn->rx_seq++;
  return 1;
}

PRIVATE int ssh_try_decode_packet(struct ssh_connection *conn,
                                  u_int8_t *payload, int *payload_len)
{
  if (conn->rx_encrypted)
    return ssh_try_decode_encrypted_packet(conn, payload, payload_len);
  return ssh_try_decode_plain_packet(conn, payload, payload_len);
}

PRIVATE int ssh_append_rx(struct ssh_connection *conn,
                          const u_int8_t *data, int len)
{
  if (conn->rx_len + len > (int)sizeof(conn->rx_buf))
    return -1;
  memcpy(conn->rx_buf + conn->rx_len, data, (size_t)len);
  conn->rx_len += len;
  return 0;
}

PRIVATE int ssh_pump_banner(struct ssh_connection *conn)
{
  u_int8_t chunk[256];
  int read_len;
  int i;

  disableInterrupt();
  read_len = rxbuf_read_direct(conn->fd, chunk, sizeof(chunk), 0);
  enableInterrupt();
  if (read_len <= 0)
    return 0;
  ssh_audit_state("ssh_banner_read_len", read_len);

  conn->last_activity_tick = kernel_tick;
  for (i = 0; i < read_len; i++) {
    if (conn->banner_received) {
      if (ssh_append_rx(conn, chunk + i, read_len - i) < 0)
        return -1;
      return 0;
    }

    if (conn->banner_len >= SSH_BANNER_MAX - 1)
      return -1;
    conn->banner_buf[conn->banner_len++] = (char)chunk[i];
    conn->banner_buf[conn->banner_len] = '\0';
    if (chunk[i] == '\n') {
      int len = conn->banner_len;

      while (len > 0 &&
             (conn->banner_buf[len - 1] == '\n' ||
              conn->banner_buf[len - 1] == '\r')) {
        len--;
      }
      conn->banner_buf[len] = '\0';
      if (strncmp(conn->banner_buf, "SSH-2.0-", 8) != 0)
        return -1;
      conn->banner_received = TRUE;
      server_audit_line("ssh_banner_rx_ok");
      if (i + 1 < read_len) {
        if (ssh_append_rx(conn, chunk + i + 1, read_len - i - 1) < 0)
          return -1;
      }
      if (ssh_queue_kexinit(conn) < 0)
        return -1;
      return 0;
    }
  }
  return 0;
}

PRIVATE int ssh_pump_rx(struct ssh_connection *conn)
{
  u_int8_t chunk[256];
  int read_len;

  if (!ssh_socket_rx_ready(conn->fd))
    return 0;
  disableInterrupt();
  read_len = rxbuf_read_direct(conn->fd, chunk, sizeof(chunk), 0);
  enableInterrupt();
  if (read_len <= 0)
    return 0;
  ssh_audit_state("ssh_rx_read_len", read_len);
  if (ssh_append_rx(conn, chunk, read_len) < 0)
    return -1;
  conn->last_activity_tick = kernel_tick;
  return 0;
}

PRIVATE void ssh_flush_outbox(struct ssh_connection *conn)
{
  struct ssh_out_packet *packet;

  if (conn->out_count <= 0)
    return;
  if (ssh_socket_tx_pending(conn->fd)) {
    ssh_audit_state("ssh_flush_blocked", socket_table[conn->fd].tx_pending);
    return;
  }

  packet = &conn->outbox[conn->out_tail];
  ssh_audit_state("ssh_flush_len", packet->len);
  if (kern_send(conn->fd, packet->data, packet->len, 0) < 0) {
    ssh_queue_close(conn, "send_failed");
    return;
  }
  if (packet->activate_tx_crypto)
    conn->tx_encrypted = TRUE;
  conn->out_tail = (conn->out_tail + 1) % SSH_OUTBOX_MAX;
  conn->out_count--;
}

PRIVATE void ssh_interrupt_foreground(struct ssh_connection *conn)
{
  pid_t pid;

  if (conn == 0 || conn->channel.tty == 0)
    return;

  pid = tty_get_foreground_pid(conn->channel.tty);
  if (pid <= 0)
    return;
  sys_kill(pid, SIGINT);
}

PRIVATE int ssh_translate_tty_chunk(struct ssh_connection *conn,
                                    const char *src, int src_len,
                                    char *dest, int dest_cap)
{
  int in_pos = 0;
  int out_pos = 0;

  if (conn == 0 || src == 0 || dest == 0 || src_len <= 0 || dest_cap <= 0)
    return 0;

  while (in_pos < src_len && out_pos < dest_cap) {
    char ch = src[in_pos++];

    if (ch == '\n' && !conn->channel.tx_prev_cr) {
      if (out_pos + 2 > dest_cap) {
        in_pos--;
        break;
      }
      dest[out_pos++] = '\r';
      dest[out_pos++] = '\n';
    } else {
      dest[out_pos++] = ch;
    }
    conn->channel.tx_prev_cr = (ch == '\r') ? TRUE : FALSE;
  }

  return out_pos;
}

PRIVATE void ssh_pump_tty_to_channel(struct ssh_connection *conn)
{
  char raw_chunk[128];
  char cooked_chunk[256];
  int raw_cap;
  int read_len;
  int send_len;
  u_int32_t cap;

  if (!conn->channel.open || conn->channel.tty == 0)
    return;
  if (conn->out_count >= SSH_OUTBOX_MAX)
    return;
  if (conn->channel.peer_window == 0)
    return;
  if (conn->channel.prompt_kick_pending) {
    if (ssh_socket_tx_pending(conn->fd))
      return;
    if (conn->out_count != 0)
      return;
    if ((int)(kernel_tick - conn->channel.shell_started_tick) < 2)
      return;
  }

  cap = conn->channel.peer_max_packet;
  if (cap > sizeof(cooked_chunk))
    cap = sizeof(cooked_chunk);
  if (cap > conn->channel.peer_window)
    cap = conn->channel.peer_window;
  if (cap == 0)
    return;

  raw_cap = (int)(cap / 2);
  if (raw_cap <= 0)
    raw_cap = 1;
  if (raw_cap > (int)sizeof(raw_chunk))
    raw_cap = sizeof(raw_chunk);

  read_len = (int)tty_master_read(conn->channel.tty, raw_chunk, raw_cap);
  if (read_len <= 0) {
    /* 初回 prompt が client 入力待ちで見えない run を避ける。 */
    if (conn->channel.prompt_kick_pending &&
        (int)(kernel_tick - conn->channel.shell_started_tick) >= 1) {
      tty_master_write(conn->channel.tty, "\n", 1);
      conn->channel.prompt_kick_pending = FALSE;
    }
    return;
  }

  conn->channel.prompt_kick_pending = FALSE;
  send_len = ssh_translate_tty_chunk(conn, raw_chunk, read_len,
                                     cooked_chunk, (int)cap);
  if (send_len <= 0)
    return;
  if (ssh_queue_channel_data(conn, (const u_int8_t *)cooked_chunk, send_len) < 0) {
    ssh_queue_close(conn, "queue_failed");
    return;
  }
  conn->channel.peer_window -= (u_int32_t)send_len;
  conn->last_activity_tick = kernel_tick;
}

PRIVATE void ssh_poll_shell(struct ssh_connection *conn)
{
  struct ssh_channel_close_plan plan;

  if (conn->channel.shell_pid <= 0)
    return;
  if (process_has_pid(conn->channel.shell_pid))
    return;

  if (conn->channel.tty != 0) {
    tty_release(conn->channel.tty);
    conn->channel.tty = 0;
  }
  conn->channel.shell_pid = -1;
  conn->channel.prompt_kick_pending = FALSE;
  ssh_channel_plan_shutdown(conn->channel.exit_status_sent,
                            conn->channel.eof_sent,
                            conn->channel.close_sent,
                            &plan);
  if (plan.send_exit_status)
    ssh_queue_exit_status(conn, 0);
  if (plan.send_eof)
    ssh_queue_channel_eof(conn);
  if (plan.send_close)
    ssh_queue_channel_close(conn);
  ssh_queue_close(conn, "shell_exit");
}

PRIVATE void ssh_accept_pending_connections(void)
{
  struct sockaddr_in peer;
  int child_fd;
  int auth_result;

  if (ssh_conn.in_use)
    return;

  disableInterrupt();
  child_fd = socket_try_accept(ssh_listener_fd, &peer);
  enableInterrupt();
  if (child_fd < 0)
    return;
  ssh_audit_state("ssh_accept_fd_raw", child_fd);

  auth_result = admin_authorize_peer(peer.sin_addr, 0);
  ssh_audit_state("ssh_accept_auth", auth_result);
  if (auth_result != ADMIN_AUTH_ALLOW) {
    ssh_audit_peer("ssh_deny", peer.sin_addr);
    kern_close_socket(child_fd);
    return;
  }

  ssh_reset_connection(&ssh_conn);
  server_audit_line("ssh_accept_reset_done");
  ssh_conn.in_use = TRUE;
  ssh_conn.fd = child_fd;
  ssh_conn.peer_addr = peer.sin_addr;
  ssh_conn.accepted_tick = kernel_tick;
  ssh_conn.last_activity_tick = kernel_tick;
  ssh_crypto_random_seed(ssh_runtime_rng_seed);
  ssh_runtime_random_reset();
  server_audit_line("ssh_accept_seed_done");
  ssh_audit_peer("accept_ssh", peer.sin_addr);
  ssh_audit_state("ssh_accept_fd", child_fd);
}

PRIVATE void ssh_poll_connection(struct ssh_connection *conn)
{
  int payload_len;
  int result;
  int start_result = 0;
  int timeout_phase;
  u_int32_t timeout_ticks;
  int iterations = 0;

  if (!conn->in_use)
    return;

  if (socket_table[conn->fd].state == SOCK_STATE_CLOSED) {
    ssh_release_connection(conn);
    return;
  }

  timeout_phase = ssh_timeout_phase(conn->auth_done,
                                    conn->channel.open,
                                    conn->channel.shell_pid > 0 ||
                                    conn->channel.tty != 0);
  timeout_ticks = ssh_timeout_ticks_for_phase(timeout_phase);
  if (!conn->closing &&
      (int)(kernel_tick - conn->last_activity_tick) > (int)timeout_ticks) {
    ssh_audit_state(ssh_timeout_audit_label_for_phase(timeout_phase),
                    (int)(kernel_tick - conn->last_activity_tick));
    ssh_queue_close(conn, ssh_timeout_close_reason_for_phase(timeout_phase));
  }

  if (ssh_send_banner(conn) < 0) {
    ssh_queue_close(conn, "banner_failed");
  }

  if (!conn->closing && !conn->banner_received) {
    int wait_ticks = (int)(kernel_tick - conn->accepted_tick);

    if (wait_ticks == 1 || wait_ticks == 8 || wait_ticks == 32)
      ssh_audit_state("ssh_wait_banner_ticks", wait_ticks);
    if (ssh_pump_banner(conn) < 0)
      ssh_queue_close(conn, "banner_invalid");
    else if (conn->banner_received)
      server_audit_line("ssh_banner_pump_done");
  }

  if (!conn->closing && conn->banner_received) {
    if (ssh_pump_rx(conn) < 0)
      ssh_queue_close(conn, "rx_overflow");
    else if (conn->rx_len > 0)
      ssh_audit_state("ssh_rx_len", conn->rx_len);
  }

  if (!conn->closing) {
    result = ssh_flush_auth_pending(conn);
    if (result < 0)
      ssh_queue_close(conn, "queue_failed");
  }

  while (!conn->closing && conn->banner_received && iterations < 8) {
    if (ssh_auth_delay_pending(conn))
      break;
    result = ssh_try_decode_packet(conn, ssh_decode_payload_buf, &payload_len);
    if (result < 0) {
      ssh_queue_close(conn, "packet_invalid");
      break;
    }
    if (result == 0) {
      if (conn->rx_len > 0)
        ssh_audit_state("ssh_packet_wait_rx", conn->rx_len);
      break;
    }
    if (ssh_handle_payload(conn, ssh_decode_payload_buf, payload_len) < 0) {
      ssh_queue_close(conn, ssh_protocol_close_reason(ssh_decode_payload_buf,
                                                      payload_len));
      break;
    }
    iterations++;
  }

  if (!conn->closing) {
    start_result = ssh_start_pending_shell(conn);
    if (start_result < 0) {
      ssh_queue_close(conn, "spawn_failed");
    }
  }

  if (!conn->closing && start_result == 0) {
    ssh_pump_tty_to_channel(conn);
    ssh_poll_shell(conn);
  }

  if (conn->out_count > 0)
    ssh_audit_state("ssh_out_count", conn->out_count);
  ssh_flush_outbox(conn);

  if (conn->closing &&
      conn->out_count == 0 &&
      !ssh_socket_tx_pending(conn->fd)) {
    ssh_release_connection(conn);
    return;
  }
}

PUBLIC void ssh_server_init(void)
{
  ssh_listener_fd = -1;
  ssh_listener_port = 0;
  ssh_signer_fd = -1;
  ssh_runtime_loaded = FALSE;
  ssh_tick_active = FALSE;
  ssh_reset_connection(&ssh_conn);
  ssh_cleanup_tty = 0;
  ssh_cleanup_pid = -1;
}

PUBLIC void ssh_server_tick(void)
{
  int port;

  if (!server_runtime_ssh_enabled())
    return;
  if (ssh_tick_active)
    return;
  ssh_tick_active = TRUE;

  if (!ssh_runtime_loaded && ssh_load_runtime_config() < 0) {
    server_audit_line("ssh_load_failed");
    goto done;
  }

  port = server_runtime_ssh_port();
  if (port <= 0)
    goto done;

  if (ssh_listener_fd < 0 || ssh_listener_port != port) {
    if (ssh_listener_fd >= 0)
      kern_close_socket(ssh_listener_fd);
    ssh_listener_fd = ssh_create_listener(port);
    ssh_listener_port = (ssh_listener_fd >= 0) ? port : 0;
    ssh_audit_state("ssh_listener_fd", ssh_listener_fd);
    if (ssh_listener_fd < 0) {
      server_audit_line("ssh_listener_create_failed");
      goto done;
    }
  }

  ssh_poll_cleanup();
  ssh_accept_pending_connections();
  ssh_poll_connection(&ssh_conn);

done:
  ssh_tick_active = FALSE;
}
#ifdef USERLAND_SSHD_BUILD
PUBLIC int ssh_userland_channel_fd(void)
{
  if (!ssh_conn.in_use || ssh_conn.channel.tty == 0 ||
      !ssh_conn.channel.tty->active) {
    return -1;
  }
  return ssh_conn.channel.tty->fd;
}

PUBLIC void ssh_userland_refresh_runtime(void)
{
  if (ssh_conn.in_use && ssh_conn.fd >= 0)
    ssh_userland_refresh_socket(ssh_conn.fd);
}

PUBLIC int ssh_userland_connection_pending(void)
{
  if (ssh_conn.in_use && ssh_conn.fd >= 0) {
    if (ssh_socket_rx_ready(ssh_conn.fd))
      return TRUE;
    if (ssh_conn.out_count > 0 && !ssh_socket_tx_pending(ssh_conn.fd))
      return TRUE;
  }
  return FALSE;
}

PUBLIC int ssh_userland_listener_pending(void)
{
  if (ssh_conn.in_use || ssh_listener_fd < 0)
    return FALSE;
  return ssh_socket_rx_ready(ssh_listener_fd);
}

PUBLIC void ssh_userland_sync_tick(void)
{
  kernel_tick = get_kernel_tick();
}

PUBLIC void ssh_userland_bootstrap(void)
{
  ssh_server_init();
  server_audit_line("sshd_main_enter");
  for (;;) {
    ssh_userland_sync_tick();
    ssh_userland_load_config();
    if (!server_runtime_ssh_enabled()) {
      server_audit_line("sshd_wait_config");
      sleep_ticks(1);
      continue;
    }
    break;
  }

  ssh_userland_sync_tick();
  ssh_server_tick();
  server_audit_line("sshd_bootstrap_done");
}
#endif
#else
PUBLIC void ssh_server_init(void) {}
PUBLIC void ssh_server_tick(void) {}
PUBLIC void ssh_userland_bootstrap(void) {}
PUBLIC void ssh_userland_sync_tick(void) {}
PUBLIC void ssh_userland_refresh_runtime(void) {}
PUBLIC int ssh_userland_connection_pending(void) { return FALSE; }
PUBLIC int ssh_userland_listener_pending(void) { return FALSE; }
PUBLIC int ssh_userland_channel_fd(void) { return -1; }
#endif
