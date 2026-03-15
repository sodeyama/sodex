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
#else
#include <string.h>
#include <io.h>
#include <uip.h>
#include <socket.h>
EXTERN volatile u_int32_t kernel_tick;
#endif

#include <admin_server.h>

#define HTTP_MAX_CONNECTIONS 2
#define HTTP_IDLE_TIMEOUT_TICKS 500

PRIVATE int http_append_char(char *buf, int cap, int pos, char c)
{
  if (pos < 0 || pos >= cap - 1) return pos;
  buf[pos++] = c;
  buf[pos] = '\0';
  return pos;
}

PRIVATE int http_append_text(char *buf, int cap, int pos, const char *text)
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

PRIVATE int http_append_int(char *buf, int cap, int pos, int value)
{
  char digits[16];
  int len = 0;
  unsigned int current;

  if (value < 0) {
    pos = http_append_char(buf, cap, pos, '-');
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
    pos = http_append_char(buf, cap, pos, digits[--len]);
  }
  return pos;
}

PRIVATE void http_copy_string(char *dest, int cap, const char *src)
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

PRIVATE void http_trim_copy(char *dest, int cap, const char *src, int len)
{
  int start = 0;
  int end = len;
  int pos = 0;

  while (start < len && (src[start] == ' ' || src[start] == '\t')) {
    start++;
  }
  while (end > start &&
         (src[end - 1] == '\r' || src[end - 1] == '\n' ||
          src[end - 1] == ' ' || src[end - 1] == '\t')) {
    end--;
  }
  while (start < end && pos < cap - 1) {
    dest[pos++] = src[start++];
  }
  dest[pos] = '\0';
}

PRIVATE const char *http_status_text(int status_code)
{
  switch (status_code) {
  case 200:
    return "OK";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 413:
    return "Payload Too Large";
  default:
    return "Internal Server Error";
  }
}

PUBLIC int http_build_response(int status_code, const char *body,
                               char *response, int response_cap,
                               const char *content_type)
{
  int pos = 0;
  int body_len = body ? (int)strlen(body) : 0;

  if (response == 0 || response_cap <= 0)
    return -1;
  response[0] = '\0';

  pos = http_append_text(response, response_cap, pos, "HTTP/1.1 ");
  pos = http_append_int(response, response_cap, pos, status_code);
  pos = http_append_char(response, response_cap, pos, ' ');
  pos = http_append_text(response, response_cap, pos, http_status_text(status_code));
  pos = http_append_text(response, response_cap, pos, "\r\nContent-Type: ");
  pos = http_append_text(response, response_cap, pos,
                         content_type ? content_type : "text/plain");
  pos = http_append_text(response, response_cap, pos, "\r\nContent-Length: ");
  pos = http_append_int(response, response_cap, pos, body_len);
  pos = http_append_text(response, response_cap, pos,
                         "\r\nConnection: close\r\n\r\n");
  if (body_len > 0) {
    pos = http_append_text(response, response_cap, pos, body);
  }
  return pos;
}

PUBLIC int http_parse_request(const char *data, int len,
                              struct http_request *out)
{
  int line_start = 0;
  int line_end = -1;
  int i;
  char line[128];

  if (data == 0 || out == 0 || len <= 0)
    return -1;

  memset(out, 0, sizeof(struct http_request));

  for (i = 0; i < len; i++) {
    if (data[i] == '\n') {
      line_end = i;
      break;
    }
  }
  if (line_end < 0)
    return -1;

  http_trim_copy(line, sizeof(line), data, line_end + 1);
  {
    char *method_end = strchr(line, ' ');
    char *path_start;
    char *path_end;

    if (method_end == 0)
      return -1;
    *method_end = '\0';
    path_start = method_end + 1;
    while (*path_start == ' ') {
      path_start++;
    }
    path_end = strchr(path_start, ' ');
    if (path_end == 0)
      return -1;
    *path_end = '\0';

    http_copy_string(out->method, sizeof(out->method), line);
    http_copy_string(out->path, sizeof(out->path), path_start);
  }

  line_start = line_end + 1;
  while (line_start < len) {
    line_end = line_start;
    while (line_end < len && data[line_end] != '\n') {
      line_end++;
    }

    http_trim_copy(line, sizeof(line), data + line_start, line_end - line_start + 1);
    if (line[0] == '\0')
      return 0;

    if (strncmp(line, "Authorization:", 14) == 0) {
      char *value = line + 14;
      while (*value == ' ') {
        value++;
      }
      if (strncmp(value, "Bearer ", 7) == 0) {
        http_copy_string(out->token, sizeof(out->token), value + 7);
      }
    }

    line_start = line_end + 1;
  }

  return 0;
}

PUBLIC int http_map_request(const struct http_request *req,
                            struct admin_request *out)
{
  if (req == 0 || out == 0)
    return -1;

  memset(out, 0, sizeof(struct admin_request));
  http_copy_string(out->token, sizeof(out->token), req->token);

  if (strcmp(req->method, "GET") == 0 &&
      strcmp(req->path, "/healthz") == 0) {
    out->action = ADMIN_ACTION_HEALTH;
  } else if (strcmp(req->method, "GET") == 0 &&
             strcmp(req->path, "/status") == 0) {
    out->action = ADMIN_ACTION_STATUS;
  } else if (strcmp(req->method, "POST") == 0 &&
             strcmp(req->path, "/agent/start") == 0) {
    out->action = ADMIN_ACTION_AGENT_START;
  } else if (strcmp(req->method, "POST") == 0 &&
             strcmp(req->path, "/agent/stop") == 0) {
    out->action = ADMIN_ACTION_AGENT_STOP;
  } else {
    return -1;
  }

  out->required_role =
      (out->action == ADMIN_ACTION_HEALTH) ? ADMIN_ROLE_HEALTH :
      (out->action == ADMIN_ACTION_STATUS) ? ADMIN_ROLE_STATUS :
      ADMIN_ROLE_CONTROL;
  return 0;
}

#ifndef TEST_BUILD
struct http_connection {
  int in_use;
  int fd;
  int closing;
  u_int32_t peer_addr;
  u_int32_t accepted_tick;
  int length;
  char buffer[ADMIN_HTTP_REQUEST_MAX];
};

PRIVATE int http_listener_fd = -1;
PRIVATE struct http_connection http_connections[HTTP_MAX_CONNECTIONS];

PRIVATE void http_fill_bind_addr(struct sockaddr_in *addr, u_int16_t port)
{
  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  addr->sin_addr = 0;
}

PRIVATE void http_reset_connection(struct http_connection *conn)
{
  memset(conn, 0, sizeof(struct http_connection));
  conn->fd = -1;
}

PRIVATE int http_find_free_connection(void)
{
  int i;

  for (i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
    if (!http_connections[i].in_use)
      return i;
  }
  return -1;
}

PRIVATE int http_headers_complete(const char *buffer, int length)
{
  int i;

  for (i = 0; i + 3 < length; i++) {
    if (buffer[i] == '\r' && buffer[i + 1] == '\n' &&
        buffer[i + 2] == '\r' && buffer[i + 3] == '\n') {
      return TRUE;
    }
  }
  for (i = 0; i + 1 < length; i++) {
    if (buffer[i] == '\n' && buffer[i + 1] == '\n')
      return TRUE;
  }
  return FALSE;
}

PRIVATE int http_create_listener(void)
{
  struct sockaddr_in addr;
  int fd = kern_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd < 0)
    return -1;

  http_fill_bind_addr(&addr, SODEX_HTTP_PORT);
  if (kern_bind(fd, &addr) < 0) {
    kern_close_socket(fd);
    return -1;
  }
  if (kern_listen(fd, SOCK_ACCEPT_BACKLOG_SIZE) < 0) {
    kern_close_socket(fd);
    return -1;
  }
  return fd;
}

PRIVATE void http_send_and_close(struct http_connection *conn,
                                 const char *response)
{
  if (conn == 0 || conn->fd < 0) return;
  if (response != 0) {
    kern_send(conn->fd, (void *)response, (int)strlen(response), 0);
  }
  socket_begin_close(conn->fd);
  conn->closing = TRUE;
}

PRIVATE void http_release_connection(struct http_connection *conn)
{
  if (conn == 0) return;
  if (conn->fd >= 0) {
    kern_close_socket(conn->fd);
  }
  http_reset_connection(conn);
}

PRIVATE void http_accept_pending_connections(void)
{
  for (;;) {
    struct sockaddr_in peer;
    int child_fd;
    int slot;

    disableInterrupt();
    child_fd = socket_try_accept(http_listener_fd, &peer);
    enableInterrupt();
    if (child_fd < 0)
      return;

    if (!admin_is_source_allowed(peer.sin_addr)) {
      char body[64];
      char response[ADMIN_RESPONSE_MAX];
      http_copy_string(body, sizeof(body), "forbidden\n");
      http_build_response(403, body, response, sizeof(response), "text/plain");
      kern_send(child_fd, response, (int)strlen(response), 0);
      kern_close_socket(child_fd);
      continue;
    }

    slot = http_find_free_connection();
    if (slot < 0) {
      char response[ADMIN_RESPONSE_MAX];
      http_build_response(403, "busy\n", response, sizeof(response), "text/plain");
      kern_send(child_fd, response, (int)strlen(response), 0);
      kern_close_socket(child_fd);
      continue;
    }

    http_connections[slot].in_use = TRUE;
    http_connections[slot].fd = child_fd;
    http_connections[slot].peer_addr = peer.sin_addr;
    http_connections[slot].accepted_tick = kernel_tick;
    http_connections[slot].length = 0;
    http_connections[slot].closing = FALSE;
  }
}

PRIVATE void http_poll_connection(struct http_connection *conn)
{
  char chunk[96];
  int read_len;
  struct http_request http_req;
  struct admin_request admin_req;
  char body[ADMIN_RESPONSE_MAX];
  char response[ADMIN_RESPONSE_MAX];
  int status_code = 200;
  const char *content_type = "application/json";

  if (conn == 0 || !conn->in_use)
    return;

  if (socket_table[conn->fd].state == SOCK_STATE_CLOSED) {
    http_release_connection(conn);
    return;
  }

  if (!conn->closing &&
      (int)(kernel_tick - conn->accepted_tick) > HTTP_IDLE_TIMEOUT_TICKS) {
    http_build_response(403, "timeout\n", response, sizeof(response), "text/plain");
    http_send_and_close(conn, response);
  }

  if (conn->closing)
    return;

  for (;;) {
    disableInterrupt();
    read_len = rxbuf_read_direct(conn->fd, (u_int8_t *)chunk, sizeof(chunk) - 1, 0);
    enableInterrupt();
    if (read_len <= 0)
      break;

    if (conn->length + read_len >= ADMIN_HTTP_REQUEST_MAX) {
      http_build_response(413, "too_large\n", response, sizeof(response), "text/plain");
      http_send_and_close(conn, response);
      return;
    }

    memcpy(conn->buffer + conn->length, chunk, read_len);
    conn->length += read_len;
    conn->buffer[conn->length] = '\0';
  }

  if (!http_headers_complete(conn->buffer, conn->length))
    return;

  if (http_parse_request(conn->buffer, conn->length, &http_req) < 0 ||
      http_map_request(&http_req, &admin_req) < 0) {
    http_build_response(400, "bad_request\n", response, sizeof(response), "text/plain");
    http_send_and_close(conn, response);
    return;
  }

  if (!admin_authorize_request(&admin_req, conn->peer_addr)) {
    http_build_response(403, "forbidden\n", response, sizeof(response), "text/plain");
    http_send_and_close(conn, response);
    return;
  }

  if (admin_req.action == ADMIN_ACTION_HEALTH) {
    content_type = "text/plain";
  }

  admin_execute_request(&admin_req, body, sizeof(body),
                        admin_req.action == ADMIN_ACTION_HEALTH ? FALSE : TRUE);
  if (admin_req.action == ADMIN_ACTION_HEALTH) {
    status_code = 200;
  }
  http_build_response(status_code, body, response, sizeof(response), content_type);
  http_send_and_close(conn, response);
}

PUBLIC void http_server_init(void)
{
  int i;

  http_listener_fd = -1;
  for (i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
    http_reset_connection(&http_connections[i]);
  }
}

PUBLIC void http_server_tick(void)
{
  int i;

  if (http_listener_fd < 0) {
    http_listener_fd = http_create_listener();
    if (http_listener_fd < 0)
      return;
  }

  http_accept_pending_connections();
  for (i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
    http_poll_connection(&http_connections[i]);
  }
}
#else
PUBLIC void http_server_init(void) {}
PUBLIC void http_server_tick(void) {}
#endif
