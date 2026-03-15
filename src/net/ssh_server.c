#ifdef TEST_BUILD
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
#include <signal.h>
#include <tty.h>
#include <uip.h>
#include <network_config.h>
#endif

#include <ssh_server.h>
#include <ssh_crypto.h>

#ifndef TEST_BUILD

#define SSH_BANNER "SSH-2.0-SodexSSH_0.1\r\n"
#define SSH_AUTH_TIMEOUT_TICKS 1000
#define SSH_IDLE_TIMEOUT_TICKS 6000
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

struct ssh_writer {
  u_int8_t *buf;
  int cap;
  int len;
  int error;
};

struct ssh_reader {
  const u_int8_t *buf;
  int len;
  int pos;
  int error;
};

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
  u_int32_t shell_start_tick;
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
  int userauth_service_ready;
  char username[32];
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
PRIVATE struct ssh_connection ssh_conn;
PRIVATE struct tty *ssh_cleanup_tty = 0;
PRIVATE pid_t ssh_cleanup_pid = -1;
PRIVATE int ssh_signer_sig_fd = -1;
PRIVATE int ssh_signer_curve_fd = -1;
PRIVATE u_int8_t ssh_hostkey_public[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES];
PRIVATE u_int8_t ssh_hostkey_secret[SSH_CRYPTO_ED25519_SECRETKEY_BYTES];
PRIVATE u_int8_t ssh_runtime_rng_seed[SSH_CRYPTO_SEED_BYTES];
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

PRIVATE void ssh_move_bytes(u_int8_t *dest, const u_int8_t *src, int len)
{
  int i;

  if (dest == src || len <= 0)
    return;
  if (dest < src) {
    for (i = 0; i < len; i++) {
      dest[i] = src[i];
    }
    return;
  }
  for (i = len - 1; i >= 0; i--) {
    dest[i] = src[i];
  }
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
  admin_runtime_audit_line(message);
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
  admin_runtime_audit_line(message);
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
  admin_runtime_audit_line(message);
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
  admin_runtime_audit_line(message);
}

PRIVATE void ssh_fill_bind_addr(struct sockaddr_in *addr, u_int16_t port)
{
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr = 0;
}

PRIVATE void ssh_write_u32_be(u_int8_t *buf, u_int32_t value)
{
  buf[0] = (u_int8_t)(value >> 24);
  buf[1] = (u_int8_t)(value >> 16);
  buf[2] = (u_int8_t)(value >> 8);
  buf[3] = (u_int8_t)value;
}

PRIVATE u_int32_t ssh_read_u32_be(const u_int8_t *buf)
{
  return ((u_int32_t)buf[0] << 24) |
         ((u_int32_t)buf[1] << 16) |
         ((u_int32_t)buf[2] << 8) |
         (u_int32_t)buf[3];
}

PRIVATE void ssh_writer_init(struct ssh_writer *writer, u_int8_t *buf, int cap)
{
  writer->buf = buf;
  writer->cap = cap;
  writer->len = 0;
  writer->error = FALSE;
}

PRIVATE void ssh_writer_put_data(struct ssh_writer *writer,
                                 const u_int8_t *data, int len)
{
  if (writer->error || len < 0)
    return;
  if (writer->len + len > writer->cap) {
    writer->error = TRUE;
    return;
  }
  if (len > 0 && data != 0)
    memcpy(writer->buf + writer->len, data, (size_t)len);
  writer->len += len;
}

PRIVATE void ssh_writer_put_byte(struct ssh_writer *writer, u_int8_t value)
{
  ssh_writer_put_data(writer, &value, 1);
}

PRIVATE void ssh_writer_put_bool(struct ssh_writer *writer, int value)
{
  ssh_writer_put_byte(writer, value ? 1 : 0);
}

PRIVATE void ssh_writer_put_u32(struct ssh_writer *writer, u_int32_t value)
{
  u_int8_t buf[4];

  ssh_write_u32_be(buf, value);
  ssh_writer_put_data(writer, buf, sizeof(buf));
}

PRIVATE void ssh_writer_put_string(struct ssh_writer *writer,
                                   const u_int8_t *data, int len)
{
  if (len < 0) {
    writer->error = TRUE;
    return;
  }
  ssh_writer_put_u32(writer, (u_int32_t)len);
  ssh_writer_put_data(writer, data, len);
}

PRIVATE void ssh_writer_put_cstring(struct ssh_writer *writer, const char *text)
{
  ssh_writer_put_string(writer, (const u_int8_t *)text, (int)strlen(text));
}

PRIVATE void ssh_writer_put_mpint(struct ssh_writer *writer,
                                  const u_int8_t *data, int len)
{
  int start = 0;
  int out_len;

  while (start < len && data[start] == 0)
    start++;
  if (start == len) {
    ssh_writer_put_u32(writer, 0);
    return;
  }

  out_len = len - start;
  if ((data[start] & 0x80) != 0) {
    ssh_writer_put_u32(writer, (u_int32_t)(out_len + 1));
    ssh_writer_put_byte(writer, 0);
  } else {
    ssh_writer_put_u32(writer, (u_int32_t)out_len);
  }
  ssh_writer_put_data(writer, data + start, out_len);
}

PRIVATE void ssh_reader_init(struct ssh_reader *reader,
                             const u_int8_t *buf, int len)
{
  reader->buf = buf;
  reader->len = len;
  reader->pos = 0;
  reader->error = FALSE;
}

PRIVATE u_int8_t ssh_reader_get_byte(struct ssh_reader *reader)
{
  if (reader->error || reader->pos + 1 > reader->len) {
    reader->error = TRUE;
    return 0;
  }
  return reader->buf[reader->pos++];
}

PRIVATE int ssh_reader_get_bool(struct ssh_reader *reader)
{
  return ssh_reader_get_byte(reader) != 0;
}

PRIVATE u_int32_t ssh_reader_get_u32(struct ssh_reader *reader)
{
  u_int32_t value;

  if (reader->error || reader->pos + 4 > reader->len) {
    reader->error = TRUE;
    return 0;
  }
  value = ssh_read_u32_be(reader->buf + reader->pos);
  reader->pos += 4;
  return value;
}

PRIVATE void ssh_reader_get_string(struct ssh_reader *reader,
                                   const u_int8_t **data, int *len)
{
  u_int32_t size = ssh_reader_get_u32(reader);

  if (reader->error)
    return;
  if (reader->pos + (int)size > reader->len) {
    reader->error = TRUE;
    return;
  }
  *data = reader->buf + reader->pos;
  *len = (int)size;
  reader->pos += (int)size;
}

PRIVATE int ssh_bytes_equal(const u_int8_t *lhs, int lhs_len, const char *rhs)
{
  int rhs_len = (int)strlen(rhs);

  if (lhs_len != rhs_len)
    return FALSE;
  return memcmp(lhs, rhs, (size_t)lhs_len) == 0;
}

PRIVATE int ssh_namelist_has(const u_int8_t *data, int len, const char *name)
{
  int start = 0;

  while (start <= len) {
    int end = start;

    while (end < len && data[end] != ',')
      end++;
    if (end > start && ssh_bytes_equal(data + start, end - start, name))
      return TRUE;
    if (end >= len)
      break;
    start = end + 1;
  }
  return FALSE;
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

  if (admin_runtime_ssh_hostkey_ed25519_public()[0] != '\0' &&
      admin_runtime_ssh_hostkey_ed25519_secret()[0] != '\0') {
    if (ssh_crypto_hex_to_bytes(admin_runtime_ssh_hostkey_ed25519_public(),
                                ssh_hostkey_public,
                                sizeof(ssh_hostkey_public)) < 0) {
      ssh_runtime_loaded = FALSE;
      return -1;
    }
    if (ssh_crypto_hex_to_bytes(admin_runtime_ssh_hostkey_ed25519_secret(),
                                ssh_hostkey_secret,
                                sizeof(ssh_hostkey_secret)) < 0) {
      ssh_runtime_loaded = FALSE;
      return -1;
    }
  } else {
    if (ssh_crypto_hex_to_bytes(admin_runtime_ssh_hostkey_ed25519_seed(),
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
  if (ssh_crypto_hex_to_bytes(admin_runtime_ssh_rng_seed(),
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

PRIVATE int ssh_send_banner(struct ssh_connection *conn)
{
  if (conn->banner_sent)
    return 0;
  if (socket_table[conn->fd].tx_pending != 0) {
    ssh_audit_state("ssh_banner_tx_pending", socket_table[conn->fd].tx_pending);
    return 0;
  }
  ssh_audit_state("ssh_send_banner_fd", conn->fd);
  if (kern_send(conn->fd, (void *)SSH_BANNER, (int)strlen(SSH_BANNER), 0) < 0) {
    admin_runtime_audit_line("ssh_send_banner_failed");
    return -1;
  }
  conn->banner_sent = TRUE;
  conn->last_activity_tick = kernel_tick;
  admin_runtime_audit_line("ssh_send_banner_ok");
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
  ssh_crypto_random_fill(ssh_queue_packet_buf + 5 + payload_len,
                         (size_t)padding_len);

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

  ssh_writer_init(&writer, conn->server_kexinit, sizeof(conn->server_kexinit));
  ssh_writer_put_byte(&writer, SSH_MSG_KEXINIT);
  ssh_crypto_random_fill(cookie, sizeof(cookie));
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
  return 0;
}

PRIVATE int ssh_queue_kexinit(struct ssh_connection *conn)
{
  if (conn->kexinit_sent)
    return 0;
  if (ssh_build_kexinit_payload(conn) < 0)
    return -1;
  if (ssh_queue_payload(conn, conn->server_kexinit,
                        conn->server_kexinit_len, FALSE) < 0) {
    return -1;
  }
  conn->kexinit_sent = TRUE;
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
  int retry = 0;

  while (retry < SSH_SIGNER_RECV_RETRIES) {
    int read_len = kern_recv(fd, buf, len, 0);

    if (read_len < 0)
      return -1;
    if (read_len == 0) {
      retry++;
      continue;
    }
    return read_len == len ? 0 : -1;
  }

  return -1;
}

PRIVATE int ssh_get_signer_fd(int *cached_fd)
{
  if (cached_fd == 0)
    return -1;
  if (*cached_fd >= 0)
    return *cached_fd;

  *cached_fd = kern_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  return *cached_fd;
}

PRIVATE int ssh_request_host_signature(
    u_int8_t signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES],
    const u_int8_t hash[SSH_CRYPTO_SHA256_BYTES])
{
  struct sockaddr_in signer_addr;
  u_int8_t request[SSH_SIGNER_REQUEST_BYTES];
  u_int8_t response[SSH_SIGNER_RESPONSE_BYTES];
  int fd;

  if (admin_runtime_ssh_signer_port() <= 0)
    return -1;

  fd = ssh_get_signer_fd(&ssh_signer_sig_fd);
  if (fd < 0)
    return -1;

  network_fill_gateway_addr(&signer_addr,
                            htons((u_int16_t)admin_runtime_ssh_signer_port()));
  memcpy(request, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES, hash, SSH_CRYPTO_SHA256_BYTES);
  if (kern_sendto(fd, request, sizeof(request), 0, &signer_addr) !=
      (int)sizeof(request)) {
    return -1;
  }
  if (ssh_signer_recv_exact(fd, response, sizeof(response)) < 0)
    return -1;
  if (memcmp(response, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES) != 0)
    return -1;

  memcpy(signature,
         response + SSH_SIGNER_MAGIC_BYTES,
         SSH_CRYPTO_ED25519_SIGNATURE_BYTES);
  return 0;
}

PRIVATE int ssh_request_host_curve25519(
    u_int8_t server_public[SSH_CRYPTO_CURVE25519_BYTES],
    u_int8_t shared_secret[SSH_CRYPTO_CURVE25519_BYTES],
    const u_int8_t server_secret[SSH_CRYPTO_CURVE25519_BYTES],
    const u_int8_t client_public[SSH_CRYPTO_CURVE25519_BYTES])
{
  struct sockaddr_in signer_addr;
  u_int8_t request[SSH_CURVE25519_REQUEST_BYTES];
  u_int8_t response[SSH_CURVE25519_RESPONSE_BYTES];
  int fd;

  if (admin_runtime_ssh_signer_port() <= 0)
    return -1;

  fd = ssh_get_signer_fd(&ssh_signer_curve_fd);
  if (fd < 0)
    return -1;

  network_fill_gateway_addr(&signer_addr,
                            htons((u_int16_t)admin_runtime_ssh_signer_port()));
  memcpy(request, SSH_CURVE25519_MAGIC, SSH_SIGNER_MAGIC_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES,
         server_secret,
         SSH_CRYPTO_CURVE25519_BYTES);
  memcpy(request + SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES,
         client_public,
         SSH_CRYPTO_CURVE25519_BYTES);
  if (kern_sendto(fd, request, sizeof(request), 0, &signer_addr) !=
      (int)sizeof(request)) {
    return -1;
  }
  if (ssh_signer_recv_exact(fd, response, sizeof(response)) < 0)
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
  admin_runtime_audit_line(message);
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
  struct ssh_reader reader;
  struct ssh_writer writer;

  ssh_audit_sign_mode("kex_start", admin_runtime_ssh_signer_port());
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &client_public, &client_public_len);
  if (reader.error || client_public_len != SSH_CRYPTO_CURVE25519_BYTES)
    return -1;

  ssh_crypto_random_seed(ssh_runtime_rng_seed);
  ssh_crypto_random_fill(ssh_kex_server_secret, sizeof(ssh_kex_server_secret));
  if (admin_runtime_ssh_signer_port() > 0) {
    if (ssh_request_host_curve25519(ssh_kex_server_public, ssh_kex_shared_secret,
                                    ssh_kex_server_secret, client_public) < 0) {
      return -1;
    }
  } else {
    if (ssh_crypto_curve25519_public_key(ssh_kex_server_public,
                                         ssh_kex_server_secret) < 0)
      return -1;
    if (ssh_crypto_curve25519_shared(ssh_kex_shared_secret,
                                     ssh_kex_server_secret,
                                     client_public) < 0) {
      return -1;
    }
  }

  hostkey_len = ssh_build_hostkey_blob(ssh_kex_hostkey_blob,
                                       sizeof(ssh_kex_hostkey_blob));
  if (hostkey_len < 0)
    return -1;

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
  if (!conn->session_id_set) {
    memcpy(conn->session_id, conn->exchange_hash, sizeof(conn->session_id));
    conn->session_id_set = TRUE;
  }
  if (admin_runtime_ssh_signer_port() > 0) {
    ssh_audit_sign_mode("remote", admin_runtime_ssh_signer_port());
    if (ssh_request_host_signature(ssh_kex_signature, conn->exchange_hash) < 0) {
      ssh_audit_sign_mode("remote_fail", admin_runtime_ssh_signer_port());
      return -1;
    }
  } else {
    ssh_audit_sign_mode("local", 0);
    if (ssh_crypto_ed25519_sign(ssh_kex_signature, ssh_hostkey_secret,
                                conn->exchange_hash,
                                sizeof(conn->exchange_hash)) < 0) {
      return -1;
    }
  }
  ssh_audit_sign_mode("sign_done", admin_runtime_ssh_signer_port());

  ssh_writer_init(&writer, ssh_kex_signature_blob, sizeof(ssh_kex_signature_blob));
  ssh_writer_put_cstring(&writer, "ssh-ed25519");
  ssh_writer_put_string(&writer,
                        ssh_kex_signature,
                        sizeof(ssh_kex_signature));
  if (writer.error)
    return -1;
  signature_len = writer.len;
  ssh_audit_sign_mode("sig_blob_done", admin_runtime_ssh_signer_port());

  ssh_derive_key_material(conn,
                          ssh_kex_shared_secret,
                          sizeof(ssh_kex_shared_secret));
  ssh_audit_sign_mode("derive_done", admin_runtime_ssh_signer_port());

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
  ssh_audit_sign_mode("reply_done", admin_runtime_ssh_signer_port());

  ssh_kex_reply[0] = SSH_MSG_NEWKEYS;
  if (ssh_queue_payload(conn, ssh_kex_reply, 1, TRUE) < 0)
    return -1;
  ssh_audit_state("ssh_newkeys_payload_len", 1);
  ssh_audit_sign_mode("newkeys_done", admin_runtime_ssh_signer_port());
  conn->newkeys_sent = TRUE;
  admin_runtime_audit_line("ssh_kex_return");
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

  admin_runtime_audit_line("ssh_spawn_begin");
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
  admin_runtime_audit_line("ssh_spawn_ok");
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
  struct ssh_reader reader;
  const u_int8_t *name = 0;
  int name_len = 0;

  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &name, &name_len);
  if (reader.error)
    return -1;

  if (ssh_bytes_equal(name, name_len, "ssh-userauth")) {
    conn->userauth_service_ready = TRUE;
    return ssh_queue_service_accept(conn, "ssh-userauth");
  }
  if (conn->auth_done && ssh_bytes_equal(name, name_len, "ssh-connection")) {
    return ssh_queue_service_accept(conn, "ssh-connection");
  }
  return -1;
}

PRIVATE int ssh_handle_userauth_request(struct ssh_connection *conn,
                                        const u_int8_t *payload, int payload_len)
{
  struct ssh_reader reader;
  const u_int8_t *username = 0;
  const u_int8_t *service = 0;
  const u_int8_t *method = 0;
  const u_int8_t *password = 0;
  int username_len = 0;
  int service_len = 0;
  int method_len = 0;
  int password_len = 0;
  int change_request;
  const char *expected_password = admin_runtime_ssh_password();

  if (!conn->userauth_service_ready)
    return -1;

  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &username, &username_len);
  ssh_reader_get_string(&reader, &service, &service_len);
  ssh_reader_get_string(&reader, &method, &method_len);
  if (reader.error)
    return -1;

  if (!ssh_bytes_equal(method, method_len, "password")) {
    return ssh_queue_userauth_failure(conn);
  }

  change_request = ssh_reader_get_bool(&reader);
  ssh_reader_get_string(&reader, &password, &password_len);
  if (reader.error)
    return -1;

  if (!change_request &&
      ssh_bytes_equal(username, username_len, "root") &&
      ssh_bytes_equal(service, service_len, "ssh-connection") &&
      password_len == (int)strlen(expected_password) &&
      memcmp(password, expected_password, (size_t)password_len) == 0) {
    conn->auth_done = TRUE;
    ssh_copy_string(conn->username, sizeof(conn->username), "root");
    ssh_audit_peer("ssh_auth_success", conn->peer_addr);
    return ssh_queue_userauth_success(conn);
  }

  ssh_audit_peer("ssh_auth_failure", conn->peer_addr);
  return ssh_queue_userauth_failure(conn);
}

PRIVATE int ssh_handle_channel_open(struct ssh_connection *conn,
                                    const u_int8_t *payload, int payload_len)
{
  struct ssh_reader reader;
  const u_int8_t *type = 0;
  int type_len = 0;
  u_int32_t peer_id;
  u_int32_t peer_window;
  u_int32_t peer_max_packet;

  if (!conn->auth_done)
    return -1;

  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &type, &type_len);
  peer_id = ssh_reader_get_u32(&reader);
  peer_window = ssh_reader_get_u32(&reader);
  peer_max_packet = ssh_reader_get_u32(&reader);
  if (reader.error)
    return -1;

  if (conn->channel.open || !ssh_bytes_equal(type, type_len, "session")) {
    return ssh_queue_channel_open_failure(conn, peer_id, "session_only");
  }

  conn->channel.open = TRUE;
  conn->channel.peer_id = peer_id;
  conn->channel.peer_window = peer_window;
  conn->channel.peer_max_packet = peer_max_packet;
  conn->channel.local_id = 0;
  conn->channel.cols = SSH_DEFAULT_COLS;
  conn->channel.rows = SSH_DEFAULT_ROWS;
  admin_runtime_audit_line("ssh_channel_open_ok");
  return ssh_queue_channel_open_confirmation(conn);
}

PRIVATE int ssh_handle_channel_request(struct ssh_connection *conn,
                                       const u_int8_t *payload, int payload_len)
{
  struct ssh_reader reader;
  const u_int8_t *request = 0;
  const u_int8_t *ignored = 0;
  int request_len = 0;
  int ignored_len = 0;
  u_int32_t recipient;
  int want_reply;

  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  recipient = ssh_reader_get_u32(&reader);
  ssh_reader_get_string(&reader, &request, &request_len);
  want_reply = ssh_reader_get_bool(&reader);
  if (reader.error || !conn->channel.open || recipient != conn->channel.local_id)
    return -1;

  if (ssh_bytes_equal(request, request_len, "pty-req")) {
    admin_runtime_audit_line("ssh_pty_req");
    ssh_reader_get_string(&reader, &ignored, &ignored_len);
    conn->channel.cols = (u_int16_t)ssh_reader_get_u32(&reader);
    conn->channel.rows = (u_int16_t)ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    ssh_reader_get_string(&reader, &ignored, &ignored_len);
    if (reader.error)
      return -1;
    conn->channel.pty_requested = TRUE;
    if (conn->channel.tty != 0)
      tty_set_winsize(conn->channel.tty, conn->channel.cols, conn->channel.rows);
    return want_reply ? ssh_queue_channel_status(conn, TRUE) : 0;
  }

  if (ssh_bytes_equal(request, request_len, "window-change")) {
    admin_runtime_audit_line("ssh_window_change");
    conn->channel.cols = (u_int16_t)ssh_reader_get_u32(&reader);
    conn->channel.rows = (u_int16_t)ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    if (reader.error)
      return -1;
    if (conn->channel.tty != 0)
      tty_set_winsize(conn->channel.tty, conn->channel.cols, conn->channel.rows);
    return want_reply ? ssh_queue_channel_status(conn, TRUE) : 0;
  }

  if (ssh_bytes_equal(request, request_len, "shell")) {
    admin_runtime_audit_line("ssh_shell_req");
    if (ssh_spawn_shell(conn) < 0)
      return want_reply ? ssh_queue_channel_status(conn, FALSE) : -1;
    conn->channel.shell_reply_pending = want_reply ? TRUE : FALSE;
    return 0;
  }

  return want_reply ? ssh_queue_channel_status(conn, FALSE) : 0;
}

PRIVATE int ssh_handle_channel_data(struct ssh_connection *conn,
                                    const u_int8_t *payload, int payload_len)
{
  struct ssh_reader reader;
  const u_int8_t *data = 0;
  int data_len = 0;
  u_int32_t recipient;

  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  recipient = ssh_reader_get_u32(&reader);
  ssh_reader_get_string(&reader, &data, &data_len);
  if (reader.error || !conn->channel.open || recipient != conn->channel.local_id)
    return -1;
  if (conn->channel.tty == 0)
    return 0;

  conn->last_activity_tick = kernel_tick;
  tty_master_write(conn->channel.tty, data, (size_t)data_len);
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
  u_int32_t packet_length;
  int padding_len;
  int total_len;

  if (conn->rx_len < 4)
    return 0;

  packet_length = ssh_read_u32_be(conn->rx_buf);
  total_len = (int)packet_length + 4;
  if (packet_length < 5 || total_len > conn->rx_len)
    return 0;
  if (total_len > SSH_PACKET_PLAIN_MAX)
    return -1;

  padding_len = conn->rx_buf[4];
  if (padding_len < 4 || (int)packet_length - padding_len - 1 < 0)
    return -1;

  *payload_len = (int)packet_length - padding_len - 1;
  memcpy(payload, conn->rx_buf + 5, (size_t)*payload_len);
  ssh_move_bytes(conn->rx_buf, conn->rx_buf + total_len, conn->rx_len - total_len);
  conn->rx_len -= total_len;
  conn->rx_seq++;
  return 1;
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

  for (;;) {
    disableInterrupt();
    read_len = rxbuf_read_direct(conn->fd, chunk, sizeof(chunk), 0);
    enableInterrupt();
    if (read_len <= 0)
      break;

    {
      int i;

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
          if (i + 1 < read_len) {
            if (ssh_append_rx(conn, chunk + i + 1, read_len - i - 1) < 0)
              return -1;
          }
          if (ssh_queue_kexinit(conn) < 0)
            return -1;
          break;
        }
      }
    }
  }
  return 0;
}

PRIVATE int ssh_pump_rx(struct ssh_connection *conn)
{
  u_int8_t chunk[256];
  int read_len;

  for (;;) {
    disableInterrupt();
    read_len = rxbuf_read_direct(conn->fd, chunk, sizeof(chunk), 0);
    enableInterrupt();
    if (read_len <= 0)
      break;
    if (ssh_append_rx(conn, chunk, read_len) < 0)
      return -1;
    conn->last_activity_tick = kernel_tick;
  }
  return 0;
}

PRIVATE void ssh_flush_outbox(struct ssh_connection *conn)
{
  struct ssh_out_packet *packet;

  if (conn->out_count <= 0)
    return;
  if (socket_table[conn->fd].tx_pending != 0)
    return;

  packet = &conn->outbox[conn->out_tail];
  if (kern_send(conn->fd, packet->data, packet->len, 0) < 0) {
    ssh_queue_close(conn, "send_failed");
    return;
  }
  if (packet->activate_tx_crypto)
    conn->tx_encrypted = TRUE;
  conn->out_tail = (conn->out_tail + 1) % SSH_OUTBOX_MAX;
  conn->out_count--;
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
  if (read_len <= 0)
    return;

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
  (void)conn;
  /* `eshell` には明示的な exit request がなく、`process_has_pid()` だけで
   * shell 終了を判定すると command 実行中に誤って close することがある。
   * cleanup は peer close / socket close / timeout 側へ寄せる。 */
}

PRIVATE void ssh_accept_pending_connections(void)
{
  for (;;) {
    struct sockaddr_in peer;
    int child_fd;
    int auth_result;

    disableInterrupt();
    child_fd = socket_try_accept(ssh_listener_fd, &peer);
    enableInterrupt();
    if (child_fd < 0)
      return;

    auth_result = admin_authorize_peer(peer.sin_addr, 0);
    if (auth_result != ADMIN_AUTH_ALLOW) {
      kern_close_socket(child_fd);
      continue;
    }

    if (ssh_conn.in_use) {
      kern_close_socket(child_fd);
      continue;
    }

    ssh_reset_connection(&ssh_conn);
    ssh_conn.in_use = TRUE;
    ssh_conn.fd = child_fd;
    ssh_conn.peer_addr = peer.sin_addr;
    ssh_conn.accepted_tick = kernel_tick;
    ssh_conn.last_activity_tick = kernel_tick;
    ssh_crypto_random_seed(ssh_runtime_rng_seed);
    ssh_audit_peer("accept_ssh", peer.sin_addr);
    ssh_audit_state("ssh_accept_fd", child_fd);
  }
}

PRIVATE void ssh_poll_connection(struct ssh_connection *conn)
{
  int payload_len;
  int result;
  int start_result = 0;
  u_int32_t timeout_ticks;
  int iterations = 0;

  if (!conn->in_use)
    return;

  if (socket_table[conn->fd].state == SOCK_STATE_CLOSED) {
    ssh_release_connection(conn);
    return;
  }

  timeout_ticks = conn->auth_done ? SSH_IDLE_TIMEOUT_TICKS
                                  : SSH_AUTH_TIMEOUT_TICKS;
  if (!conn->closing &&
      (int)(kernel_tick - conn->last_activity_tick) > (int)timeout_ticks) {
    ssh_queue_close(conn, "timeout");
  }

  if (ssh_send_banner(conn) < 0) {
    ssh_queue_close(conn, "banner_failed");
  }

  if (!conn->closing && !conn->banner_received) {
    if (ssh_pump_banner(conn) < 0)
      ssh_queue_close(conn, "banner_invalid");
  }

  if (!conn->closing && conn->banner_received) {
    if (ssh_pump_rx(conn) < 0)
      ssh_queue_close(conn, "rx_overflow");
  }

  while (!conn->closing && conn->banner_received && iterations < 8) {
    result = ssh_try_decode_packet(conn, ssh_decode_payload_buf, &payload_len);
    if (result < 0) {
      ssh_queue_close(conn, "packet_invalid");
      break;
    }
    if (result == 0)
      break;
    if (ssh_handle_payload(conn, ssh_decode_payload_buf, payload_len) < 0) {
      ssh_queue_close(conn, "protocol_error");
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

  ssh_flush_outbox(conn);

  if (conn->closing &&
      conn->out_count == 0 &&
      socket_table[conn->fd].tx_pending == 0 &&
      !conn->close_initiated &&
      (int)(kernel_tick - conn->close_started_tick) >= SSH_CLOSE_DRAIN_TICKS) {
    socket_begin_close(conn->fd);
    conn->close_initiated = TRUE;
  }
}

PUBLIC void ssh_server_init(void)
{
  ssh_listener_fd = -1;
  ssh_listener_port = 0;
  ssh_runtime_loaded = FALSE;
  ssh_tick_active = FALSE;
  ssh_reset_connection(&ssh_conn);
  ssh_cleanup_tty = 0;
  ssh_cleanup_pid = -1;
  ssh_signer_sig_fd = -1;
  ssh_signer_curve_fd = -1;
}

PUBLIC void ssh_server_tick(void)
{
  int port;

  if (!admin_runtime_ssh_enabled())
    return;
  if (ssh_tick_active)
    return;
  ssh_tick_active = TRUE;

  if (!ssh_runtime_loaded && ssh_load_runtime_config() < 0) {
    admin_runtime_audit_line("ssh_load_failed");
    goto done;
  }

  port = admin_runtime_ssh_port();
  if (port <= 0)
    goto done;

  if (ssh_listener_fd < 0 || ssh_listener_port != port) {
    if (ssh_listener_fd >= 0)
      kern_close_socket(ssh_listener_fd);
    ssh_listener_fd = ssh_create_listener(port);
    ssh_listener_port = (ssh_listener_fd >= 0) ? port : 0;
    if (ssh_listener_fd < 0) {
      admin_runtime_audit_line("ssh_listener_create_failed");
      goto done;
    }
  }

  ssh_poll_cleanup();
  ssh_accept_pending_connections();
  ssh_poll_connection(&ssh_conn);

done:
  ssh_tick_active = FALSE;
}
#else
PUBLIC void ssh_server_init(void) {}
PUBLIC void ssh_server_tick(void) {}
#endif
