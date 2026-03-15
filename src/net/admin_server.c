#ifdef TEST_BUILD
#include <stdint.h>
#include <string.h>

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;

#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef PRIVATE
#define PRIVATE static
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

static u_int32_t kernel_tick = 0;
#else
#include <sys/types.h>
#include <string.h>
#include <vga.h>
#include <io.h>
#include <rs232c.h>
#include <uip.h>
#include <ext3fs.h>
#include <fs.h>
#include <process.h>
#include <socket.h>
EXTERN volatile u_int32_t kernel_tick;
#endif

#include <admin_server.h>

#ifndef SODEX_ADMIN_STATUS_TOKEN
#define SODEX_ADMIN_STATUS_TOKEN ""
#endif

#ifndef SODEX_ADMIN_CONTROL_TOKEN
#define SODEX_ADMIN_CONTROL_TOKEN ""
#endif

#ifndef SODEX_ADMIN_ALLOW_IP0
#define SODEX_ADMIN_ALLOW_IP0 10
#endif

#ifndef SODEX_ADMIN_ALLOW_IP1
#define SODEX_ADMIN_ALLOW_IP1 0
#endif

#ifndef SODEX_ADMIN_ALLOW_IP2
#define SODEX_ADMIN_ALLOW_IP2 2
#endif

#ifndef SODEX_ADMIN_ALLOW_IP3
#define SODEX_ADMIN_ALLOW_IP3 2
#endif

#ifndef SODEX_SSH_PORT
#define SODEX_SSH_PORT 0
#endif

#ifndef SODEX_SSH_PASSWORD
#define SODEX_SSH_PASSWORD ""
#endif

#ifndef SODEX_SSH_HOSTKEY_ED25519_SEED
#define SODEX_SSH_HOSTKEY_ED25519_SEED ""
#endif

#ifndef SODEX_SSH_RNG_SEED
#define SODEX_SSH_RNG_SEED ""
#endif

#define ADMIN_AUDIT_LINES 16
#define ADMIN_AUDIT_LINE_SIZE 96
#define ADMIN_MAX_CONNECTIONS 2
#define ADMIN_IDLE_TIMEOUT_TICKS 500
#define ADMIN_CONFIG_MAX 256
#define ADMIN_CLOSE_DRAIN_TICKS 60
#define ADMIN_AUTH_BUCKETS 8
#define ADMIN_AUTH_WINDOW_TICKS 300
#define ADMIN_AUTH_FAILURE_THRESHOLD 3
#define ADMIN_AUTH_BACKOFF_BASE_TICKS 100
#define ADMIN_AUTH_BACKOFF_MAX_SHIFT 3

struct admin_auth_bucket {
  int in_use;
  u_int32_t peer_addr;
  u_int32_t window_start_tick;
  u_int32_t last_seen_tick;
  int failure_count;
  u_int32_t blocked_until_tick;
};

struct admin_runtime_state {
  char status_token[ADMIN_TOKEN_MAX];
  char control_token[ADMIN_TOKEN_MAX];
  u_int32_t allow_ip;
  u_int16_t debug_shell_port;
  u_int16_t ssh_port;
  char ssh_password[ADMIN_SECRET_MAX];
  char ssh_hostkey_ed25519_seed[ADMIN_HEX_SEED_MAX];
  char ssh_rng_seed[ADMIN_HEX_SEED_MAX];
  int agent_running;
  int agent_start_count;
  int agent_stop_count;
  u_int32_t last_transition_tick;
  char audit_lines[ADMIN_AUDIT_LINES][ADMIN_AUDIT_LINE_SIZE];
  int audit_head;
  int audit_count;
  struct admin_auth_bucket auth_buckets[ADMIN_AUTH_BUCKETS];
  int listener_ready_mask;
  int listener_ready_announced;
};

struct admin_runtime_state admin_runtime;

#ifndef TEST_BUILD
struct admin_connection {
  int in_use;
  int fd;
  int closing;
  int close_started_tick;
  int close_initiated;
  u_int32_t peer_addr;
  u_int32_t accepted_tick;
  int length;
  char buffer[ADMIN_TEXT_REQUEST_MAX];
  char response[ADMIN_RESPONSE_MAX];
  struct admin_request request;
};

PRIVATE int admin_listener_fd = -1;
PRIVATE struct admin_connection admin_connections[ADMIN_MAX_CONNECTIONS];
PRIVATE int admin_listener_state_log = 0;
#endif

PRIVATE void admin_copy_string(char *dest, int cap, const char *src)
{
  int i = 0;

  if (dest == 0 || cap <= 0) return;
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

PRIVATE int admin_strings_equal(const char *lhs, const char *rhs)
{
  if (lhs == 0 || rhs == 0) return FALSE;
  return strcmp(lhs, rhs) == 0;
}

PRIVATE int admin_append_char(char *buf, int cap, int pos, char c)
{
  if (pos < 0 || pos >= cap - 1) return pos;
  buf[pos++] = c;
  buf[pos] = '\0';
  return pos;
}

PRIVATE int admin_append_text(char *buf, int cap, int pos, const char *text)
{
  int i = 0;

  if (buf == 0 || cap <= 0 || pos < 0) return pos;
  if (text == 0) return pos;
  while (text[i] != '\0' && pos < cap - 1) {
    buf[pos++] = text[i++];
  }
  buf[pos] = '\0';
  return pos;
}

PRIVATE int admin_append_int(char *buf, int cap, int pos, int value)
{
  char digits[16];
  int len = 0;
  unsigned int current;

  if (value < 0) {
    pos = admin_append_char(buf, cap, pos, '-');
    current = (unsigned int)(-value);
  } else {
    current = (unsigned int)value;
  }

  if (current == 0) {
    digits[len++] = '0';
  } else {
    while (current > 0 && len < (int)sizeof(digits)) {
      digits[len++] = (char)('0' + (current % 10));
      current /= 10;
    }
  }

  while (len > 0) {
    pos = admin_append_char(buf, cap, pos, digits[--len]);
  }
  return pos;
}

PRIVATE int admin_append_uptime(char *buf, int cap, int pos)
{
  return admin_append_int(buf, cap, pos, (int)kernel_tick);
}

PRIVATE int admin_parse_positive_int(const char *text)
{
  int value = 0;
  int i = 0;

  if (text == 0 || text[0] == '\0') return -1;
  while (text[i] != '\0') {
    if (text[i] < '0' || text[i] > '9')
      return -1;
    value = value * 10 + (text[i] - '0');
    i++;
  }
  return value;
}

PRIVATE int admin_is_hex_string(const char *text)
{
  int i = 0;

  if (text == 0 || text[0] == '\0')
    return FALSE;

  while (text[i] != '\0') {
    char c = text[i];

    if (!((c >= '0' && c <= '9') ||
          (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return FALSE;
    }
    i++;
  }
  return TRUE;
}

PRIVATE int admin_parse_ipv4(const char *text, u_int32_t *out)
{
  u_int8_t raw[4];
  int part = 0;
  int value = 0;
  int digits = 0;
  int i = 0;

  if (text == 0 || out == 0) return FALSE;

  while (TRUE) {
    char c = text[i];

    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      digits++;
      if (digits > 3 || value > 255)
        return FALSE;
    } else if (c == '.' || c == '\0') {
      if (digits == 0 || part >= 4)
        return FALSE;
      raw[part++] = (u_int8_t)value;
      value = 0;
      digits = 0;
      if (c == '\0')
        break;
    } else {
      return FALSE;
    }
    i++;
  }

  if (part != 4)
    return FALSE;

  memcpy(out, raw, sizeof(raw));
  return TRUE;
}

PRIVATE void admin_trim_line(const char *line, int len, char *out, int out_cap)
{
  int start = 0;
  int end = len;
  int pos = 0;

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

PRIVATE void admin_audit_line(const char *line)
{
  int slot;

  if (line == 0 || line[0] == '\0') return;

  slot = admin_runtime.audit_head;
  admin_copy_string(admin_runtime.audit_lines[slot], ADMIN_AUDIT_LINE_SIZE, line);
  admin_runtime.audit_head = (admin_runtime.audit_head + 1) % ADMIN_AUDIT_LINES;
  if (admin_runtime.audit_count < ADMIN_AUDIT_LINES)
    admin_runtime.audit_count++;

#ifndef TEST_BUILD
  _kprintf("AUDIT %s\n", admin_runtime.audit_lines[slot]);
  com1_puts("AUDIT ");
  com1_puts((char *)admin_runtime.audit_lines[slot]);
  com1_puts("\n");
#endif
}

PRIVATE void admin_format_ip(u_int32_t peer_addr, char *buf, int cap)
{
  u_int8_t *raw = (u_int8_t *)&peer_addr;
  int pos = 0;

  if (buf == 0 || cap <= 0) return;
  buf[0] = '\0';
  pos = admin_append_int(buf, cap, pos, raw[0]);
  pos = admin_append_char(buf, cap, pos, '.');
  pos = admin_append_int(buf, cap, pos, raw[1]);
  pos = admin_append_char(buf, cap, pos, '.');
  pos = admin_append_int(buf, cap, pos, raw[2]);
  pos = admin_append_char(buf, cap, pos, '.');
  admin_append_int(buf, cap, pos, raw[3]);
}

#ifndef TEST_BUILD
PRIVATE void admin_audit_peer(const char *prefix, u_int32_t peer_addr)
{
  char message[ADMIN_AUDIT_LINE_SIZE];
  char ipbuf[24];
  int pos = 0;

  admin_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  pos = admin_append_text(message, sizeof(message), pos, prefix);
  pos = admin_append_text(message, sizeof(message), pos, " peer=");
  pos = admin_append_text(message, sizeof(message), pos, ipbuf);
  message[pos] = '\0';
  admin_audit_line(message);
}
#endif

PRIVATE void admin_audit_auth_event(const char *prefix, const char *reason,
                                    u_int32_t peer_addr, int count,
                                    u_int32_t retry_after_ticks)
{
  char message[ADMIN_AUDIT_LINE_SIZE];
  char ipbuf[24];
  int pos = 0;

  admin_format_ip(peer_addr, ipbuf, sizeof(ipbuf));
  pos = admin_append_text(message, sizeof(message), pos, prefix);
  pos = admin_append_text(message, sizeof(message), pos, " peer=");
  pos = admin_append_text(message, sizeof(message), pos, ipbuf);
  if (reason != 0 && reason[0] != '\0') {
    pos = admin_append_text(message, sizeof(message), pos, " reason=");
    pos = admin_append_text(message, sizeof(message), pos, reason);
  }
  if (count > 0) {
    pos = admin_append_text(message, sizeof(message), pos, " count=");
    pos = admin_append_int(message, sizeof(message), pos, count);
  }
  if (retry_after_ticks > 0) {
    pos = admin_append_text(message, sizeof(message), pos, " retry=");
    pos = admin_append_int(message, sizeof(message), pos, (int)retry_after_ticks);
  }
  message[pos] = '\0';
  admin_audit_line(message);
}

PRIVATE struct admin_auth_bucket *admin_find_auth_bucket(u_int32_t peer_addr,
                                                         int create)
{
  int i;
  int free_slot = -1;
  int oldest_slot = -1;

  for (i = 0; i < ADMIN_AUTH_BUCKETS; i++) {
    struct admin_auth_bucket *bucket = &admin_runtime.auth_buckets[i];

    if (bucket->in_use) {
      if (bucket->peer_addr == peer_addr)
        return bucket;
      if (oldest_slot < 0 ||
          bucket->last_seen_tick <
              admin_runtime.auth_buckets[oldest_slot].last_seen_tick) {
        oldest_slot = i;
      }
    } else if (free_slot < 0) {
      free_slot = i;
    }
  }

  if (!create)
    return 0;

  if (free_slot >= 0)
    oldest_slot = free_slot;
  if (oldest_slot < 0)
    return 0;

  memset(&admin_runtime.auth_buckets[oldest_slot], 0,
         sizeof(struct admin_auth_bucket));
  admin_runtime.auth_buckets[oldest_slot].in_use = TRUE;
  admin_runtime.auth_buckets[oldest_slot].peer_addr = peer_addr;
  admin_runtime.auth_buckets[oldest_slot].window_start_tick = kernel_tick;
  admin_runtime.auth_buckets[oldest_slot].last_seen_tick = kernel_tick;
  return &admin_runtime.auth_buckets[oldest_slot];
}

PRIVATE void admin_refresh_auth_bucket(struct admin_auth_bucket *bucket)
{
  if (bucket == 0 || !bucket->in_use)
    return;

  if ((int)(kernel_tick - bucket->window_start_tick) > ADMIN_AUTH_WINDOW_TICKS) {
    bucket->window_start_tick = kernel_tick;
    bucket->failure_count = 0;
  }
  if (bucket->blocked_until_tick != 0 &&
      bucket->blocked_until_tick <= kernel_tick) {
    bucket->blocked_until_tick = 0;
  }
  bucket->last_seen_tick = kernel_tick;
}

PRIVATE u_int32_t admin_auth_backoff_ticks(int failure_count)
{
  u_int32_t backoff = ADMIN_AUTH_BACKOFF_BASE_TICKS;
  int extra_failures = failure_count - ADMIN_AUTH_FAILURE_THRESHOLD - 1;
  int shift = 0;

  if (failure_count <= ADMIN_AUTH_FAILURE_THRESHOLD)
    return 0;

  while (extra_failures > 0 && shift < ADMIN_AUTH_BACKOFF_MAX_SHIFT) {
    backoff <<= 1;
    extra_failures--;
    shift++;
  }
  return backoff;
}

PRIVATE int admin_is_peer_throttled(u_int32_t peer_addr,
                                    u_int32_t *retry_after_ticks)
{
  struct admin_auth_bucket *bucket = admin_find_auth_bucket(peer_addr, FALSE);

  if (retry_after_ticks != 0)
    *retry_after_ticks = 0;
  if (bucket == 0)
    return FALSE;

  admin_refresh_auth_bucket(bucket);
  if (bucket->blocked_until_tick == 0)
    return FALSE;

  if (retry_after_ticks != 0)
    *retry_after_ticks = bucket->blocked_until_tick - kernel_tick;
  admin_audit_auth_event("auth_throttle", "backoff", peer_addr,
                         bucket->failure_count,
                         retry_after_ticks ? *retry_after_ticks : 0);
  return TRUE;
}

PRIVATE int admin_record_auth_failure(u_int32_t peer_addr, const char *reason,
                                      u_int32_t *retry_after_ticks)
{
  struct admin_auth_bucket *bucket = admin_find_auth_bucket(peer_addr, TRUE);
  u_int32_t backoff;

  if (retry_after_ticks != 0)
    *retry_after_ticks = 0;
  if (bucket == 0)
    return ADMIN_AUTH_DENY;

  admin_refresh_auth_bucket(bucket);
  bucket->failure_count++;

  backoff = admin_auth_backoff_ticks(bucket->failure_count);
  if (backoff > 0) {
    bucket->blocked_until_tick = kernel_tick + backoff;
    if (retry_after_ticks != 0)
      *retry_after_ticks = backoff;
    admin_audit_auth_event("auth_throttle", reason, peer_addr,
                           bucket->failure_count, backoff);
    return ADMIN_AUTH_THROTTLED;
  }

  admin_audit_auth_event("auth_reject", reason, peer_addr,
                         bucket->failure_count, 0);
  return ADMIN_AUTH_DENY;
}

PRIVATE void admin_record_auth_success(u_int32_t peer_addr)
{
  struct admin_auth_bucket *bucket = admin_find_auth_bucket(peer_addr, FALSE);

  if (bucket == 0)
    return;

  bucket->failure_count = 0;
  bucket->blocked_until_tick = 0;
  bucket->window_start_tick = kernel_tick;
  bucket->last_seen_tick = kernel_tick;
}

PUBLIC void admin_runtime_note_listener_ready(int listener_kind)
{
  char message[ADMIN_AUDIT_LINE_SIZE];
  char ipbuf[24];
  int pos = 0;
  int listener_bit;
  int ready_mask;

  if (listener_kind == ADMIN_LISTENER_ADMIN) {
    listener_bit = ADMIN_LISTENER_ADMIN;
    admin_audit_line("listener_ready kind=admin port=10023");
  } else if (listener_kind == ADMIN_LISTENER_HTTP) {
    listener_bit = ADMIN_LISTENER_HTTP;
    admin_audit_line("listener_ready kind=http port=8080");
  } else if (listener_kind == ADMIN_LISTENER_SSH) {
    listener_bit = ADMIN_LISTENER_SSH;
    pos = admin_append_text(message, sizeof(message), pos,
                            "listener_ready kind=ssh port=");
    pos = admin_append_int(message, sizeof(message), pos,
                           (int)admin_runtime.ssh_port);
    message[pos] = '\0';
    admin_audit_line(message);
  } else {
    return;
  }

  if ((admin_runtime.listener_ready_mask & listener_bit) != 0)
    return;
  admin_runtime.listener_ready_mask |= listener_bit;

  ready_mask = admin_runtime.listener_ready_mask &
               (ADMIN_LISTENER_ADMIN | ADMIN_LISTENER_HTTP);
  if (admin_runtime.listener_ready_announced ||
      ready_mask != (ADMIN_LISTENER_ADMIN | ADMIN_LISTENER_HTTP)) {
    return;
  }

  admin_runtime.listener_ready_announced = TRUE;
  admin_format_ip(admin_runtime.allow_ip, ipbuf, sizeof(ipbuf));
  pos = admin_append_text(message, sizeof(message), pos,
                          "server_runtime_ready allow_ip=");
  pos = admin_append_text(message, sizeof(message), pos, ipbuf);
  pos = admin_append_text(message, sizeof(message), pos, " admin=10023 http=8080");
  message[pos] = '\0';
  admin_audit_line(message);
}

PRIVATE int admin_apply_config_line(const char *line)
{
  char key[32];
  char value[ADMIN_SECRET_MAX];
  char raw[ADMIN_TEXT_REQUEST_MAX];
  char *sep;
  u_int32_t allow_ip;
  int port;

  if (line == 0)
    return 0;

  admin_trim_line(line, (int)strlen(line), raw, sizeof(raw));
  if (raw[0] == '\0' || raw[0] == '#')
    return 0;

  sep = strchr(raw, '=');
  if (sep == 0)
    return -1;
  *sep = '\0';

  admin_trim_line(raw, (int)strlen(raw), key, sizeof(key));
  admin_trim_line(sep + 1, (int)strlen(sep + 1), value, sizeof(value));

  if (strcmp(key, "status_token") == 0) {
    admin_copy_string(admin_runtime.status_token,
                      sizeof(admin_runtime.status_token), value);
    return 1;
  }
  if (strcmp(key, "control_token") == 0) {
    admin_copy_string(admin_runtime.control_token,
                      sizeof(admin_runtime.control_token), value);
    return 1;
  }
  if (strcmp(key, "allow_ip") == 0) {
    if (!admin_parse_ipv4(value, &allow_ip))
      return -1;
    admin_runtime.allow_ip = allow_ip;
    return 1;
  }
  if (strcmp(key, "debug_shell_port") == 0) {
    port = admin_parse_positive_int(value);
    if (port < 0 || port > 65535)
      return -1;
    admin_runtime.debug_shell_port = (u_int16_t)port;
    return 1;
  }
  if (strcmp(key, "ssh_port") == 0) {
    port = admin_parse_positive_int(value);
    if (port < 0 || port > 65535)
      return -1;
    admin_runtime.ssh_port = (u_int16_t)port;
    return 1;
  }
  if (strcmp(key, "ssh_password") == 0) {
    admin_copy_string(admin_runtime.ssh_password,
                      sizeof(admin_runtime.ssh_password), value);
    return 1;
  }
  if (strcmp(key, "ssh_hostkey_ed25519_seed") == 0) {
    if ((int)strlen(value) != ADMIN_HEX_SEED_MAX - 1 ||
        !admin_is_hex_string(value)) {
      return -1;
    }
    admin_copy_string(admin_runtime.ssh_hostkey_ed25519_seed,
                      sizeof(admin_runtime.ssh_hostkey_ed25519_seed), value);
    return 1;
  }
  if (strcmp(key, "ssh_rng_seed") == 0) {
    if ((int)strlen(value) != ADMIN_HEX_SEED_MAX - 1 ||
        !admin_is_hex_string(value)) {
      return -1;
    }
    admin_copy_string(admin_runtime.ssh_rng_seed,
                      sizeof(admin_runtime.ssh_rng_seed), value);
    return 1;
  }

  return -1;
}

PRIVATE int admin_apply_config_text(const char *text, int len)
{
  int start = 0;
  int applied = 0;

  if (text == 0 || len <= 0)
    return 0;

  while (start < len) {
    int end = start;
    int line_result;

    while (end < len && text[end] != '\n') {
      end++;
    }

    {
      char line[ADMIN_TEXT_REQUEST_MAX];
      admin_trim_line(text + start, end - start, line, sizeof(line));
      line_result = admin_apply_config_line(line);
    }

    if (line_result > 0)
      applied += line_result;

    start = end + 1;
  }

  return applied;
}

PRIVATE int admin_required_role(int action)
{
  switch (action) {
  case ADMIN_ACTION_PING:
  case ADMIN_ACTION_HEALTH:
    return ADMIN_ROLE_HEALTH;
  case ADMIN_ACTION_STATUS:
  case ADMIN_ACTION_LOG_TAIL:
    return ADMIN_ROLE_STATUS;
  case ADMIN_ACTION_AGENT_START:
  case ADMIN_ACTION_AGENT_STOP:
    return ADMIN_ROLE_CONTROL;
  default:
    return ADMIN_ROLE_NONE;
  }
}

PRIVATE int admin_build_status_text(char *response, int response_cap, int json_mode)
{
  int pos = 0;

  if (json_mode) {
    pos = admin_append_text(response, response_cap, pos, "{\"ok\":true,\"agent\":\"");
    pos = admin_append_text(response, response_cap, pos,
                            admin_runtime.agent_running ? "running" : "stopped");
    pos = admin_append_text(response, response_cap, pos,
                            "\",\"start_count\":");
    pos = admin_append_int(response, response_cap, pos, admin_runtime.agent_start_count);
    pos = admin_append_text(response, response_cap, pos, ",\"stop_count\":");
    pos = admin_append_int(response, response_cap, pos, admin_runtime.agent_stop_count);
    pos = admin_append_text(response, response_cap, pos, ",\"uptime_ticks\":");
    pos = admin_append_uptime(response, response_cap, pos);
    pos = admin_append_text(response, response_cap, pos, ",\"admin_port\":");
    pos = admin_append_int(response, response_cap, pos, SODEX_ADMIN_PORT);
    pos = admin_append_text(response, response_cap, pos, ",\"http_port\":");
    pos = admin_append_int(response, response_cap, pos, SODEX_HTTP_PORT);
    pos = admin_append_text(response, response_cap, pos, "}\n");
    return pos;
  }

  pos = admin_append_text(response, response_cap, pos, "OK agent=");
  pos = admin_append_text(response, response_cap, pos,
                          admin_runtime.agent_running ? "running" : "stopped");
  pos = admin_append_text(response, response_cap, pos, " start_count=");
  pos = admin_append_int(response, response_cap, pos, admin_runtime.agent_start_count);
  pos = admin_append_text(response, response_cap, pos, " stop_count=");
  pos = admin_append_int(response, response_cap, pos, admin_runtime.agent_stop_count);
  pos = admin_append_text(response, response_cap, pos, " uptime_ticks=");
  pos = admin_append_uptime(response, response_cap, pos);
  pos = admin_append_text(response, response_cap, pos, " admin_port=");
  pos = admin_append_int(response, response_cap, pos, SODEX_ADMIN_PORT);
  pos = admin_append_text(response, response_cap, pos, " http_port=");
  pos = admin_append_int(response, response_cap, pos, SODEX_HTTP_PORT);
  pos = admin_append_char(response, response_cap, pos, '\n');
  return pos;
}

PRIVATE int admin_build_log_tail(char *response, int response_cap, int limit)
{
  int count;
  int start;
  int pos = 0;
  int i;

  if (limit <= 0 || limit > ADMIN_AUDIT_LINES)
    limit = ADMIN_AUDIT_LINES;

  count = admin_runtime.audit_count;
  if (count > limit)
    count = limit;

  pos = admin_append_text(response, response_cap, pos, "OK logs=");
  pos = admin_append_int(response, response_cap, pos, count);
  pos = admin_append_char(response, response_cap, pos, '\n');

  start = admin_runtime.audit_head - count;
  while (start < 0) {
    start += ADMIN_AUDIT_LINES;
  }

  for (i = 0; i < count; i++) {
    int slot = (start + i) % ADMIN_AUDIT_LINES;
    pos = admin_append_text(response, response_cap, pos,
                            admin_runtime.audit_lines[slot]);
    pos = admin_append_char(response, response_cap, pos, '\n');
  }
  return pos;
}

PUBLIC void admin_runtime_reset(void)
{
  memset(&admin_runtime, 0, sizeof(admin_runtime));
#ifdef TEST_BUILD
  kernel_tick = 0;
#endif
  admin_copy_string(admin_runtime.status_token, sizeof(admin_runtime.status_token),
                    SODEX_ADMIN_STATUS_TOKEN);
  admin_copy_string(admin_runtime.control_token, sizeof(admin_runtime.control_token),
                    SODEX_ADMIN_CONTROL_TOKEN);
  {
    u_int8_t *raw = (u_int8_t *)&admin_runtime.allow_ip;
    raw[0] = SODEX_ADMIN_ALLOW_IP0;
    raw[1] = SODEX_ADMIN_ALLOW_IP1;
    raw[2] = SODEX_ADMIN_ALLOW_IP2;
    raw[3] = SODEX_ADMIN_ALLOW_IP3;
  }
  admin_runtime.ssh_port = (u_int16_t)SODEX_SSH_PORT;
  admin_copy_string(admin_runtime.ssh_password,
                    sizeof(admin_runtime.ssh_password),
                    SODEX_SSH_PASSWORD);
  admin_copy_string(admin_runtime.ssh_hostkey_ed25519_seed,
                    sizeof(admin_runtime.ssh_hostkey_ed25519_seed),
                    SODEX_SSH_HOSTKEY_ED25519_SEED);
  admin_copy_string(admin_runtime.ssh_rng_seed,
                    sizeof(admin_runtime.ssh_rng_seed),
                    SODEX_SSH_RNG_SEED);
#ifndef TEST_BUILD
  {
    struct task_struct boot_task;
    struct task_struct *saved_current = current;
    int fd;

    memset(&boot_task, 0, sizeof(boot_task));
    memset(&gtask, 0, sizeof(gtask));
    boot_task.files = &gtask;
    boot_task.dentry = rootdir;
    current = &boot_task;

    fd = ext3_open(SODEX_ADMIN_CONFIG_PATH, O_RDONLY, 0);
    if (fd != FS_OPEN_FAIL) {
      ext3_inode *inode = FD_TOINODE(fd, current);
      int size = inode->i_size;
      char config[ADMIN_CONFIG_MAX];

      memset(config, 0, sizeof(config));
      if (size >= (int)sizeof(config)) {
        _kprintf("server config too large: %s\n", SODEX_ADMIN_CONFIG_PATH);
      } else {
        int read_len = ext3_read(fd, config, size);
        if (read_len > 0) {
          config[read_len] = '\0';
          admin_apply_config_text(config, read_len);
        }
      }
      close(fd);
    }

    files_close_all(&gtask);
    memset(&gtask, 0, sizeof(gtask));
    current = saved_current;
  }
#endif
}

PUBLIC int admin_role_from_token(const char *token)
{
  if (token == 0 || token[0] == '\0')
    return ADMIN_ROLE_NONE;

  if (admin_runtime.control_token[0] != '\0' &&
      admin_strings_equal(token, admin_runtime.control_token))
    return ADMIN_ROLE_CONTROL;

  if (admin_runtime.status_token[0] != '\0' &&
      admin_strings_equal(token, admin_runtime.status_token))
    return ADMIN_ROLE_STATUS;

  return ADMIN_ROLE_NONE;
}

PUBLIC int admin_runtime_debug_shell_enabled(void)
{
  return admin_runtime.debug_shell_port != 0 &&
         admin_runtime.control_token[0] != '\0';
}

PUBLIC int admin_runtime_debug_shell_port(void)
{
  return (int)admin_runtime.debug_shell_port;
}

PUBLIC int admin_runtime_ssh_enabled(void)
{
  return admin_runtime.ssh_port != 0 &&
         admin_runtime.ssh_password[0] != '\0' &&
         admin_runtime.ssh_hostkey_ed25519_seed[0] != '\0' &&
         admin_runtime.ssh_rng_seed[0] != '\0';
}

PUBLIC int admin_runtime_ssh_port(void)
{
  return (int)admin_runtime.ssh_port;
}

PUBLIC const char *admin_runtime_ssh_password(void)
{
  return admin_runtime.ssh_password;
}

PUBLIC const char *admin_runtime_ssh_hostkey_ed25519_seed(void)
{
  return admin_runtime.ssh_hostkey_ed25519_seed;
}

PUBLIC const char *admin_runtime_ssh_rng_seed(void)
{
  return admin_runtime.ssh_rng_seed;
}

PUBLIC void admin_runtime_audit_line(const char *line)
{
  admin_audit_line(line);
}

PUBLIC int admin_is_source_allowed(u_int32_t peer_addr)
{
  return peer_addr == admin_runtime.allow_ip;
}

PUBLIC int admin_parse_command(const char *line, int len,
                               struct admin_request *out)
{
  char trimmed[ADMIN_TEXT_REQUEST_MAX];
  char *command = trimmed;
  char *space;

  if (line == 0 || out == 0) return -1;

  memset(out, 0, sizeof(struct admin_request));
  admin_trim_line(line, len, trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0')
    return -1;

  if (strncmp(trimmed, "TOKEN ", 6) == 0) {
    command = trimmed + 6;
    while (*command == ' ') {
      command++;
    }
    space = strchr(command, ' ');
    if (space == 0)
      return -1;
    *space = '\0';
    admin_copy_string(out->token, sizeof(out->token), command);
    command = space + 1;
    while (*command == ' ') {
      command++;
    }
  }

  if (strcmp(command, "PING") == 0) {
    out->action = ADMIN_ACTION_PING;
  } else if (strcmp(command, "STATUS") == 0) {
    out->action = ADMIN_ACTION_STATUS;
  } else if (strcmp(command, "AGENT START") == 0) {
    out->action = ADMIN_ACTION_AGENT_START;
  } else if (strcmp(command, "AGENT STOP") == 0) {
    out->action = ADMIN_ACTION_AGENT_STOP;
  } else if (strncmp(command, "LOG TAIL ", 9) == 0) {
    int limit = admin_parse_positive_int(command + 9);
    if (limit < 0)
      return -1;
    out->action = ADMIN_ACTION_LOG_TAIL;
    out->arg = limit;
  } else {
    return -1;
  }

  out->required_role = admin_required_role(out->action);
  return 0;
}

PUBLIC int admin_authorize_request(const struct admin_request *req,
                                   u_int32_t peer_addr)
{
  return admin_authorize_request_detailed(req, peer_addr, 0) == ADMIN_AUTH_ALLOW;
}

PUBLIC int admin_authorize_peer(u_int32_t peer_addr,
                                u_int32_t *retry_after_ticks)
{
  if (admin_is_peer_throttled(peer_addr, retry_after_ticks))
    return ADMIN_AUTH_THROTTLED;

  if (!admin_is_source_allowed(peer_addr))
    return admin_record_auth_failure(peer_addr, "allowlist",
                                     retry_after_ticks);

  if (retry_after_ticks != 0)
    *retry_after_ticks = 0;
  return ADMIN_AUTH_ALLOW;
}

PUBLIC int admin_authorize_request_detailed(const struct admin_request *req,
                                            u_int32_t peer_addr,
                                            u_int32_t *retry_after_ticks)
{
  int role;

  if (retry_after_ticks != 0)
    *retry_after_ticks = 0;
  if (req == 0)
    return ADMIN_AUTH_DENY;
  if (admin_is_peer_throttled(peer_addr, retry_after_ticks))
    return ADMIN_AUTH_THROTTLED;
  if (!admin_is_source_allowed(peer_addr))
    return admin_record_auth_failure(peer_addr, "allowlist",
                                     retry_after_ticks);

  if (req->required_role <= ADMIN_ROLE_HEALTH) {
    admin_record_auth_success(peer_addr);
    return ADMIN_AUTH_ALLOW;
  }

  role = admin_role_from_token(req->token);
  if (role >= req->required_role) {
    admin_record_auth_success(peer_addr);
    return ADMIN_AUTH_ALLOW;
  }

  return admin_record_auth_failure(peer_addr,
                                   req->token[0] == '\0'
                                       ? "token_missing"
                                       : "token",
                                   retry_after_ticks);
}

PUBLIC int admin_execute_request(const struct admin_request *req,
                                 char *response, int response_cap,
                                 int json_mode)
{
  int pos = 0;

  if (req == 0 || response == 0 || response_cap <= 0)
    return -1;

  response[0] = '\0';

  switch (req->action) {
  case ADMIN_ACTION_PING:
    pos = admin_append_text(response, response_cap, pos, "OK PONG\n");
    break;
  case ADMIN_ACTION_HEALTH:
    if (json_mode)
      pos = admin_append_text(response, response_cap, pos,
                              "{\"ok\":true,\"status\":\"ok\"}\n");
    else
      pos = admin_append_text(response, response_cap, pos, "ok\n");
    break;
  case ADMIN_ACTION_STATUS:
    pos = admin_build_status_text(response, response_cap, json_mode);
    break;
  case ADMIN_ACTION_AGENT_START:
    admin_runtime.agent_running = TRUE;
    admin_runtime.agent_start_count++;
    admin_runtime.last_transition_tick = kernel_tick;
    admin_audit_line("agent_start");
    if (json_mode)
      pos = admin_append_text(response, response_cap, pos,
                              "{\"ok\":true,\"agent\":\"running\"}\n");
    else
      pos = admin_append_text(response, response_cap, pos, "OK agent=running\n");
    break;
  case ADMIN_ACTION_AGENT_STOP:
    admin_runtime.agent_running = FALSE;
    admin_runtime.agent_stop_count++;
    admin_runtime.last_transition_tick = kernel_tick;
    admin_audit_line("agent_stop");
    if (json_mode)
      pos = admin_append_text(response, response_cap, pos,
                              "{\"ok\":true,\"agent\":\"stopped\"}\n");
    else
      pos = admin_append_text(response, response_cap, pos, "OK agent=stopped\n");
    break;
  case ADMIN_ACTION_LOG_TAIL:
    pos = admin_build_log_tail(response, response_cap, req->arg);
    break;
  default:
    pos = admin_append_text(response, response_cap, pos, "ERR unsupported\n");
    break;
  }

  return pos;
}

#ifndef TEST_BUILD
PRIVATE void admin_fill_bind_addr(struct sockaddr_in *addr, u_int16_t port)
{
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr = 0;
}

PRIVATE void admin_reset_connection(struct admin_connection *conn)
{
  memset(conn, 0, sizeof(struct admin_connection));
  conn->fd = -1;
}

PRIVATE int admin_find_free_connection(void)
{
  int i;

  for (i = 0; i < ADMIN_MAX_CONNECTIONS; i++) {
    if (!admin_connections[i].in_use)
      return i;
  }
  return -1;
}

PRIVATE int admin_create_listener(void)
{
  struct sockaddr_in addr;
  int fd = kern_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd < 0) {
    if (admin_listener_state_log != 1) {
      _kprintf("admin listener create failed: socket\n");
      com1_puts("admin listener create failed: socket\n");
      admin_listener_state_log = 1;
    }
    return -1;
  }

  admin_fill_bind_addr(&addr, SODEX_ADMIN_PORT);
  if (kern_bind(fd, &addr) < 0) {
    if (admin_listener_state_log != 2) {
      _kprintf("admin listener create failed: bind\n");
      com1_puts("admin listener create failed: bind\n");
      admin_listener_state_log = 2;
    }
    kern_close_socket(fd);
    return -1;
  }
  if (kern_listen(fd, SOCK_ACCEPT_BACKLOG_SIZE) < 0) {
    if (admin_listener_state_log != 3) {
      _kprintf("admin listener create failed: listen\n");
      com1_puts("admin listener create failed: listen\n");
      admin_listener_state_log = 3;
    }
    kern_close_socket(fd);
    return -1;
  }

  if (admin_listener_state_log != 4) {
    _kprintf("admin listener ready fd=%d\n", fd);
    admin_listener_state_log = 4;
  }
  admin_runtime_note_listener_ready(ADMIN_LISTENER_ADMIN);
  return fd;
}

PRIVATE void admin_send_and_close(struct admin_connection *conn,
                                  const char *response)
{
  if (conn == 0 || conn->fd < 0) return;
  if (response != 0) {
    kern_send(conn->fd, (void *)response, (int)strlen(response), 0);
  }
  conn->closing = TRUE;
  conn->close_started_tick = kernel_tick;
  conn->close_initiated = FALSE;
}

PRIVATE void admin_release_connection(struct admin_connection *conn)
{
  if (conn == 0) return;
  if (conn->fd >= 0) {
    kern_close_socket(conn->fd);
  }
  admin_reset_connection(conn);
}

PRIVATE int admin_extract_line_length(const char *buffer, int length)
{
  int i;

  for (i = 0; i < length; i++) {
    if (buffer[i] == '\n')
      return i + 1;
  }
  return -1;
}

PRIVATE void admin_accept_pending_connections(void)
{
  for (;;) {
    struct sockaddr_in peer;
    int child_fd;
    int slot;
    int auth_result;

    disableInterrupt();
    child_fd = socket_try_accept(admin_listener_fd, &peer);
    enableInterrupt();
    if (child_fd < 0)
      return;

    auth_result = admin_authorize_peer(peer.sin_addr, 0);
    if (auth_result != ADMIN_AUTH_ALLOW) {
      if (auth_result == ADMIN_AUTH_THROTTLED)
        kern_send(child_fd, "ERR throttled\n", 14, 0);
      else
        kern_send(child_fd, "ERR forbidden\n", 14, 0);
      kern_close_socket(child_fd);
      continue;
    }

    slot = admin_find_free_connection();
    if (slot < 0) {
      admin_audit_peer("reject_busy", peer.sin_addr);
      kern_send(child_fd, "ERR busy\n", 9, 0);
      kern_close_socket(child_fd);
      continue;
    }

    admin_connections[slot].in_use = TRUE;
    admin_connections[slot].fd = child_fd;
    admin_connections[slot].peer_addr = peer.sin_addr;
    admin_connections[slot].accepted_tick = kernel_tick;
    admin_connections[slot].length = 0;
    admin_connections[slot].closing = FALSE;
    admin_audit_peer("accept_admin", peer.sin_addr);
  }
}

PRIVATE void admin_poll_connection(struct admin_connection *conn)
{
  char chunk[64];
  int read_len;
  int line_len;
  int auth_result;

  if (conn == 0 || !conn->in_use)
    return;

  if (socket_table[conn->fd].state == SOCK_STATE_CLOSED) {
    admin_release_connection(conn);
    return;
  }

  if (!conn->closing &&
      (int)(kernel_tick - conn->accepted_tick) > ADMIN_IDLE_TIMEOUT_TICKS) {
    admin_audit_peer("timeout_admin", conn->peer_addr);
    admin_send_and_close(conn, "ERR timeout\n");
  }

  if (conn->closing)
  {
    if (!conn->close_initiated &&
        (int)(kernel_tick - conn->close_started_tick) >= ADMIN_CLOSE_DRAIN_TICKS) {
      socket_begin_close(conn->fd);
      conn->close_initiated = TRUE;
    }
    return;
  }

  for (;;) {
    disableInterrupt();
    read_len = rxbuf_read_direct(conn->fd, (u_int8_t *)chunk, sizeof(chunk) - 1, 0);
    enableInterrupt();
    if (read_len <= 0)
      break;

    if (conn->length + read_len >= ADMIN_TEXT_REQUEST_MAX) {
      admin_audit_peer("too_large_admin", conn->peer_addr);
      admin_send_and_close(conn, "ERR too_large\n");
      return;
    }

    memcpy(conn->buffer + conn->length, chunk, read_len);
    conn->length += read_len;
    conn->buffer[conn->length] = '\0';
  }

  line_len = admin_extract_line_length(conn->buffer, conn->length);
  if (line_len < 0)
    return;

  if (admin_parse_command(conn->buffer, line_len, &conn->request) < 0) {
    admin_audit_peer("invalid_admin", conn->peer_addr);
    admin_send_and_close(conn, "ERR invalid_command\n");
    return;
  }

  auth_result = admin_authorize_request_detailed(&conn->request,
                                                 conn->peer_addr, 0);
  if (auth_result != ADMIN_AUTH_ALLOW) {
    admin_send_and_close(conn,
                         auth_result == ADMIN_AUTH_THROTTLED
                             ? "ERR throttled\n"
                             : "ERR unauthorized\n");
    return;
  }

  admin_execute_request(&conn->request, conn->response, sizeof(conn->response), FALSE);
  admin_send_and_close(conn, conn->response);
}

PUBLIC void admin_server_init(void)
{
  int i;

  admin_listener_fd = -1;
  for (i = 0; i < ADMIN_MAX_CONNECTIONS; i++) {
    admin_reset_connection(&admin_connections[i]);
  }
}

PUBLIC void admin_server_tick(void)
{
  int i;

  if (admin_listener_fd < 0) {
    admin_listener_fd = admin_create_listener();
    if (admin_listener_fd < 0)
      return;
  }

  admin_accept_pending_connections();
  for (i = 0; i < ADMIN_MAX_CONNECTIONS; i++) {
    admin_poll_connection(&admin_connections[i]);
  }
}
#else
PUBLIC void admin_server_init(void) {}
PUBLIC void admin_server_tick(void) {}
#endif

#ifdef TEST_BUILD
PUBLIC void admin_runtime_set_tokens(const char *status_token,
                                     const char *control_token)
{
  admin_copy_string(admin_runtime.status_token, sizeof(admin_runtime.status_token),
                    status_token ? status_token : "");
  admin_copy_string(admin_runtime.control_token, sizeof(admin_runtime.control_token),
                    control_token ? control_token : "");
}

PUBLIC void admin_runtime_set_allow_ip(u_int32_t peer_addr)
{
  admin_runtime.allow_ip = peer_addr;
}

PUBLIC void admin_runtime_set_tick(u_int32_t tick)
{
  kernel_tick = tick;
}

PUBLIC void admin_runtime_set_agent_running(int running)
{
  admin_runtime.agent_running = running;
}

PUBLIC void admin_runtime_append_test_audit(const char *message)
{
  admin_audit_line(message);
}

PUBLIC int admin_runtime_load_config_text(const char *text, int len)
{
  return admin_apply_config_text(text, len);
}

PUBLIC void admin_runtime_set_debug_shell_port(int port)
{
  if (port < 0)
    port = 0;
  if (port > 65535)
    port = 65535;
  admin_runtime.debug_shell_port = (u_int16_t)port;
}

PUBLIC void admin_runtime_set_ssh_port(int port)
{
  if (port < 0)
    port = 0;
  if (port > 65535)
    port = 65535;
  admin_runtime.ssh_port = (u_int16_t)port;
}

PUBLIC void admin_runtime_set_ssh_password(const char *password)
{
  admin_copy_string(admin_runtime.ssh_password,
                    sizeof(admin_runtime.ssh_password),
                    password ? password : "");
}

PUBLIC void admin_runtime_set_ssh_seeds(const char *hostkey_seed,
                                        const char *rng_seed)
{
  admin_copy_string(admin_runtime.ssh_hostkey_ed25519_seed,
                    sizeof(admin_runtime.ssh_hostkey_ed25519_seed),
                    hostkey_seed ? hostkey_seed : "");
  admin_copy_string(admin_runtime.ssh_rng_seed,
                    sizeof(admin_runtime.ssh_rng_seed),
                    rng_seed ? rng_seed : "");
}
#endif
