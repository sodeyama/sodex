#include <sx_runtime.h>
#include <json.h>
#include <string.h>

#ifdef TEST_BUILD
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#define sx_guest_debug_printf(...) ((void)0)
#else
#include <arpa/inet.h>
#include <stdlib.h>
#include <debug.h>
#include <dns.h>
#include <fs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sleep.h>
#include <sys/socket.h>

extern u_int32_t get_kernel_tick(void);
#define sx_guest_debug_printf(...) debug_printf(__VA_ARGS__)
#endif

#define SX_DUMMY_PID 1
#define SX_DUMMY_FD_BASE 1000

enum sx_flow_kind {
  SX_FLOW_NEXT = 0,
  SX_FLOW_RETURN = 1,
  SX_FLOW_BREAK = 2,
  SX_FLOW_CONTINUE = 3
};

#ifndef TEST_BUILD
struct sx_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};
#endif

static int sx_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void sx_copy_runtime_limits(struct sx_runtime_limits *dst,
                                   const struct sx_runtime_limits *src)
{
  if (dst == 0 || src == 0)
    return;
  dst->max_bindings = src->max_bindings;
  dst->max_scope_depth = src->max_scope_depth;
  dst->max_call_depth = src->max_call_depth;
  dst->max_loop_iterations = src->max_loop_iterations;
}

static int sx_call_stack_contains(const struct sx_runtime *runtime,
                                  const char *name)
{
  int i;

  if (runtime == 0 || name == 0 || name[0] == '\0')
    return 0;
  for (i = 0; i < runtime->call_depth && i < SX_MAX_CALL_DEPTH; i++) {
    if (strcmp(runtime->call_stack[i], name) == 0)
      return 1;
  }
  return 0;
}

static int sx_format_i32(char *buf, int cap, int value)
{
  char temp[16];
  unsigned int magnitude;
  int len = 0;
  int pos = 0;

  if (buf == 0 || cap <= 1)
    return -1;
  if (value < 0)
    magnitude = (unsigned int)(-value);
  else
    magnitude = (unsigned int)value;
  do {
    temp[len++] = (char)('0' + (magnitude % 10));
    magnitude /= 10;
  } while (magnitude > 0 && len < (int)sizeof(temp));
  if (value < 0)
    temp[len++] = '-';
  if (len >= cap)
    return -1;
  while (len > 0)
    buf[pos++] = temp[--len];
  buf[pos] = '\0';
  return pos;
}

static void sx_set_string_value(struct sx_value *value, const char *text)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_STRING;
  value->data_len = 0;
  value->bool_value = 0;
  value->int_value = 0;
  if (sx_copy_text(value->text, sizeof(value->text), text) < 0)
    value->text[0] = '\0';
  value->data_len = (int)strlen(value->text);
}

static int sx_default_output(void *ctx, const char *text, int len)
{
  int written;

  (void)ctx;
  written = (int)write(STDOUT_FILENO, text, (size_t)len);
  return written == len ? 0 : -1;
}

static void sx_set_unit_value(struct sx_value *value)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_NONE;
  value->text[0] = '\0';
  value->data_len = 0;
  value->bool_value = 0;
  value->int_value = 0;
}

static void sx_set_bool_value(struct sx_value *value, int bool_value)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_BOOL;
  value->bool_value = bool_value != 0;
  value->int_value = value->bool_value;
  sx_copy_text(value->text, sizeof(value->text),
               bool_value != 0 ? "true" : "false");
  value->data_len = (int)strlen(value->text);
}

static void sx_set_i32_value(struct sx_value *value, int int_value)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_I32;
  value->bool_value = int_value != 0;
  value->int_value = int_value;
  if (sx_format_i32(value->text, sizeof(value->text), int_value) < 0)
    value->text[0] = '\0';
  value->data_len = (int)strlen(value->text);
}

static void sx_set_bytes_value(struct sx_value *value,
                               const char *data, int len)
{
  if (value == 0)
    return;
  if (len < 0)
    len = 0;
  if (len >= SX_TEXT_MAX)
    len = SX_TEXT_MAX - 1;
  value->kind = SX_VALUE_BYTES;
  value->bool_value = len > 0;
  value->int_value = len;
  value->data_len = len;
  if (len > 0 && data != 0)
    memcpy(value->text, data, (size_t)len);
  value->text[len] = '\0';
}

static void sx_set_handle_value(struct sx_value *value,
                                enum sx_value_kind kind,
                                int handle,
                                const char *label)
{
  int len = 0;

  if (value == 0 || label == 0)
    return;
  value->kind = kind;
  value->bool_value = 1;
  value->int_value = handle;
  if (sx_copy_text(value->text, sizeof(value->text), label) < 0)
    value->text[0] = '\0';
  len = (int)strlen(value->text);
  if (len < SX_TEXT_MAX - 2) {
    value->text[len++] = '#';
    value->text[len] = '\0';
    if (sx_format_i32(value->text + len, SX_TEXT_MAX - len, handle) > 0)
      len = (int)strlen(value->text);
  }
  value->data_len = len;
}

static void sx_set_list_value(struct sx_value *value, int handle)
{
  sx_set_handle_value(value, SX_VALUE_LIST, handle, "<list");
}

static void sx_set_map_value(struct sx_value *value, int handle)
{
  sx_set_handle_value(value, SX_VALUE_MAP, handle, "<map");
}

static void sx_set_result_value(struct sx_value *value, int handle)
{
  sx_set_handle_value(value, SX_VALUE_RESULT, handle, "<result");
}

static void sx_set_dummy_value_for_type(struct sx_value *value,
                                        const char *type_name)
{
  if (value == 0)
    return;
  if (type_name == 0 || type_name[0] == '\0') {
    sx_set_unit_value(value);
    return;
  }
  if (strcmp(type_name, "str") == 0) {
    sx_set_string_value(value, "");
    return;
  }
  if (strcmp(type_name, "bool") == 0) {
    sx_set_bool_value(value, 0);
    return;
  }
  if (strcmp(type_name, "i32") == 0) {
    sx_set_i32_value(value, 0);
    return;
  }
  sx_set_unit_value(value);
}

static int sx_format_value_text(const struct sx_value *value,
                                char *buf, int cap)
{
  if (buf == 0 || cap <= 0 || value == 0)
    return -1;
  if (value->kind == SX_VALUE_BYTES) {
    if (value->data_len == 0)
      return sx_copy_text(buf, cap, "");
    if (value->data_len >= cap)
      return -1;
    memcpy(buf, value->text, (size_t)value->data_len);
    buf[value->data_len] = '\0';
    return 0;
  }
  return sx_copy_text(buf, cap, value->text);
}

static void sx_close_pipe_handle(struct sx_pipe_handle *pipe_handle)
{
  if (pipe_handle == 0 || pipe_handle->active == 0)
    return;
  if (pipe_handle->read_fd >= 0)
    close(pipe_handle->read_fd);
  if (pipe_handle->write_fd >= 0 &&
      pipe_handle->write_fd != pipe_handle->read_fd)
    close(pipe_handle->write_fd);
  pipe_handle->active = 0;
  pipe_handle->read_fd = -1;
  pipe_handle->write_fd = -1;
}

static int sx_alloc_pipe_handle(struct sx_runtime *runtime,
                                int read_fd, int write_fd)
{
  int i;

  if (runtime == 0)
    return -1;
  for (i = 0; i < SX_MAX_PIPE_HANDLES; i++) {
    if (runtime->pipes[i].active != 0)
      continue;
    runtime->pipes[i].active = 1;
    runtime->pipes[i].read_fd = read_fd;
    runtime->pipes[i].write_fd = write_fd;
    return i;
  }
  return -1;
}

static struct sx_pipe_handle *sx_get_pipe_handle(struct sx_runtime *runtime,
                                                 int handle)
{
  if (runtime == 0 || handle < 0 || handle >= SX_MAX_PIPE_HANDLES)
    return 0;
  if (runtime->pipes[handle].active == 0)
    return 0;
  return &runtime->pipes[handle];
}

static int sx_close_socket_fd(int fd)
{
  if (fd < 0)
    return 0;
#ifdef TEST_BUILD
  return close(fd);
#else
  return closesocket(fd);
#endif
}

static int sx_track_socket_fd(struct sx_runtime *runtime, int fd)
{
  int i;

  if (runtime == 0 || fd < 0)
    return -1;
  for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
    if (runtime->sockets[i].active != 0 &&
        runtime->sockets[i].fd == fd)
      return 0;
  }
  for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
    if (runtime->sockets[i].active != 0)
      continue;
    runtime->sockets[i].active = 1;
    runtime->sockets[i].fd = fd;
    return 0;
  }
  return -1;
}

static void sx_detach_socket_fd(struct sx_runtime *runtime, int fd)
{
  int i;

  if (runtime == 0 || fd < 0)
    return;
  for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
    if (runtime->sockets[i].active == 0 ||
        runtime->sockets[i].fd != fd)
      continue;
    runtime->sockets[i].active = 0;
    runtime->sockets[i].fd = -1;
  }
}

static int sx_is_tracked_socket_fd(const struct sx_runtime *runtime, int fd)
{
  int i;

  if (runtime == 0 || fd < 0)
    return 0;
  for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
    if (runtime->sockets[i].active != 0 &&
        runtime->sockets[i].fd == fd)
      return 1;
  }
  return 0;
}

static int sx_socket_poll_internal(int fd, int events, int timeout_ticks)
{
  struct pollfd poll_fd;
  int ready;

  memset(&poll_fd, 0, sizeof(poll_fd));
  poll_fd.fd = fd;
  poll_fd.events = (short)events;
  ready = poll(&poll_fd, 1, timeout_ticks);
  if (ready <= 0)
    return ready;
  return (poll_fd.revents & events) != 0 || (poll_fd.revents & POLLHUP) != 0;
}

static int sx_poll_socket_readable(int fd, int timeout_ticks)
{
  if (fd < 0)
    return -1;
  return sx_socket_poll_internal(fd, POLLIN, timeout_ticks);
}

static int sx_socket_send_once(int fd, const char *buf, int len)
{
  if (fd < 0 || buf == 0 || len < 0)
    return -1;
#ifdef TEST_BUILD
  return (int)send(fd, buf, (size_t)len, 0);
#else
  return send_msg(fd, buf, len, 0);
#endif
}

static int sx_socket_recv_once(int fd, char *buf, int len)
{
  if (fd < 0 || buf == 0 || len <= 0)
    return -1;
#ifdef TEST_BUILD
  return (int)recv(fd, buf, (size_t)len, 0);
#else
  return recv_msg(fd, buf, len, 0);
#endif
}

static int sx_socket_write_all(int fd, const char *buf, int len)
{
  int written = 0;

  if (fd < 0 || buf == 0 || len < 0)
    return -1;
  while (written < len) {
    int nr = sx_socket_send_once(fd, buf + written, len - written);

    if (nr <= 0)
      return -1;
    written += nr;
  }
  return written;
}

static int sx_socket_read_text(int fd, char *buf, int cap)
{
  int len = 0;

  if (fd < 0 || buf == 0 || cap <= 1)
    return -1;
  while (len < cap - 1) {
    int nr = sx_socket_recv_once(fd, buf + len, cap - len - 1);

    if (nr < 0)
      return -1;
    if (nr == 0)
      break;
    len += nr;
    if (len >= cap - 1 || sx_poll_socket_readable(fd, 0) <= 0)
      break;
  }
  buf[len] = '\0';
  return len;
}

static int sx_parse_host_ipv4(const char *host, struct in_addr *addr)
{
  if (host == 0 || addr == 0)
    return -1;
#ifdef TEST_BUILD
  if (strcmp(host, "localhost") == 0)
    host = "127.0.0.1";
  return inet_aton(host, addr) != 0 ? 0 : -1;
#else
  if (inet_aton(host, addr) != 0)
    return 0;
  {
    struct dns_result resolved;

    if (dns_resolve(host, &resolved) < 0)
      return -1;
    addr->s_addr = (u_int32_t)resolved.addr[0] |
                   ((u_int32_t)resolved.addr[1] << 8) |
                   ((u_int32_t)resolved.addr[2] << 16) |
                   ((u_int32_t)resolved.addr[3] << 24);
  }
  return 0;
#endif
}

static int sx_build_sockaddr(const char *host, int port,
                             struct sockaddr_in *addr)
{
  if (addr == 0 || port < 0 || port > 65535)
    return -1;
  memset(addr, 0, sizeof(*addr));
  addr->sin_family = AF_INET;
  addr->sin_port = htons((u_int16_t)port);
  if (host == 0)
    return 0;
  return sx_parse_host_ipv4(host, &addr->sin_addr);
}

static int sx_alloc_dummy_socket_fd(struct sx_runtime *runtime)
{
  (void)runtime;
  return SX_DUMMY_FD_BASE + 100;
}

static int sx_alloc_list_handle(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return -1;
  for (i = 0; i < SX_MAX_LIST_HANDLES; i++) {
    if (runtime->lists[i].active != 0)
      continue;
    memset(&runtime->lists[i], 0, sizeof(runtime->lists[i]));
    runtime->lists[i].active = 1;
    return i;
  }
  return -1;
}

static struct sx_list_handle *sx_get_list_handle(struct sx_runtime *runtime,
                                                 int handle)
{
  if (runtime == 0 || handle < 0 || handle >= SX_MAX_LIST_HANDLES)
    return 0;
  if (runtime->lists[handle].active == 0)
    return 0;
  return &runtime->lists[handle];
}

static int sx_alloc_map_handle(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return -1;
  for (i = 0; i < SX_MAX_MAP_HANDLES; i++) {
    if (runtime->maps[i].active != 0)
      continue;
    memset(&runtime->maps[i], 0, sizeof(runtime->maps[i]));
    runtime->maps[i].active = 1;
    return i;
  }
  return -1;
}

static struct sx_map_handle *sx_get_map_handle(struct sx_runtime *runtime,
                                               int handle)
{
  if (runtime == 0 || handle < 0 || handle >= SX_MAX_MAP_HANDLES)
    return 0;
  if (runtime->maps[handle].active == 0)
    return 0;
  return &runtime->maps[handle];
}

static int sx_alloc_result_handle(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return -1;
  for (i = 0; i < SX_MAX_RESULT_HANDLES; i++) {
    if (runtime->results[i].active != 0)
      continue;
    memset(&runtime->results[i], 0, sizeof(runtime->results[i]));
    runtime->results[i].active = 1;
    return i;
  }
  return -1;
}

static struct sx_result_handle *sx_get_result_handle(struct sx_runtime *runtime,
                                                     int handle)
{
  if (runtime == 0 || handle < 0 || handle >= SX_MAX_RESULT_HANDLES)
    return 0;
  if (runtime->results[handle].active == 0)
    return 0;
  return &runtime->results[handle];
}

static void sx_detach_pipe_fd(struct sx_runtime *runtime, int fd)
{
  int i;

  if (runtime == 0 || fd < 0)
    return;
  for (i = 0; i < SX_MAX_PIPE_HANDLES; i++) {
    struct sx_pipe_handle *pipe_handle = &runtime->pipes[i];

    if (pipe_handle->active == 0)
      continue;
    if (pipe_handle->read_fd == fd)
      pipe_handle->read_fd = -1;
    if (pipe_handle->write_fd == fd)
      pipe_handle->write_fd = -1;
    if (pipe_handle->read_fd < 0 && pipe_handle->write_fd < 0)
      pipe_handle->active = 0;
  }
}

static int sx_read_line_from_fd(int fd, char *buf, int cap)
{
  int len = 0;

  if (fd < 0 || buf == 0 || cap <= 1)
    return -1;
  while (len < cap - 1) {
    char ch;
    int nr = (int)read(fd, &ch, 1);

    if (nr < 0)
      return -1;
    if (nr == 0)
      break;
    if (ch == '\r')
      continue;
    if (ch == '\n')
      break;
    buf[len++] = ch;
  }
  buf[len] = '\0';
  return len;
}

static int sx_write_all_text(int fd, const char *text)
{
  int len;
  int written = 0;

  if (fd < 0 || text == 0)
    return -1;
  len = (int)strlen(text);
  while (written < len) {
    int nr = (int)write(fd, text + written, (size_t)(len - written));

    if (nr <= 0)
      return -1;
    written += nr;
  }
  return written;
}

#ifdef TEST_BUILD
static int sx_get_current_dir(char *buf, int cap)
{
  if (buf == 0 || cap <= 1)
    return -1;
  if (getcwd(buf, (size_t)cap) == 0)
    return -1;
  return 0;
}

static int sx_path_is_dir(const char *path)
{
  struct stat st;

  if (path == 0)
    return 0;
  if (stat(path, &st) < 0)
    return 0;
  return S_ISDIR(st.st_mode) != 0;
}

static int sx_now_ticks(void)
{
  struct timeval tv;

  if (gettimeofday(&tv, 0) < 0)
    return -1;
  return (int)(tv.tv_sec * 100 + tv.tv_usec / 10000);
}

static int sx_sleep_for_ticks(int ticks)
{
  if (ticks < 0)
    return -1;
  usleep((useconds_t)ticks * 10000);
  return 0;
}
#else
static int sx_append_path_component(char *buf, int cap, const char *name)
{
  int len;
  int name_len;

  if (buf == 0 || name == 0 || cap <= 1)
    return -1;
  len = (int)strlen(buf);
  name_len = (int)strlen(name);
  if (strcmp(buf, "/") != 0) {
    if (len >= cap - 1)
      return -1;
    buf[len++] = '/';
    buf[len] = '\0';
  }
  if (len + name_len >= cap)
    return -1;
  memcpy(buf + len, name, (size_t)name_len);
  buf[len + name_len] = '\0';
  return 0;
}

static int sx_build_dentry_path(const ext3_dentry *dentry, char *buf, int cap)
{
  char parent[SX_TEXT_MAX];

  if (dentry == 0 || buf == 0 || cap <= 1)
    return -1;
  if (dentry->d_parent == 0 || dentry->d_name == 0 ||
      strcmp(dentry->d_name, "/") == 0)
    return sx_copy_text(buf, cap, "/");
  if (sx_build_dentry_path(dentry->d_parent, parent, sizeof(parent)) < 0)
    return -1;
  if (sx_copy_text(buf, cap, parent) < 0)
    return -1;
  return sx_append_path_component(buf, cap, dentry->d_name);
}

static int sx_get_current_dir(char *buf, int cap)
{
  ext3_dentry *dentry;

  if (buf == 0 || cap <= 1)
    return -1;
  dentry = getdentry();
  if (dentry == 0)
    return -1;
  return sx_build_dentry_path(dentry, buf, cap);
}

static int sx_path_is_dir(const char *path)
{
  char cwd[SX_TEXT_MAX];

  if (path == 0)
    return 0;
  if (sx_get_current_dir(cwd, sizeof(cwd)) < 0)
    return 0;
  if (chdir((char *)path) < 0)
    return 0;
  chdir(cwd);
  return 1;
}

static int sx_now_ticks(void)
{
  return (int)get_kernel_tick();
}

static int sx_sleep_for_ticks(int ticks)
{
  if (ticks < 0)
    return -1;
  sleep_ticks((u_int32_t)ticks);
  return 0;
}
#endif

static const char *sx_env_get(const char *name)
{
  const char *value;

  if (name == 0 || name[0] == '\0')
    return 0;
  value = getenv(name);
  if (value == 0 || value[0] == '\0')
    return 0;
  return value;
}

static int sx_find_binding(const struct sx_runtime *runtime, const char *name)
{
  int i;

  for (i = runtime->binding_count - 1; i >= 0; i--) {
    if (strcmp(runtime->bindings[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int sx_find_binding_in_current_scope(const struct sx_runtime *runtime,
                                            const char *name)
{
  int i;

  for (i = runtime->binding_count - 1; i >= 0; i--) {
    if (runtime->bindings[i].scope_depth != runtime->scope_depth)
      continue;
    if (strcmp(runtime->bindings[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int sx_find_function(const struct sx_program *program, const char *name)
{
  int i;

  for (i = 0; i < program->function_count; i++) {
    if (strcmp(program->functions[i].name, name) == 0)
      return i;
  }
  return -1;
}

static void sx_snapshot_error_stack(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return;
  if (runtime->error_call_depth >= runtime->call_depth)
    return;
  runtime->error_call_depth = runtime->call_depth;
  for (i = 0; i < runtime->call_depth && i < SX_MAX_CALL_DEPTH; i++) {
    sx_copy_text(runtime->error_call_stack[i],
                 sizeof(runtime->error_call_stack[0]),
                 runtime->call_stack[i]);
  }
}

static int sx_value_to_bool(const struct sx_value *value,
                            const struct sx_source_span *span,
                            struct sx_diagnostic *diag)
{
  if (value->kind == SX_VALUE_BOOL)
    return value->bool_value;
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected bool value");
  return -1;
}

static int sx_value_to_string(const struct sx_value *value,
                              const struct sx_source_span *span,
                              struct sx_diagnostic *diag)
{
  if (value->kind == SX_VALUE_STRING)
    return 0;
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected string value");
  return -1;
}

static int sx_value_to_bytes(const struct sx_value *value,
                             const struct sx_source_span *span,
                             struct sx_diagnostic *diag)
{
  if (value->kind == SX_VALUE_BYTES)
    return 0;
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected bytes value");
  return -1;
}

static int sx_value_to_i32(const struct sx_value *value,
                           const struct sx_source_span *span,
                           struct sx_diagnostic *diag,
                           int *out_value)
{
  if (value->kind == SX_VALUE_I32) {
    if (out_value != 0)
      *out_value = value->int_value;
    return 0;
  }
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected i32 value");
  return -1;
}

static int sx_value_to_list_handle(const struct sx_value *value,
                                   const struct sx_source_span *span,
                                   struct sx_diagnostic *diag,
                                   int *out_handle)
{
  if (value->kind == SX_VALUE_LIST) {
    if (out_handle != 0)
      *out_handle = value->int_value;
    return 0;
  }
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected list value");
  return -1;
}

static int sx_value_to_map_handle(const struct sx_value *value,
                                  const struct sx_source_span *span,
                                  struct sx_diagnostic *diag,
                                  int *out_handle)
{
  if (value->kind == SX_VALUE_MAP) {
    if (out_handle != 0)
      *out_handle = value->int_value;
    return 0;
  }
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected map value");
  return -1;
}

static int sx_value_to_result_handle(const struct sx_value *value,
                                     const struct sx_source_span *span,
                                     struct sx_diagnostic *diag,
                                     int *out_handle)
{
  if (value->kind == SX_VALUE_RESULT) {
    if (out_handle != 0)
      *out_handle = value->int_value;
    return 0;
  }
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected result value");
  return -1;
}

static int sx_read_text_file(const char *path, char *buf, int cap)
{
  int fd;
  int len = 0;

  if (path == 0 || buf == 0 || cap <= 1)
    return -1;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  while (len < cap - 1) {
    int nr = (int)read(fd, buf + len, (size_t)(cap - len - 1));

    if (nr < 0) {
      close(fd);
      return -1;
    }
    if (nr == 0)
      break;
    len += nr;
  }
  buf[len] = '\0';
  close(fd);
  return len;
}

static int sx_write_text_file(const char *path, const char *text, int append)
{
  int fd;
  int flags;
  int len;
  int written = 0;

  if (path == 0 || text == 0)
    return -1;
  flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  fd = open(path, flags, 0644);
  if (fd < 0)
    return -1;
  len = (int)strlen(text);
  while (written < len) {
    int nr = (int)write(fd, text + written, (size_t)(len - written));

    if (nr <= 0) {
      close(fd);
      return -1;
    }
    written += nr;
  }
  close(fd);
  return 0;
}

static int sx_read_bytes_file(const char *path, char *buf, int cap)
{
  int fd;
  int len = 0;

  if (path == 0 || buf == 0 || cap <= 0)
    return -1;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  while (len < cap) {
    int nr = (int)read(fd, buf + len, (size_t)(cap - len));

    if (nr < 0) {
      close(fd);
      return -1;
    }
    if (nr == 0)
      break;
    len += nr;
  }
  close(fd);
  return len;
}

static int sx_write_bytes_file(const char *path, const char *data, int len)
{
  int fd;
  int written = 0;

  if (path == 0 || data == 0 || len < 0)
    return -1;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return -1;
  while (written < len) {
    int nr = (int)write(fd, data + written, (size_t)(len - written));

    if (nr <= 0) {
      close(fd);
      return -1;
    }
    written += nr;
  }
  close(fd);
  return written;
}

static int sx_path_exists(const char *path)
{
  int fd;

  if (path == 0)
    return 0;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static int sx_list_dir_text(const char *path, char *buf, int cap)
{
  int len = 0;

  if (path == 0 || buf == 0 || cap <= 1)
    return -1;
#ifdef TEST_BUILD
  {
    DIR *dirp;
    struct dirent *de;

    dirp = opendir(path);
    if (dirp == 0)
      return -1;

    while ((de = readdir(dirp)) != 0) {
      int name_len;
      int i;

      if ((strcmp(de->d_name, ".") == 0) ||
          (strcmp(de->d_name, "..") == 0))
        continue;
      name_len = (int)strlen(de->d_name);
      if (len > 0) {
        if (len >= cap - 1) {
          closedir(dirp);
          return -1;
        }
        buf[len++] = '\n';
      }
      if (len + name_len >= cap) {
        closedir(dirp);
        return -1;
      }
      for (i = 0; i < name_len; i++)
        buf[len++] = de->d_name[i];
    }
    buf[len] = '\0';
    closedir(dirp);
    return len;
  }
#else
  {
    int fd;
    int bytes_read;
    char dir_buf[4096];

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
      return -1;
    while ((bytes_read = (int)read(fd, dir_buf, sizeof(dir_buf))) > 0) {
      int offset = 0;

      while (offset < bytes_read) {
        struct sx_dir_entry *de = (struct sx_dir_entry *)(dir_buf + offset);
        int name_len;
        int i;

        if (de->rec_len < 8 || offset + de->rec_len > bytes_read)
          break;
        if (de->inode == 0 || de->name_len == 0) {
          offset += de->rec_len;
          continue;
        }
        name_len = (int)de->name_len;
        if ((name_len == 1 && de->name[0] == '.') ||
            (name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
          offset += de->rec_len;
          continue;
        }
        if (len > 0) {
          if (len >= cap - 1) {
            close(fd);
            return -1;
          }
          buf[len++] = '\n';
        }
        if (len + name_len >= cap) {
          close(fd);
          return -1;
        }
        for (i = 0; i < name_len; i++)
          buf[len++] = de->name[i];
        offset += de->rec_len;
      }
    }
    close(fd);
    if (bytes_read < 0)
      return -1;
    buf[len] = '\0';
    return len;
  }
#endif
}

static int sx_text_contains(const char *text, const char *needle)
{
  int i;
  int j;

  if (text == 0 || needle == 0)
    return 0;
  if (needle[0] == '\0')
    return 1;
  for (i = 0; text[i] != '\0'; i++) {
    for (j = 0; needle[j] != '\0'; j++) {
      if (text[i + j] == '\0' || text[i + j] != needle[j])
        break;
    }
    if (needle[j] == '\0')
      return 1;
  }
  return 0;
}

static int sx_trim_text(const char *text, char *buf, int cap)
{
  int start = 0;
  int end;
  int len;
  int i;

  if (text == 0 || buf == 0 || cap <= 0)
    return -1;
  end = (int)strlen(text);
  while (text[start] != '\0' && sx_is_space(text[start]) != 0)
    start++;
  while (end > start && sx_is_space(text[end - 1]) != 0)
    end--;
  len = end - start;
  if (len >= cap)
    return -1;
  for (i = 0; i < len; i++)
    buf[i] = text[start + i];
  buf[len] = '\0';
  return len;
}

static int sx_concat_text(const char *lhs, const char *rhs, char *buf, int cap)
{
  int len = 0;
  int i;

  if (lhs == 0 || rhs == 0 || buf == 0 || cap <= 0)
    return -1;
  for (i = 0; lhs[i] != '\0'; i++) {
    if (len >= cap - 1)
      return -1;
    buf[len++] = lhs[i];
  }
  for (i = 0; rhs[i] != '\0'; i++) {
    if (len >= cap - 1)
      return -1;
    buf[len++] = rhs[i];
  }
  buf[len] = '\0';
  return len;
}

static int sx_parse_json(const char *text,
                         struct json_token *tokens,
                         int *token_count)
{
  struct json_parser parser;
  int status;

  if (text == 0 || tokens == 0 || token_count == 0)
    return -1;
  json_init(&parser);
  status = json_parse(&parser, text, (int)strlen(text), tokens, JSON_MAX_TOKENS);
  if (status < 0)
    return status;
  *token_count = status;
  return 0;
}

static int sx_json_find_value(const char *text,
                              const struct json_token *tokens,
                              int token_count,
                              const char *key)
{
  if (token_count <= 0 || tokens[0].type != JSON_OBJECT)
    return -1;
  return json_find_key(text, tokens, token_count, 0, key);
}

static int sx_capture_process_output(int fd, char *buf, int cap)
{
  int len = 0;

  if (fd < 0 || buf == 0 || cap <= 1)
    return -1;
  while (len < cap - 1) {
    int nr = (int)read(fd, buf + len, (size_t)(cap - len - 1));

    if (nr < 0)
      return -1;
    if (nr == 0)
      break;
    len += nr;
  }
  buf[len] = '\0';
  return len;
}

static int sx_wait_for_pid(pid_t pid, int *status_out)
{
  int status = 0;

  if (pid < 0)
    return -1;
  if (waitpid(pid, &status, 0) < 0)
    return -1;
#ifdef TEST_BUILD
  if (WIFEXITED(status))
    status = WEXITSTATUS(status);
#endif
  if (status_out != 0)
    *status_out = status;
  return 0;
}

static int sx_capture_child_output(int read_fd,
                                   pid_t pid,
                                   char *buf,
                                   int cap,
                                   int *status_out)
{
  struct pollfd pfd;
  int len = 0;
  int status = 0;
  int child_done = 0;

  if (read_fd < 0 || pid < 0 || buf == 0 || cap <= 1)
    return -1;

  memset(&pfd, 0, sizeof(pfd));
  pfd.fd = read_fd;
  pfd.events = POLLIN;

  while (child_done == 0) {
    int ret;

    pfd.revents = 0;
    ret = poll(&pfd, 1, 1);
    if (ret > 0 && (pfd.revents & POLLIN) != 0) {
      char discard[256];
      char *dst = discard;
      int remain = (int)sizeof(discard);
      int nr;

      if (len < cap - 1) {
        dst = buf + len;
        remain = cap - len - 1;
      }
      nr = (int)read(read_fd, dst, (size_t)remain);
      if (nr < 0)
        return -1;
      if (nr > 0 && len < cap - 1) {
        if (nr > cap - len - 1)
          nr = cap - len - 1;
        len += nr;
      }
    }

    if (waitpid(pid, &status, WNOHANG) > 0)
      child_done = 1;
  }

  for (;;) {
    int ret;

    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    if (ret <= 0 || (pfd.revents & POLLIN) == 0)
      break;
    if (len < cap - 1) {
      int nr = (int)read(read_fd, buf + len, (size_t)(cap - len - 1));

      if (nr < 0)
        return -1;
      if (nr == 0)
        break;
      len += nr;
    } else {
      char discard[256];
      int nr = (int)read(read_fd, discard, sizeof(discard));

      if (nr < 0)
        return -1;
      if (nr == 0)
        break;
    }
  }

#ifdef TEST_BUILD
  if (WIFEXITED(status))
    status = WEXITSTATUS(status);
#endif
  buf[len] = '\0';
  if (status_out != 0)
    *status_out = status;
  return len;
}

static int sx_build_exec_argv(const struct sx_value *args,
                              int arg_count,
                              int extra_start,
                              char *argv[],
                              int argv_cap)
{
  int out_count = 0;
  int i;

  if (args == 0 || argv == 0 || arg_count <= 0 || argv_cap <= 1)
    return -1;
  argv[out_count++] = (char *)args[0].text;
  for (i = extra_start; i < arg_count; i++) {
    if (out_count >= argv_cap - 1)
      return -1;
    argv[out_count++] = (char *)args[i].text;
  }
  argv[out_count] = 0;
  return out_count;
}

#ifdef TEST_BUILD
static void sx_host_close_runtime_pipe_fds(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return;
  for (i = 0; i < SX_MAX_PIPE_HANDLES; i++) {
    if (runtime->pipes[i].active == 0)
      continue;
    if (runtime->pipes[i].read_fd >= STDERR_FILENO + 1)
      close(runtime->pipes[i].read_fd);
    if (runtime->pipes[i].write_fd >= STDERR_FILENO + 1)
      close(runtime->pipes[i].write_fd);
  }
}

static int sx_host_spawn_process(struct sx_runtime *runtime,
                                 char *argv[],
                                 int stdin_fd,
                                 int stdout_fd,
                                 int stderr_fd,
                                 pid_t *out_pid)
{
  pid_t pid;

  if (argv == 0 || argv[0] == 0 || out_pid == 0)
    return -1;
  pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    if (stdin_fd >= 0 && stdin_fd != STDIN_FILENO)
      dup2(stdin_fd, STDIN_FILENO);
    if (stdout_fd >= 0 && stdout_fd != STDOUT_FILENO)
      dup2(stdout_fd, STDOUT_FILENO);
    if (stderr_fd >= 0 && stderr_fd != STDERR_FILENO)
      dup2(stderr_fd, STDERR_FILENO);
    sx_host_close_runtime_pipe_fds(runtime);
    if (stdin_fd >= STDERR_FILENO + 1)
      close(stdin_fd);
    if (stdout_fd >= STDERR_FILENO + 1)
      close(stdout_fd);
    if (stderr_fd >= STDERR_FILENO + 1)
      close(stderr_fd);
    execv(argv[0], argv);
    _exit(127);
  }
  *out_pid = pid;
  return 0;
}
#else
static int sx_guest_save_fd_once(int target_fd, int *saved_fds)
{
  if (saved_fds == 0 || target_fd < STDIN_FILENO || target_fd > STDERR_FILENO)
    return -1;
  if (saved_fds[target_fd] >= 0)
    return 0;
  saved_fds[target_fd] = dup(target_fd);
  if (saved_fds[target_fd] < 0)
    return -1;
  return 0;
}

static int sx_guest_dup_to_target(int source_fd, int target_fd)
{
  int new_fd;

  if (source_fd == target_fd)
    return 0;
  close(target_fd);
  new_fd = dup(source_fd);
  if (new_fd != target_fd) {
    if (new_fd >= 0)
      close(new_fd);
    return -1;
  }
  return 0;
}

static int sx_guest_assign_fd(int target_fd, int source_fd, int *saved_fds)
{
  if (source_fd < 0 || source_fd == target_fd)
    return 0;
  if (sx_guest_save_fd_once(target_fd, saved_fds) < 0)
    return -1;
  return sx_guest_dup_to_target(source_fd, target_fd);
}

static int sx_guest_hide_fd(int fd,
                            const int *keep_fds,
                            int keep_count,
                            int *hidden_fds,
                            int *saved_fds,
                            int *hidden_count,
                            int hidden_cap)
{
  int i;

  if (fd < STDERR_FILENO + 1 || hidden_fds == 0 || saved_fds == 0 ||
      hidden_count == 0)
    return 0;
  for (i = 0; i < keep_count; i++) {
    if (keep_fds[i] == fd)
      return 0;
  }
  for (i = 0; i < *hidden_count; i++) {
    if (hidden_fds[i] == fd)
      return 0;
  }
  if (*hidden_count >= hidden_cap)
    return -1;
  saved_fds[*hidden_count] = dup(fd);
  if (saved_fds[*hidden_count] < 0)
    return -1;
  close(fd);
  hidden_fds[*hidden_count] = fd;
  (*hidden_count)++;
  return 0;
}

static int sx_guest_hide_runtime_fds(struct sx_runtime *runtime,
                                     int stdin_fd,
                                     int stdout_fd,
                                     int stderr_fd,
                                     int *hidden_fds,
                                     int *saved_fds,
                                     int hidden_cap,
                                     int *hidden_count_out)
{
  int i;
  int hidden_count = 0;
  int keep_fds[3];

  if (hidden_count_out != 0)
    *hidden_count_out = 0;
  if (runtime == 0)
    return 0;

  keep_fds[0] = stdin_fd;
  keep_fds[1] = stdout_fd;
  keep_fds[2] = stderr_fd;

  for (i = 0; i < SX_MAX_PIPE_HANDLES; i++) {
    if (runtime->pipes[i].active == 0)
      continue;
    if (sx_guest_hide_fd(runtime->pipes[i].read_fd, keep_fds, 3,
                         hidden_fds, saved_fds, &hidden_count,
                         hidden_cap) < 0) {
      if (hidden_count_out != 0)
        *hidden_count_out = hidden_count;
      return -1;
    }
    if (sx_guest_hide_fd(runtime->pipes[i].write_fd, keep_fds, 3,
                         hidden_fds, saved_fds, &hidden_count,
                         hidden_cap) < 0) {
      if (hidden_count_out != 0)
        *hidden_count_out = hidden_count;
      return -1;
    }
  }

  for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
    if (runtime->sockets[i].active == 0)
      continue;
    if (sx_guest_hide_fd(runtime->sockets[i].fd, keep_fds, 3,
                         hidden_fds, saved_fds, &hidden_count,
                         hidden_cap) < 0) {
      if (hidden_count_out != 0)
        *hidden_count_out = hidden_count;
      return -1;
    }
  }

  if (hidden_count_out != 0)
    *hidden_count_out = hidden_count;
  return hidden_count;
}

static void sx_guest_restore_hidden_fds(const int *hidden_fds,
                                        int *saved_fds,
                                        int hidden_count)
{
  int i;

  if (hidden_fds == 0 || saved_fds == 0)
    return;
  for (i = hidden_count - 1; i >= 0; i--) {
    close(hidden_fds[i]);
    if (dup(saved_fds[i]) != hidden_fds[i]) {
      close(saved_fds[i]);
      continue;
    }
    close(saved_fds[i]);
    saved_fds[i] = -1;
  }
}

static void sx_guest_restore_fds(int *saved_fds)
{
  int i;

  if (saved_fds == 0)
    return;
  for (i = STDIN_FILENO; i <= STDERR_FILENO; i++) {
    if (saved_fds[i] < 0)
      continue;
    close(i);
    dup(saved_fds[i]);
    close(saved_fds[i]);
    saved_fds[i] = -1;
  }
}

static int sx_guest_spawn_process(struct sx_runtime *runtime,
                                  char *argv[],
                                  int stdin_fd,
                                  int stdout_fd,
                                  int stderr_fd,
                                  pid_t *out_pid)
{
  int saved_fds[STDERR_FILENO + 1];
  int hidden_fds[SX_MAX_PIPE_HANDLES * 2 + SX_MAX_SOCKET_HANDLES];
  int hidden_saved_fds[SX_MAX_PIPE_HANDLES * 2 + SX_MAX_SOCKET_HANDLES];
  int hidden_count = 0;
  int hide_result;
  int i;
  pid_t pid;

  if (argv == 0 || argv[0] == 0 || out_pid == 0)
    return -1;

  saved_fds[STDIN_FILENO] = -1;
  saved_fds[STDOUT_FILENO] = -1;
  saved_fds[STDERR_FILENO] = -1;
  for (i = 0; i < SX_MAX_PIPE_HANDLES * 2 + SX_MAX_SOCKET_HANDLES; i++) {
    hidden_fds[i] = -1;
    hidden_saved_fds[i] = -1;
  }
  hide_result = sx_guest_hide_runtime_fds(runtime,
                                          stdin_fd, stdout_fd, stderr_fd,
                                          hidden_fds, hidden_saved_fds,
                                          SX_MAX_PIPE_HANDLES * 2 +
                                              SX_MAX_SOCKET_HANDLES,
                                          &hidden_count);
  if (hide_result < 0) {
    sx_guest_restore_hidden_fds(hidden_fds, hidden_saved_fds, hidden_count);
    return -1;
  }
  if (sx_guest_assign_fd(STDIN_FILENO, stdin_fd, saved_fds) < 0 ||
      sx_guest_assign_fd(STDOUT_FILENO, stdout_fd, saved_fds) < 0 ||
      sx_guest_assign_fd(STDERR_FILENO, stderr_fd, saved_fds) < 0) {
    sx_guest_restore_fds(saved_fds);
    sx_guest_restore_hidden_fds(hidden_fds, hidden_saved_fds, hidden_count);
    return -1;
  }
  pid = execve(argv[0], argv, 0);
  sx_guest_restore_fds(saved_fds);
  sx_guest_restore_hidden_fds(hidden_fds, hidden_saved_fds, hidden_count);
  if (pid < 0)
    return -1;
  *out_pid = pid;
  return 0;
}
#endif

static int sx_spawn_process(struct sx_runtime *runtime,
                            char *argv[],
                            int stdin_fd,
                            int stdout_fd,
                            int stderr_fd,
                            pid_t *out_pid)
{
#ifdef TEST_BUILD
  return sx_host_spawn_process(runtime, argv,
                               stdin_fd, stdout_fd, stderr_fd, out_pid);
#else
  return sx_guest_spawn_process(runtime, argv,
                                stdin_fd, stdout_fd, stderr_fd, out_pid);
#endif
}

static int sx_spawn_from_values(struct sx_runtime *runtime,
                                const struct sx_value *args,
                                int arg_count,
                                int extra_start,
                                int stdin_fd,
                                int stdout_fd,
                                int stderr_fd,
                                int execute_side_effects,
                                pid_t *out_pid)
{
  char *argv[SX_CALL_MAX_ARGS + 1];

  if (args == 0 || arg_count <= 0 || out_pid == 0)
    return -1;
  if (sx_build_exec_argv(args, arg_count, extra_start,
                         argv, SX_CALL_MAX_ARGS + 1) < 0)
    return -1;
  if (execute_side_effects == 0) {
    *out_pid = SX_DUMMY_PID;
    return 0;
  }
  return sx_spawn_process(runtime, argv,
                          stdin_fd, stdout_fd, stderr_fd, out_pid);
}

static int sx_run_process(struct sx_runtime *runtime,
                          const struct sx_value *args,
                          int arg_count,
                          int capture_output,
                          int execute_side_effects,
                          struct sx_value *value)
{
  pid_t pid;
  int status = 0;

  if (value == 0 || arg_count <= 0)
    return -1;
  if (execute_side_effects == 0) {
    if (capture_output != 0)
      sx_set_string_value(value, "");
    else
      sx_set_i32_value(value, 0);
    return 0;
  }
  if (capture_output != 0) {
    int pipefd[2];
    int capture_len;

    if (pipe(pipefd) < 0)
      return -1;
    sx_guest_debug_printf("AUDIT sx_capture_pipe_create read=%d write=%d\n",
                          pipefd[0], pipefd[1]);
    if (sx_spawn_from_values(runtime, args, arg_count, 1, -1, pipefd[1], -1,
                             1, &pid) < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      return -1;
    }
    close(pipefd[1]);
    sx_guest_debug_printf("AUDIT sx_capture_wait_begin pid=%d read=%d\n",
                          (int)pid, pipefd[0]);
    capture_len = sx_capture_child_output(pipefd[0], pid, value->text,
                                          sizeof(value->text), &status);
    if (capture_len < 0) {
      close(pipefd[0]);
      return -1;
    }
    sx_guest_debug_printf("AUDIT sx_capture_wait_done pid=%d status=%d\n",
                          (int)pid, status);
    sx_guest_debug_printf("AUDIT sx_capture_read_done pid=%d len=%d\n",
                          (int)pid, capture_len);
    close(pipefd[0]);
    value->kind = SX_VALUE_STRING;
    value->bool_value = 0;
    value->int_value = 0;
    value->data_len = capture_len;
    return 0;
  }

  if (sx_spawn_from_values(runtime, args, arg_count, 1, -1, -1, -1,
                           execute_side_effects, &pid) < 0)
    return -1;
  if (sx_wait_for_pid(pid, &status) < 0)
    return -1;
  sx_set_i32_value(value, status);
  return 0;
}

static int sx_store_result(struct sx_runtime *runtime,
                           int ok,
                           const struct sx_value *payload,
                           const char *error_text,
                           struct sx_value *value)
{
  int handle;
  struct sx_result_handle *result;

  if (runtime == 0 || value == 0)
    return -1;
  handle = sx_alloc_result_handle(runtime);
  if (handle < 0)
    return -1;
  result = &runtime->results[handle];
  result->active = 1;
  result->ok = ok != 0;
  if (payload != 0)
    result->value = *payload;
  else
    sx_set_unit_value(&result->value);
  if (sx_copy_text(result->error, sizeof(result->error),
                   error_text != 0 ? error_text : "") < 0) {
    result->active = 0;
    return -1;
  }
  sx_set_result_value(value, handle);
  return 0;
}

static int sx_enter_scope(struct sx_runtime *runtime,
                          const struct sx_source_span *span,
                          struct sx_diagnostic *diag)
{
  if (runtime->scope_depth + 1 > runtime->limits.max_scope_depth) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "scope depth limit exceeded");
    return -1;
  }
  runtime->scope_depth++;
  return 0;
}

static void sx_leave_scope(struct sx_runtime *runtime)
{
  if (runtime->scope_depth <= 0)
    return;
  while (runtime->binding_count > 0 &&
         runtime->bindings[runtime->binding_count - 1].scope_depth ==
             runtime->scope_depth)
    runtime->binding_count--;
  runtime->scope_depth--;
}

static int sx_eval_atom(struct sx_runtime *runtime,
                        const struct sx_atom *atom,
                        struct sx_value *value,
                        struct sx_diagnostic *diag)
{
  int index;

  if (atom->kind == SX_ATOM_STRING) {
    sx_set_string_value(value, atom->text);
    return 0;
  }
  if (atom->kind == SX_ATOM_BOOL) {
    sx_set_bool_value(value, atom->bool_value);
    return 0;
  }
  if (atom->kind == SX_ATOM_I32) {
    sx_set_i32_value(value, atom->int_value);
    return 0;
  }
  if (atom->kind != SX_ATOM_NAME) {
    sx_set_diagnostic(diag, atom->span.offset, atom->span.length,
                      atom->span.line, atom->span.column,
                      "unsupported atom");
    return -1;
  }

  index = sx_find_binding(runtime, atom->text);
  if (index < 0) {
    sx_set_diagnostic(diag, atom->span.offset, atom->span.length,
                      atom->span.line, atom->span.column,
                      "undefined name");
    return -1;
  }
  *value = runtime->bindings[index].value;
  return 0;
}

static int sx_validate_functions(const struct sx_program *program,
                                 struct sx_diagnostic *diag)
{
  int i;
  int j;

  for (i = 0; i < program->function_count; i++) {
    for (j = i + 1; j < program->function_count; j++) {
      if (strcmp(program->functions[i].name, program->functions[j].name) == 0) {
        sx_set_diagnostic(diag,
                          program->functions[j].span.offset,
                          program->functions[j].span.length,
                          program->functions[j].span.line,
                          program->functions[j].span.column,
                          "duplicate function");
        return -1;
      }
    }
  }
  return 0;
}

static int sx_register_binding(struct sx_runtime *runtime,
                               const char *name,
                               const struct sx_value *value,
                               const struct sx_source_span *span,
                               struct sx_diagnostic *diag)
{
  int index;

  index = sx_find_binding_in_current_scope(runtime, name);
  if (index >= 0) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "duplicate binding");
    return -1;
  }
  if (runtime->binding_count >= runtime->limits.max_bindings) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "binding table is full");
    return -1;
  }
  index = runtime->binding_count++;
  sx_copy_text(runtime->bindings[index].name,
               sizeof(runtime->bindings[index].name), name);
  runtime->bindings[index].value = *value;
  runtime->bindings[index].scope_depth = runtime->scope_depth;
  return 0;
}

static int sx_assign_binding(struct sx_runtime *runtime,
                             const char *name,
                             const struct sx_value *value,
                             const struct sx_source_span *span,
                             struct sx_diagnostic *diag)
{
  int index;

  index = sx_find_binding(runtime, name);
  if (index < 0) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "undefined name");
    return -1;
  }
  runtime->bindings[index].value = *value;
  return 0;
}

static int sx_eval_expr(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        const struct sx_expr *expr,
                        struct sx_value *value,
                        int execute_side_effects,
                        struct sx_diagnostic *diag);

static int sx_eval_expr_from_index(struct sx_runtime *runtime,
                                   const struct sx_program *program,
                                   int expr_index,
                                   struct sx_value *value,
                                   int execute_side_effects,
                                   struct sx_diagnostic *diag)
{
  if (expr_index < 0 || expr_index >= program->expr_count) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid expression");
    return -1;
  }
  return sx_eval_expr(runtime, program, &program->exprs[expr_index],
                      value, execute_side_effects, diag);
}

static int sx_eval_unary_expr(struct sx_runtime *runtime,
                              const struct sx_program *program,
                              const struct sx_unary_expr *unary,
                              const struct sx_source_span *span,
                              int execute_side_effects,
                              struct sx_value *value,
                              struct sx_diagnostic *diag)
{
  struct sx_value operand;
  int operand_bool;
  int operand_i32;

  sx_set_unit_value(&operand);
  if (sx_eval_expr_from_index(runtime, program, unary->operand_expr_index,
                              &operand, execute_side_effects, diag) < 0)
    return -1;
  if (unary->op == SX_UNARY_NOT) {
    operand_bool = sx_value_to_bool(&operand, span, diag);
    if (operand_bool < 0)
      return -1;
    sx_set_bool_value(value, operand_bool == 0);
    return 0;
  }
  if (sx_value_to_i32(&operand, span, diag, &operand_i32) < 0)
    return -1;
  sx_set_i32_value(value, -operand_i32);
  return 0;
}

static int sx_value_equals(const struct sx_value *lhs,
                           const struct sx_value *rhs)
{
  if (lhs->kind != rhs->kind)
    return 0;
  if (lhs->kind == SX_VALUE_STRING)
    return strcmp(lhs->text, rhs->text) == 0;
  if (lhs->kind == SX_VALUE_BOOL)
    return lhs->bool_value == rhs->bool_value;
  if (lhs->kind == SX_VALUE_I32)
    return lhs->int_value == rhs->int_value;
  if (lhs->kind == SX_VALUE_BYTES) {
    if (lhs->data_len != rhs->data_len)
      return 0;
    return memcmp(lhs->text, rhs->text, (size_t)lhs->data_len) == 0;
  }
  if (lhs->kind == SX_VALUE_LIST ||
      lhs->kind == SX_VALUE_MAP ||
      lhs->kind == SX_VALUE_RESULT)
    return lhs->int_value == rhs->int_value;
  return 1;
}

static int sx_eval_binary_expr(struct sx_runtime *runtime,
                               const struct sx_program *program,
                               const struct sx_binary_expr *binary,
                               const struct sx_source_span *span,
                               int execute_side_effects,
                               struct sx_value *value,
                               struct sx_diagnostic *diag)
{
  struct sx_value lhs;
  struct sx_value rhs;
  int lhs_bool;
  int rhs_bool;
  int lhs_i32;
  int rhs_i32;

  sx_set_unit_value(&lhs);
  sx_set_unit_value(&rhs);
  if (binary->op == SX_BINARY_AND || binary->op == SX_BINARY_OR) {
    if (sx_eval_expr_from_index(runtime, program, binary->left_expr_index,
                                &lhs, execute_side_effects, diag) < 0)
      return -1;
    lhs_bool = sx_value_to_bool(&lhs, span, diag);
    if (lhs_bool < 0)
      return -1;
    if (binary->op == SX_BINARY_AND && lhs_bool == 0) {
      sx_set_bool_value(value, 0);
      return 0;
    }
    if (binary->op == SX_BINARY_OR && lhs_bool != 0) {
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (sx_eval_expr_from_index(runtime, program, binary->right_expr_index,
                                &rhs, execute_side_effects, diag) < 0)
      return -1;
    rhs_bool = sx_value_to_bool(&rhs, span, diag);
    if (rhs_bool < 0)
      return -1;
    sx_set_bool_value(value, rhs_bool != 0);
    return 0;
  }

  if (sx_eval_expr_from_index(runtime, program, binary->left_expr_index,
                              &lhs, execute_side_effects, diag) < 0)
    return -1;
  if (sx_eval_expr_from_index(runtime, program, binary->right_expr_index,
                              &rhs, execute_side_effects, diag) < 0)
    return -1;

  if (binary->op == SX_BINARY_EQ || binary->op == SX_BINARY_NE) {
    if (lhs.kind != rhs.kind &&
        lhs.kind != SX_VALUE_NONE && rhs.kind != SX_VALUE_NONE) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "equality type mismatch");
      return -1;
    }
    lhs_bool = sx_value_equals(&lhs, &rhs);
    sx_set_bool_value(value,
                      binary->op == SX_BINARY_EQ ? lhs_bool : lhs_bool == 0);
    return 0;
  }

  if (sx_value_to_i32(&lhs, span, diag, &lhs_i32) < 0 ||
      sx_value_to_i32(&rhs, span, diag, &rhs_i32) < 0)
    return -1;

  if (binary->op == SX_BINARY_ADD) {
    sx_set_i32_value(value, lhs_i32 + rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_SUB) {
    sx_set_i32_value(value, lhs_i32 - rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_MUL) {
    sx_set_i32_value(value, lhs_i32 * rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_DIV) {
    if (rhs_i32 == 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "division by zero");
      return -1;
    }
    sx_set_i32_value(value, lhs_i32 / rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_MOD) {
    if (rhs_i32 == 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "modulo by zero");
      return -1;
    }
    sx_set_i32_value(value, lhs_i32 % rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_LT) {
    sx_set_bool_value(value, lhs_i32 < rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_LE) {
    sx_set_bool_value(value, lhs_i32 <= rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_GT) {
    sx_set_bool_value(value, lhs_i32 > rhs_i32);
    return 0;
  }
  if (binary->op == SX_BINARY_GE) {
    sx_set_bool_value(value, lhs_i32 >= rhs_i32);
    return 0;
  }

  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "unsupported binary operator");
  return -1;
}

static int sx_eval_list_expr(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             const struct sx_list_expr *list_expr,
                             int execute_side_effects,
                             struct sx_value *value,
                             struct sx_diagnostic *diag)
{
  struct sx_list_handle *list_handle;
  int handle;
  int i;

  if (runtime == 0 || program == 0 || list_expr == 0 || value == 0)
    return -1;
  handle = sx_alloc_list_handle(runtime);
  if (handle < 0) {
    sx_set_diagnostic(diag, 0, 0, 0, 0,
                      "list handle table is full");
    return -1;
  }
  list_handle = sx_get_list_handle(runtime, handle);
  if (list_handle == 0)
    return -1;
  for (i = 0; i < list_expr->item_count; i++) {
    struct sx_value item_value;
    int item_expr_index;

    if (list_expr->item_start_index + i >= program->list_literal_item_count) {
      sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid list literal");
      return -1;
    }
    item_expr_index = program->list_literal_items[list_expr->item_start_index + i];

    sx_set_unit_value(&item_value);
    if (sx_eval_expr_from_index(runtime, program,
                                item_expr_index,
                                &item_value, execute_side_effects, diag) < 0)
      return -1;
    if (list_handle->count >= SX_MAX_LIST_ITEMS) {
      sx_set_diagnostic(diag, 0, 0, 0, 0, "list is full");
      return -1;
    }
    list_handle->items[list_handle->count++] = item_value;
  }
  sx_set_list_value(value, handle);
  return 0;
}

static int sx_eval_map_expr(struct sx_runtime *runtime,
                            const struct sx_program *program,
                            const struct sx_map_expr *map_expr,
                            int execute_side_effects,
                            struct sx_value *value,
                            struct sx_diagnostic *diag)
{
  struct sx_map_handle *map_handle;
  int handle;
  int i;

  if (runtime == 0 || program == 0 || map_expr == 0 || value == 0)
    return -1;
  handle = sx_alloc_map_handle(runtime);
  if (handle < 0) {
    sx_set_diagnostic(diag, 0, 0, 0, 0,
                      "map handle table is full");
    return -1;
  }
  map_handle = sx_get_map_handle(runtime, handle);
  if (map_handle == 0)
    return -1;
  for (i = 0; i < map_expr->item_count; i++) {
    struct sx_value item_value;
    const struct sx_map_literal_item *item;

    if (map_expr->item_start_index + i >= program->map_literal_item_count) {
      sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid map literal");
      return -1;
    }
    item = &program->map_literal_items[map_expr->item_start_index + i];

    sx_set_unit_value(&item_value);
    if (sx_eval_expr_from_index(runtime, program,
                                item->value_expr_index,
                                &item_value, execute_side_effects, diag) < 0)
      return -1;
    if (i >= SX_MAX_MAP_ITEMS) {
      sx_set_diagnostic(diag, 0, 0, 0, 0, "map is full");
      return -1;
    }
    map_handle->entries[i].used = 1;
    sx_copy_text(map_handle->entries[i].key,
                 sizeof(map_handle->entries[i].key),
                 item->key);
    map_handle->entries[i].value = item_value;
    map_handle->count++;
  }
  sx_set_map_value(value, handle);
  return 0;
}

static int sx_run_block(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        int block_index,
                        int execute_calls,
                        int create_scope,
                        struct sx_value *value,
                        struct sx_diagnostic *diag);

static int sx_run_statement(struct sx_runtime *runtime,
                            const struct sx_program *program,
                            const struct sx_stmt *stmt,
                            int execute_calls,
                            struct sx_value *value,
                            struct sx_diagnostic *diag);

static int sx_call_user_function(struct sx_runtime *runtime,
                                 const struct sx_program *program,
                                 const struct sx_call_expr *call,
                                 const struct sx_source_span *span,
                                 int execute_side_effects,
                                 struct sx_value *value,
                                 struct sx_diagnostic *diag)
{
  struct sx_value args[SX_CALL_MAX_ARGS];
  struct sx_value result;
  const struct sx_function *fn;
  int function_index;
  int i;
  int flow;
  int entered_scope = 0;

  memset(args, 0, sizeof(args));
  sx_set_unit_value(&result);

  function_index = sx_find_function(program, call->target_name);
  if (function_index < 0) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "unknown function");
    return -1;
  }
  fn = &program->functions[function_index];
  if (call->arg_count != fn->param_count) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "function argument count mismatch");
    return -1;
  }
  if (runtime->call_depth >= runtime->limits.max_call_depth) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "call depth limit exceeded");
    return -1;
  }
  for (i = 0; i < call->arg_count; i++) {
    if (sx_eval_expr_from_index(runtime, program, call->args[i],
                                &args[i], execute_side_effects, diag) < 0)
      return -1;
  }
  if (execute_side_effects == 0 &&
      sx_call_stack_contains(runtime, fn->name) != 0) {
    sx_set_dummy_value_for_type(value, fn->return_type);
    return 0;
  }

  runtime->call_depth++;
  sx_copy_text(runtime->call_stack[runtime->call_depth - 1],
               sizeof(runtime->call_stack[0]), fn->name);
  runtime->inside_function++;
  if (sx_enter_scope(runtime, span, diag) < 0)
    goto fail;
  entered_scope = 1;
  for (i = 0; i < fn->param_count; i++) {
    if (sx_register_binding(runtime, fn->params[i], &args[i], span, diag) < 0)
      goto fail;
  }
  flow = sx_run_block(runtime, program, fn->body_block_index,
                      execute_side_effects, 0, &result, diag);
  if (flow < 0)
    goto fail;

  if (entered_scope != 0)
    sx_leave_scope(runtime);
  runtime->inside_function--;
  runtime->call_stack[runtime->call_depth - 1][0] = '\0';
  runtime->call_depth--;
  if (flow == SX_FLOW_RETURN)
    *value = result;
  else
    sx_set_unit_value(value);
  return 0;

fail:
  sx_snapshot_error_stack(runtime);
  if (entered_scope != 0)
    sx_leave_scope(runtime);
  runtime->inside_function--;
  runtime->call_stack[runtime->call_depth - 1][0] = '\0';
  runtime->call_depth--;
  return -1;
}

static int sx_eval_namespace_call(struct sx_runtime *runtime,
                                  const struct sx_program *program,
                                  const struct sx_call_expr *call,
                                  const struct sx_source_span *span,
                                  int execute_side_effects,
                                  struct sx_value *value,
                                  struct sx_diagnostic *diag)
{
  struct sx_value args[SX_CALL_MAX_ARGS];
  struct json_token tokens[JSON_MAX_TOKENS];
  int i;

  memset(args, 0, sizeof(args));
  memset(tokens, 0, sizeof(tokens));
  for (i = 0; i < call->arg_count; i++) {
    if (sx_eval_expr_from_index(runtime, program, call->args[i],
                                &args[i], execute_side_effects, diag) < 0)
      return -1;
  }

  if (strcmp(call->target_name, "io") == 0) {
    if (strcmp(call->member_name, "print") == 0 ||
        strcmp(call->member_name, "println") == 0) {
      char rendered[SX_TEXT_MAX];

      if (call->arg_count != 1) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.print/io.println expects 1 argument");
        return -1;
      }
      if (sx_format_value_text(&args[0], rendered, sizeof(rendered)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.print/io.println failed to render value");
        return -1;
      }
      if (execute_side_effects != 0 &&
          runtime->output(runtime->output_ctx, rendered,
                          (int)strlen(rendered)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "output failed");
        return -1;
      }
      if (execute_side_effects != 0 &&
          strcmp(call->member_name, "println") == 0 &&
          runtime->output(runtime->output_ctx, "\n", 1) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "output failed");
        return -1;
      }
      sx_set_unit_value(value);
      return 0;
    }
    if (strcmp(call->member_name, "read_all") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.read_all expects 0 arguments");
        return -1;
      }
      if (execute_side_effects == 0)
        sx_set_string_value(value, "");
      else if (sx_capture_process_output(STDIN_FILENO, value->text,
                                         sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.read_all failed");
        return -1;
      } else
        value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "read_line") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.read_line expects 0 arguments");
        return -1;
      }
      if (execute_side_effects == 0)
        sx_set_string_value(value, "");
      else if (sx_read_line_from_fd(STDIN_FILENO, value->text,
                                    sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.read_line failed");
        return -1;
      } else
        value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "read_fd") == 0) {
      int fd = -1;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &fd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.read_fd expects 1 i32 argument");
        return -1;
      }
      if (execute_side_effects == 0)
        sx_set_string_value(value, "");
      else {
        int read_len = 0;

        sx_guest_debug_printf("AUDIT sx_io_read_fd_begin fd=%d\r\n", fd);
        read_len = sx_capture_process_output(fd, value->text,
                                             sizeof(value->text));
        if (read_len < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "io.read_fd failed");
          return -1;
        }
        sx_guest_debug_printf("AUDIT sx_io_read_fd_done fd=%d len=%d\r\n",
                              fd, read_len);
        value->kind = SX_VALUE_STRING;
      }
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "read_fd_bytes") == 0) {
      int fd = -1;
      int len = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &fd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.read_fd_bytes expects 1 i32 argument");
        return -1;
      }
      if (execute_side_effects == 0)
        sx_set_bytes_value(value, "", 0);
      else {
        len = sx_capture_process_output(fd, value->text, sizeof(value->text));
        if (len < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "io.read_fd_bytes failed");
          return -1;
        }
        sx_set_bytes_value(value, value->text, len);
      }
      return 0;
    }
    if (strcmp(call->member_name, "write_fd") == 0) {
      int fd = -1;
      int written = 0;

      if (call->arg_count != 2 ||
          sx_value_to_i32(&args[0], span, diag, &fd) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.write_fd expects fd and string");
        return -1;
      }
      if (execute_side_effects == 0)
        written = (int)strlen(args[1].text);
      else {
        sx_guest_debug_printf("AUDIT sx_io_write_fd_begin fd=%d len=%d\r\n",
                              fd, (int)strlen(args[1].text));
        written = sx_write_all_text(fd, args[1].text);
        if (written < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "io.write_fd failed");
          return -1;
        }
        sx_guest_debug_printf("AUDIT sx_io_write_fd_done fd=%d wrote=%d\r\n",
                              fd, written);
      }
      sx_set_i32_value(value, written);
      return 0;
    }
    if (strcmp(call->member_name, "write_fd_bytes") == 0) {
      int fd = -1;
      int written = 0;

      if (call->arg_count != 2 ||
          sx_value_to_i32(&args[0], span, diag, &fd) < 0 ||
          sx_value_to_bytes(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.write_fd_bytes expects fd and bytes");
        return -1;
      }
      if (execute_side_effects == 0)
        written = args[1].data_len;
      else {
        while (written < args[1].data_len) {
          int nr = (int)write(fd, args[1].text + written,
                              (size_t)(args[1].data_len - written));

          if (nr <= 0) {
            sx_set_diagnostic(diag, span->offset, span->length,
                              span->line, span->column,
                              "io.write_fd_bytes failed");
            return -1;
          }
          written += nr;
        }
      }
      sx_set_i32_value(value, written);
      return 0;
    }
    if (strcmp(call->member_name, "close") == 0) {
      int fd = -1;
      int is_socket = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &fd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.close expects 1 i32 argument");
        return -1;
      }
      is_socket = sx_is_tracked_socket_fd(runtime, fd);
      if (execute_side_effects != 0 &&
          (is_socket != 0 ? sx_close_socket_fd(fd) : close(fd)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "io.close failed");
        return -1;
      }
      sx_detach_pipe_fd(runtime, fd);
      sx_detach_socket_fd(runtime, fd);
      sx_set_bool_value(value, 1);
      return 0;
    }
  }

  if (strcmp(call->target_name, "fs") == 0) {
    if (strcmp(call->member_name, "exists") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.exists expects 1 string argument");
        return -1;
      }
      sx_set_bool_value(value,
                        execute_side_effects != 0 ?
                          sx_path_exists(args[0].text) : 0);
      return 0;
    }
    if (strcmp(call->member_name, "read_text") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.read_text expects 1 string argument");
        return -1;
      }
      if (execute_side_effects == 0)
        value->text[0] = '\0';
      else if (sx_read_text_file(args[0].text, value->text,
                                 sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.read_text failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "read_bytes") == 0) {
      int len = 0;

      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.read_bytes expects 1 string argument");
        return -1;
      }
      if (execute_side_effects == 0)
        sx_set_bytes_value(value, "", 0);
      else {
        len = sx_read_bytes_file(args[0].text, value->text, SX_TEXT_MAX - 1);
        if (len < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "fs.read_bytes failed");
          return -1;
        }
        sx_set_bytes_value(value, value->text, len);
      }
      return 0;
    }
    if (strcmp(call->member_name, "try_read_text") == 0 ||
        strcmp(call->member_name, "try_read_bytes") == 0) {
      struct sx_value payload;

      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.try_read_* expects 1 string argument");
        return -1;
      }
      sx_set_unit_value(&payload);
      if (execute_side_effects == 0) {
        if (strcmp(call->member_name, "try_read_bytes") == 0)
          sx_set_bytes_value(&payload, "", 0);
        else
          sx_set_string_value(&payload, "");
        if (sx_store_result(runtime, 1, &payload, "", value) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "fs.try_read_* failed");
          return -1;
        }
        return 0;
      }
      if (strcmp(call->member_name, "try_read_bytes") == 0) {
        int len = sx_read_bytes_file(args[0].text, payload.text, SX_TEXT_MAX - 1);

        if (len < 0) {
          if (sx_store_result(runtime, 0, 0, "fs.read_bytes failed", value) < 0)
            goto fs_try_store_fail;
          return 0;
        }
        sx_set_bytes_value(&payload, payload.text, len);
      } else {
        if (sx_read_text_file(args[0].text, payload.text, sizeof(payload.text)) < 0) {
          if (sx_store_result(runtime, 0, 0, "fs.read_text failed", value) < 0)
            goto fs_try_store_fail;
          return 0;
        }
        payload.kind = SX_VALUE_STRING;
        payload.bool_value = 0;
        payload.int_value = 0;
        payload.data_len = (int)strlen(payload.text);
      }
      if (sx_store_result(runtime, 1, &payload, "", value) < 0) {
fs_try_store_fail:
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.try_read_* failed");
        return -1;
      }
      return 0;
    }
    if (strcmp(call->member_name, "list_dir") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.list_dir expects 1 string argument");
        return -1;
      }
      if (execute_side_effects == 0)
        value->text[0] = '\0';
      else if (sx_list_dir_text(args[0].text, value->text,
                                sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.list_dir failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "write_text") == 0 ||
        strcmp(call->member_name, "append_text") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.write_text/fs.append_text expects 2 string arguments");
        return -1;
      }
      if (execute_side_effects != 0 &&
          sx_write_text_file(args[0].text, args[1].text,
                             strcmp(call->member_name, "append_text") == 0) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.write_text/fs.append_text failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "write_bytes") == 0) {
      int written = 0;

      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_bytes(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.write_bytes expects string path and bytes");
        return -1;
      }
      if (execute_side_effects != 0) {
        written = sx_write_bytes_file(args[0].text, args[1].text,
                                      args[1].data_len);
        if (written < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "fs.write_bytes failed");
          return -1;
        }
      }
      sx_set_i32_value(value, execute_side_effects != 0 ? written : args[1].data_len);
      return 0;
    }
    if (strcmp(call->member_name, "mkdir") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.mkdir expects 1 string argument");
        return -1;
      }
      if (execute_side_effects != 0 && mkdir(args[0].text, 0755) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.mkdir failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "remove") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.remove expects 1 string argument");
        return -1;
      }
      if (execute_side_effects != 0 &&
          unlink(args[0].text) < 0 &&
          rmdir(args[0].text) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.remove failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "rename") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.rename expects 2 string arguments");
        return -1;
      }
      if (execute_side_effects != 0 &&
          rename(args[0].text, args[1].text) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.rename failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "chdir") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.chdir expects 1 string argument");
        return -1;
      }
      if (execute_side_effects != 0 && chdir((char *)args[0].text) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.chdir failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "cwd") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.cwd expects 0 arguments");
        return -1;
      }
      if (execute_side_effects == 0)
        sx_copy_text(value->text, sizeof(value->text), ".");
      else if (sx_get_current_dir(value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.cwd failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "is_dir") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.is_dir expects 1 string argument");
        return -1;
      }
      sx_set_bool_value(value,
                        execute_side_effects != 0 ?
                          sx_path_is_dir(args[0].text) : 0);
      return 0;
    }
  }

  if (strcmp(call->target_name, "time") == 0) {
    if (strcmp(call->member_name, "now_ticks") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "time.now_ticks expects 0 arguments");
        return -1;
      }
      sx_set_i32_value(value,
                       execute_side_effects != 0 ? sx_now_ticks() : 0);
      return 0;
    }
    if (strcmp(call->member_name, "sleep_ticks") == 0) {
      int ticks = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &ticks) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "time.sleep_ticks expects 1 i32 argument");
        return -1;
      }
      if (execute_side_effects != 0 &&
          sx_sleep_for_ticks(ticks) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "time.sleep_ticks failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
  }

  if (strcmp(call->target_name, "text") == 0) {
    if (strcmp(call->member_name, "contains") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.contains expects 2 string arguments");
        return -1;
      }
      sx_set_bool_value(value, sx_text_contains(args[0].text, args[1].text));
      return 0;
    }
    if (strcmp(call->member_name, "trim") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.trim expects 1 string argument");
        return -1;
      }
      if (sx_trim_text(args[0].text, value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.trim failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "concat") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.concat expects 2 string arguments");
        return -1;
      }
      if (sx_concat_text(args[0].text, args[1].text,
                         value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.concat failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
  }

  if (strcmp(call->target_name, "json") == 0) {
    int token_count = 0;
    int value_index = -1;

    if (strcmp(call->member_name, "valid") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.valid expects 1 string argument");
        return -1;
      }
      sx_set_bool_value(value,
                        sx_parse_json(args[0].text, tokens, &token_count) == 0);
      return 0;
    }

    if (call->arg_count != 2 ||
        sx_value_to_string(&args[0], span, diag) < 0 ||
        sx_value_to_string(&args[1], span, diag) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "json.get_* expects json text and key string");
      return -1;
    }
    if (sx_parse_json(args[0].text, tokens, &token_count) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "json.parse failed");
      return -1;
    }
    value_index = sx_json_find_value(args[0].text, tokens, token_count, args[1].text);
    if (value_index < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "json key not found");
      return -1;
    }

    if (strcmp(call->member_name, "get_str") == 0) {
      if (json_token_str(args[0].text, &tokens[value_index],
                         value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.get_str failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = (int)strlen(value->text);
      return 0;
    }
    if (strcmp(call->member_name, "get_bool") == 0) {
      int bool_value = 0;

      if (json_token_bool(args[0].text, &tokens[value_index], &bool_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.get_bool failed");
        return -1;
      }
      sx_set_bool_value(value, bool_value);
      return 0;
    }
    if (strcmp(call->member_name, "get_i32") == 0) {
      int int_value = 0;

      if (json_token_int(args[0].text, &tokens[value_index], &int_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.get_i32 failed");
        return -1;
      }
      sx_set_i32_value(value, int_value);
      return 0;
    }
  }

  if (strcmp(call->target_name, "bytes") == 0) {
    if (strcmp(call->member_name, "from_text") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "bytes.from_text expects 1 string argument");
        return -1;
      }
      sx_set_bytes_value(value, args[0].text, (int)strlen(args[0].text));
      return 0;
    }
    if (strcmp(call->member_name, "to_text") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_bytes(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "bytes.to_text expects 1 bytes argument");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      value->data_len = args[0].data_len;
      if (value->data_len >= SX_TEXT_MAX)
        value->data_len = SX_TEXT_MAX - 1;
      memcpy(value->text, args[0].text, (size_t)value->data_len);
      value->text[value->data_len] = '\0';
      return 0;
    }
    if (strcmp(call->member_name, "len") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_bytes(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "bytes.len expects 1 bytes argument");
        return -1;
      }
      sx_set_i32_value(value, args[0].data_len);
      return 0;
    }
  }

  if (strcmp(call->target_name, "list") == 0) {
    int handle = -1;
    struct sx_list_handle *list_handle;

    if (strcmp(call->member_name, "new") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list.new expects 0 arguments");
        return -1;
      }
      handle = sx_alloc_list_handle(runtime);
      if (handle < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list handle table is full");
        return -1;
      }
      sx_set_list_value(value, handle);
      return 0;
    }
    if (call->arg_count < 1 ||
        sx_value_to_list_handle(&args[0], span, diag, &handle) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "list.* expects list as first argument");
      return -1;
    }
    list_handle = sx_get_list_handle(runtime, handle);
    if (list_handle == 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "invalid list handle");
      return -1;
    }
    if (strcmp(call->member_name, "len") == 0) {
      if (call->arg_count != 1) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list.len expects 1 argument");
        return -1;
      }
      sx_set_i32_value(value, list_handle->count);
      return 0;
    }
    if (strcmp(call->member_name, "push") == 0) {
      if (call->arg_count != 2) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list.push expects 2 arguments");
        return -1;
      }
      if (list_handle->count >= SX_MAX_LIST_ITEMS) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list is full");
        return -1;
      }
      list_handle->items[list_handle->count++] = args[1];
      sx_set_i32_value(value, list_handle->count);
      return 0;
    }
    if (strcmp(call->member_name, "get") == 0 ||
        strcmp(call->member_name, "set") == 0) {
      int index = 0;

      if ((strcmp(call->member_name, "get") == 0 && call->arg_count != 2) ||
          (strcmp(call->member_name, "set") == 0 && call->arg_count != 3) ||
          sx_value_to_i32(&args[1], span, diag, &index) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list.get/list.set expects list, index, [value]");
        return -1;
      }
      if (index < 0 || index >= list_handle->count) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "list index out of range");
        return -1;
      }
      if (strcmp(call->member_name, "get") == 0) {
        *value = list_handle->items[index];
        return 0;
      }
      list_handle->items[index] = args[2];
      sx_set_bool_value(value, 1);
      return 0;
    }
  }

  if (strcmp(call->target_name, "map") == 0) {
    int handle = -1;
    struct sx_map_handle *map_handle;
    int entry_index = -1;

    if (strcmp(call->member_name, "new") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map.new expects 0 arguments");
        return -1;
      }
      handle = sx_alloc_map_handle(runtime);
      if (handle < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map handle table is full");
        return -1;
      }
      sx_set_map_value(value, handle);
      return 0;
    }
    if (call->arg_count < 1 ||
        sx_value_to_map_handle(&args[0], span, diag, &handle) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "map.* expects map as first argument");
      return -1;
    }
    map_handle = sx_get_map_handle(runtime, handle);
    if (map_handle == 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "invalid map handle");
      return -1;
    }
    if (strcmp(call->member_name, "len") == 0) {
      if (call->arg_count != 1) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map.len expects 1 argument");
        return -1;
      }
      sx_set_i32_value(value, map_handle->count);
      return 0;
    }
    if (call->arg_count < 2 ||
        sx_value_to_string(&args[1], span, diag) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "map.* expects string key as second argument");
      return -1;
    }
    for (i = 0; i < SX_MAX_MAP_ITEMS; i++) {
      if (map_handle->entries[i].used == 0)
        continue;
      if (strcmp(map_handle->entries[i].key, args[1].text) == 0) {
        entry_index = i;
        break;
      }
    }
    if (strcmp(call->member_name, "has") == 0) {
      if (call->arg_count != 2) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map.has expects 2 arguments");
        return -1;
      }
      sx_set_bool_value(value, entry_index >= 0);
      return 0;
    }
    if (strcmp(call->member_name, "get") == 0) {
      if (call->arg_count != 2) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map.get expects 2 arguments");
        return -1;
      }
      if (entry_index < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map key not found");
        return -1;
      }
      *value = map_handle->entries[entry_index].value;
      return 0;
    }
    if (strcmp(call->member_name, "set") == 0) {
      if (call->arg_count != 3) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map.set expects 3 arguments");
        return -1;
      }
      if (entry_index < 0) {
        for (i = 0; i < SX_MAX_MAP_ITEMS; i++) {
          if (map_handle->entries[i].used == 0) {
            entry_index = i;
            break;
          }
        }
      }
      if (entry_index < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map is full");
        return -1;
      }
      if (map_handle->entries[entry_index].used == 0)
        map_handle->count++;
      map_handle->entries[entry_index].used = 1;
      sx_copy_text(map_handle->entries[entry_index].key,
                   sizeof(map_handle->entries[entry_index].key),
                   args[1].text);
      map_handle->entries[entry_index].value = args[2];
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "remove") == 0) {
      if (call->arg_count != 2) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "map.remove expects 2 arguments");
        return -1;
      }
      if (entry_index >= 0) {
        memset(&map_handle->entries[entry_index], 0,
               sizeof(map_handle->entries[entry_index]));
        map_handle->count--;
      }
      sx_set_bool_value(value, entry_index >= 0);
      return 0;
    }
  }

  if (strcmp(call->target_name, "result") == 0) {
    int handle = -1;
    struct sx_result_handle *result_handle;

    if (strcmp(call->member_name, "ok") == 0) {
      if (call->arg_count != 1 || sx_store_result(runtime, 1, &args[0], "", value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "result.ok expects 1 argument");
        return -1;
      }
      return 0;
    }
    if (strcmp(call->member_name, "err") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_store_result(runtime, 0, 0, args[0].text, value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "result.err expects 1 string argument");
        return -1;
      }
      return 0;
    }
    if (call->arg_count != 1 ||
        sx_value_to_result_handle(&args[0], span, diag, &handle) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "result.* expects result as first argument");
      return -1;
    }
    result_handle = sx_get_result_handle(runtime, handle);
    if (result_handle == 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "invalid result handle");
      return -1;
    }
    if (strcmp(call->member_name, "is_ok") == 0) {
      sx_set_bool_value(value, result_handle->ok);
      return 0;
    }
    if (strcmp(call->member_name, "error") == 0) {
      sx_set_string_value(value, result_handle->error);
      return 0;
    }
    if (strcmp(call->member_name, "value") == 0) {
      if (result_handle->ok == 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "result has no value");
        return -1;
      }
      *value = result_handle->value;
      return 0;
    }
  }

  if (strcmp(call->target_name, "test") == 0) {
    if (strcmp(call->member_name, "assert_eq") == 0) {
      if (call->arg_count != 2) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "test.assert_eq expects 2 arguments");
        return -1;
      }
      if (sx_value_equals(&args[0], &args[1]) == 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "test.assert_eq failed");
        return -1;
      }
      sx_set_unit_value(value);
      return 0;
    }
    if (strcmp(call->member_name, "fail") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "test.fail expects 1 string argument");
        return -1;
      }
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        args[0].text);
      return -1;
    }
  }

  if (strcmp(call->target_name, "net") == 0) {
    if (strcmp(call->member_name, "connect") == 0) {
      struct sockaddr_in addr;
      int sockfd = -1;
      int port = 0;

      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_i32(&args[1], span, diag, &port) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.connect expects host and port");
        return -1;
      }
      if (execute_side_effects == 0) {
        sx_set_i32_value(value, sx_alloc_dummy_socket_fd(runtime));
        return 0;
      }
      if (sx_build_sockaddr(args[0].text, port, &addr) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.connect failed to resolve host");
        return -1;
      }
      sockfd = socket(AF_INET, SOCK_STREAM, 0);
      if (sockfd < 0 ||
          connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (sockfd >= 0)
          sx_close_socket_fd(sockfd);
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.connect failed");
        return -1;
      }
      if (sx_track_socket_fd(runtime, sockfd) < 0) {
        sx_close_socket_fd(sockfd);
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "socket table is full");
        return -1;
      }
      sx_set_i32_value(value, sockfd);
      return 0;
    }
    if (strcmp(call->member_name, "listen") == 0) {
      struct sockaddr_in addr;
      int listener = -1;
      int port = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &port) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.listen expects 1 i32 port");
        return -1;
      }
      if (execute_side_effects == 0) {
        sx_set_i32_value(value, sx_alloc_dummy_socket_fd(runtime));
        return 0;
      }
      if (sx_build_sockaddr(0, port, &addr) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.listen failed");
        return -1;
      }
      listener = socket(AF_INET, SOCK_STREAM, 0);
      if (listener < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.listen failed");
        return -1;
      }
#ifdef TEST_BUILD
      {
        int reuse = 1;

        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, (socklen_t)sizeof(reuse));
      }
#endif
      if (bind(listener, (const struct sockaddr *)&addr, sizeof(addr)) < 0 ||
          listen(listener, 4) < 0) {
        sx_close_socket_fd(listener);
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.listen failed");
        return -1;
      }
      if (sx_track_socket_fd(runtime, listener) < 0) {
        sx_close_socket_fd(listener);
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "socket table is full");
        return -1;
      }
      sx_set_i32_value(value, listener);
      return 0;
    }
    if (strcmp(call->member_name, "accept") == 0) {
      int listener = -1;
      int sockfd = -1;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &listener) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.accept expects 1 i32 socket");
        return -1;
      }
      if (execute_side_effects == 0) {
        sx_set_i32_value(value, sx_alloc_dummy_socket_fd(runtime));
        return 0;
      }
#ifdef TEST_BUILD
      sockfd = accept(listener, 0, 0);
#else
      {
        struct sockaddr_in peer;

        sockfd = accept(listener, (struct sockaddr *)&peer, 0);
      }
#endif
      if (sockfd < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.accept failed");
        return -1;
      }
      if (sx_track_socket_fd(runtime, sockfd) < 0) {
        sx_close_socket_fd(sockfd);
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "socket table is full");
        return -1;
      }
      sx_set_i32_value(value, sockfd);
      return 0;
    }
    if (strcmp(call->member_name, "read") == 0 ||
        strcmp(call->member_name, "read_bytes") == 0) {
      int sockfd = -1;
      int len = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &sockfd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.read/net.read_bytes expects 1 i32 socket");
        return -1;
      }
      if (execute_side_effects == 0) {
        if (strcmp(call->member_name, "read_bytes") == 0)
          sx_set_bytes_value(value, "", 0);
        else
          sx_set_string_value(value, "");
        return 0;
      }
      len = sx_socket_read_text(sockfd, value->text, sizeof(value->text));
      if (len < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.read/net.read_bytes failed");
        return -1;
      }
      if (strcmp(call->member_name, "read_bytes") == 0)
        sx_set_bytes_value(value, value->text, len);
      else {
        value->kind = SX_VALUE_STRING;
        value->bool_value = 0;
        value->int_value = 0;
        value->data_len = len;
        value->text[len] = '\0';
      }
      return 0;
    }
    if (strcmp(call->member_name, "write") == 0 ||
        strcmp(call->member_name, "write_bytes") == 0) {
      int sockfd = -1;
      int written = 0;
      const char *payload = 0;
      int payload_len = 0;

      if (call->arg_count != 2 ||
          sx_value_to_i32(&args[0], span, diag, &sockfd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.write/net.write_bytes expects socket and payload");
        return -1;
      }
      if (strcmp(call->member_name, "write_bytes") == 0) {
        if (sx_value_to_bytes(&args[1], span, diag) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "net.write_bytes expects bytes payload");
          return -1;
        }
        payload = args[1].text;
        payload_len = args[1].data_len;
      } else {
        if (sx_value_to_string(&args[1], span, diag) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "net.write expects string payload");
          return -1;
        }
        payload = args[1].text;
        payload_len = (int)strlen(args[1].text);
      }
      if (execute_side_effects == 0)
        written = payload_len;
      else {
        written = sx_socket_write_all(sockfd, payload, payload_len);
        if (written < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "net.write/net.write_bytes failed");
          return -1;
        }
      }
      sx_set_i32_value(value, written);
      return 0;
    }
    if (strcmp(call->member_name, "poll_read") == 0) {
      int sockfd = -1;
      int timeout_ticks = 0;
      int ready = 0;

      if (call->arg_count != 2 ||
          sx_value_to_i32(&args[0], span, diag, &sockfd) < 0 ||
          sx_value_to_i32(&args[1], span, diag, &timeout_ticks) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.poll_read expects socket and timeout");
        return -1;
      }
      if (execute_side_effects != 0) {
        ready = sx_poll_socket_readable(sockfd, timeout_ticks);
        if (ready < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "net.poll_read failed");
          return -1;
        }
      }
      sx_set_bool_value(value, ready > 0);
      return 0;
    }
    if (strcmp(call->member_name, "close") == 0) {
      int sockfd = -1;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &sockfd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.close expects 1 i32 socket");
        return -1;
      }
      if (execute_side_effects != 0 &&
          sx_close_socket_fd(sockfd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "net.close failed");
        return -1;
      }
      sx_detach_socket_fd(runtime, sockfd);
      sx_set_bool_value(value, 1);
      return 0;
    }
  }

  if (strcmp(call->target_name, "proc") == 0) {
    if (strcmp(call->member_name, "argv_count") == 0) {
      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.argv_count expects 0 arguments");
        return -1;
      }
      sx_set_i32_value(value, runtime->argc);
      return 0;
    }
    if (strcmp(call->member_name, "argv") == 0) {
      int index = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &index) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.argv expects 1 i32 argument");
        return -1;
      }
      if (index < 0 || index >= runtime->argc) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.argv index out of range");
        return -1;
      }
      sx_set_string_value(value, runtime->argv[index]);
      return 0;
    }
    if (strcmp(call->member_name, "env") == 0 ||
        strcmp(call->member_name, "has_env") == 0) {
      const char *env_value;

      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.env/proc.has_env expects 1 string argument");
        return -1;
      }
      env_value = sx_env_get(args[0].text);
      if (strcmp(call->member_name, "has_env") == 0) {
        sx_set_bool_value(value, env_value != 0);
        return 0;
      }
      if (env_value == 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.env key not found");
        return -1;
      }
      sx_set_string_value(value, env_value);
      return 0;
    }
    if (strcmp(call->member_name, "status_ok") == 0) {
      int status_value = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &status_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.status_ok expects 1 i32 argument");
        return -1;
      }
      sx_set_bool_value(value, status_value == 0);
      return 0;
    }
    if (strcmp(call->member_name, "run") == 0 ||
        strcmp(call->member_name, "capture") == 0 ||
        strcmp(call->member_name, "spawn") == 0 ||
        strcmp(call->member_name, "try_run") == 0 ||
        strcmp(call->member_name, "try_capture") == 0) {
      pid_t pid;
      struct sx_value payload;

      for (i = 0; i < call->arg_count; i++) {
        if (sx_value_to_string(&args[i], span, diag) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.run/proc.capture/proc.spawn expects string arguments");
          return -1;
        }
      }
      if (strcmp(call->member_name, "spawn") == 0) {
        if (sx_spawn_from_values(runtime, args, call->arg_count, 1,
                                 -1, -1, -1,
                                 execute_side_effects, &pid) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.spawn failed");
          return -1;
        }
        sx_set_i32_value(value, (int)pid);
        return 0;
      }
      sx_set_unit_value(&payload);
      if (sx_run_process(runtime, args, call->arg_count,
                         strcmp(call->member_name, "capture") == 0 ||
                         strcmp(call->member_name, "try_capture") == 0,
                         execute_side_effects,
                         (strcmp(call->member_name, "try_run") == 0 ||
                          strcmp(call->member_name, "try_capture") == 0) ?
                             &payload : value) < 0) {
        if (strcmp(call->member_name, "try_run") == 0 ||
            strcmp(call->member_name, "try_capture") == 0) {
          if (sx_store_result(runtime, 0, 0,
                              strcmp(call->member_name, "try_capture") == 0 ?
                                  "proc.capture failed" : "proc.run failed",
                              value) < 0) {
            sx_set_diagnostic(diag, span->offset, span->length,
                              span->line, span->column,
                              "proc.try_* failed");
            return -1;
          }
          return 0;
        }
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.run/proc.capture failed");
        return -1;
      }
      if (strcmp(call->member_name, "try_run") == 0 ||
          strcmp(call->member_name, "try_capture") == 0) {
        if (sx_store_result(runtime, 1, &payload, "", value) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.try_* failed");
          return -1;
        }
      }
      return 0;
    }
    if (strcmp(call->member_name, "wait") == 0) {
      int pid_value = 0;
      int status = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &pid_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.wait expects 1 i32 argument");
        return -1;
      }
      if (execute_side_effects != 0) {
        sx_guest_debug_printf("AUDIT sx_wait_begin pid=%d\r\n", pid_value);
        if (sx_wait_for_pid((pid_t)pid_value, &status) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.wait failed");
          return -1;
        }
      }
      if (execute_side_effects != 0)
        sx_guest_debug_printf("AUDIT sx_wait_done pid=%d status=%d\r\n",
                              pid_value, status);
      sx_set_i32_value(value, status);
      return 0;
    }
    if (strcmp(call->member_name, "pipe") == 0) {
      int handle;

      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.pipe expects 0 arguments");
        return -1;
      }
      if (execute_side_effects != 0) {
        int pipefd[2];

        if (pipe(pipefd) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.pipe failed");
          return -1;
        }
        handle = sx_alloc_pipe_handle(runtime, pipefd[0], pipefd[1]);
        if (handle < 0) {
          close(pipefd[0]);
          close(pipefd[1]);
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.pipe handle table is full");
          return -1;
        }
      } else {
        handle = sx_alloc_pipe_handle(runtime, -1, -1);
        if (handle < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.pipe handle table is full");
          return -1;
        }
        runtime->pipes[handle].read_fd = SX_DUMMY_FD_BASE + handle * 2;
        runtime->pipes[handle].write_fd = SX_DUMMY_FD_BASE + handle * 2 + 1;
      }
      sx_set_i32_value(value, handle);
      return 0;
    }
    if (strcmp(call->member_name, "pipe_read_fd") == 0 ||
        strcmp(call->member_name, "pipe_write_fd") == 0 ||
        strcmp(call->member_name, "pipe_close") == 0) {
      int handle = -1;
      struct sx_pipe_handle *pipe_handle;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &handle) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.pipe_* expects 1 i32 handle");
        return -1;
      }
      pipe_handle = sx_get_pipe_handle(runtime, handle);
      if (pipe_handle == 0) {
        if (strcmp(call->member_name, "pipe_close") == 0) {
          sx_set_bool_value(value, 1);
          return 0;
        }
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "invalid pipe handle");
        return -1;
      }
      if (strcmp(call->member_name, "pipe_read_fd") == 0) {
        if (pipe_handle->read_fd < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "pipe read end is closed");
          return -1;
        }
        sx_set_i32_value(value, pipe_handle->read_fd);
        return 0;
      }
      if (strcmp(call->member_name, "pipe_write_fd") == 0) {
        if (pipe_handle->write_fd < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "pipe write end is closed");
          return -1;
        }
        sx_set_i32_value(value, pipe_handle->write_fd);
        return 0;
      }
      if (execute_side_effects != 0)
        sx_close_pipe_handle(pipe_handle);
      else {
        pipe_handle->active = 0;
        pipe_handle->read_fd = -1;
        pipe_handle->write_fd = -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
    if (strcmp(call->member_name, "spawn_io") == 0) {
      int stdin_fd = -1;
      int stdout_fd = -1;
      int stderr_fd = -1;
      pid_t pid;

      if (call->arg_count < 4 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_i32(&args[1], span, diag, &stdin_fd) < 0 ||
          sx_value_to_i32(&args[2], span, diag, &stdout_fd) < 0 ||
          sx_value_to_i32(&args[3], span, diag, &stderr_fd) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.spawn_io expects path and 3 fd arguments");
        return -1;
      }
      for (i = 4; i < call->arg_count; i++) {
        if (sx_value_to_string(&args[i], span, diag) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.spawn_io command arguments must be strings");
          return -1;
        }
      }
      if (sx_spawn_from_values(runtime, args, call->arg_count, 4,
                               stdin_fd, stdout_fd, stderr_fd,
                               execute_side_effects, &pid) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.spawn_io failed");
        return -1;
      }
      if (execute_side_effects != 0) {
        sx_guest_debug_printf(
            "AUDIT sx_spawn_io_done pid=%d stdin=%d stdout=%d stderr=%d\r\n",
            (int)pid, stdin_fd, stdout_fd, stderr_fd);
      }
      sx_set_i32_value(value, (int)pid);
      return 0;
    }
    if (strcmp(call->member_name, "fork") == 0) {
      pid_t pid;

      if (call->arg_count != 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.fork expects 0 arguments");
        return -1;
      }
      if (execute_side_effects == 0)
        pid = SX_DUMMY_PID;
      else
        pid = fork();
      if (pid < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.fork failed");
        return -1;
      }
      sx_set_i32_value(value, (int)pid);
      return 0;
    }
    if (strcmp(call->member_name, "exit") == 0) {
      int exit_code = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &exit_code) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.exit expects 1 i32 argument");
        return -1;
      }
      if (execute_side_effects != 0)
        exit(exit_code);
      sx_set_unit_value(value);
      return 0;
    }
  }

  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "unknown builtin");
  return -1;
}

static int sx_eval_call_expr(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             const struct sx_call_expr *call,
                             const struct sx_source_span *span,
                             int execute_side_effects,
                             struct sx_value *value,
                             struct sx_diagnostic *diag)
{
  if (call->target_kind == SX_CALL_TARGET_FUNCTION) {
    return sx_call_user_function(runtime, program, call, span,
                                 execute_side_effects, value, diag);
  }
  return sx_eval_namespace_call(runtime, program, call, span,
                                execute_side_effects, value, diag);
}

static int sx_eval_expr(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        const struct sx_expr *expr,
                        struct sx_value *value,
                        int execute_side_effects,
                        struct sx_diagnostic *diag)
{
  if (expr->kind == SX_EXPR_ATOM)
    return sx_eval_atom(runtime, &expr->data.atom, value, diag);
  if (expr->kind == SX_EXPR_CALL)
    return sx_eval_call_expr(runtime, program, &expr->data.call_expr,
                             &expr->span, execute_side_effects, value, diag);
  if (expr->kind == SX_EXPR_UNARY)
    return sx_eval_unary_expr(runtime, program, &expr->data.unary_expr,
                              &expr->span, execute_side_effects, value, diag);
  if (expr->kind == SX_EXPR_BINARY)
    return sx_eval_binary_expr(runtime, program, &expr->data.binary_expr,
                               &expr->span, execute_side_effects, value, diag);
  if (expr->kind == SX_EXPR_LIST)
    return sx_eval_list_expr(runtime, program, &expr->data.list_expr,
                             execute_side_effects, value, diag);
  if (expr->kind == SX_EXPR_MAP)
    return sx_eval_map_expr(runtime, program, &expr->data.map_expr,
                            execute_side_effects, value, diag);
  sx_set_diagnostic(diag, expr->span.offset, expr->span.length,
                    expr->span.line, expr->span.column,
                    "unsupported expression");
  return -1;
}

static int sx_run_for_stmt(struct sx_runtime *runtime,
                           const struct sx_program *program,
                           const struct sx_stmt *stmt,
                           int execute_calls,
                           struct sx_value *value,
                           struct sx_diagnostic *diag)
{
  int iterations = 0;
  int flow;
  struct sx_source_span scope_span = stmt->span;

  if (sx_enter_scope(runtime, &scope_span, diag) < 0)
    return -1;
  runtime->loop_depth++;
  if (stmt->data.for_stmt.init_stmt_index >= 0) {
    flow = sx_run_statement(runtime, program,
                            &program->statements[stmt->data.for_stmt.init_stmt_index],
                            execute_calls, value, diag);
    if (flow < 0)
      goto fail;
  }

  while (1) {
    if (stmt->data.for_stmt.has_condition != 0) {
      struct sx_value condition_value;
      int condition_bool;

      sx_set_unit_value(&condition_value);
      if (sx_eval_expr(runtime, program, &stmt->data.for_stmt.condition,
                       &condition_value, execute_calls, diag) < 0)
        goto fail;
      condition_bool = sx_value_to_bool(&condition_value,
                                        &stmt->data.for_stmt.condition.span, diag);
      if (condition_bool < 0)
        goto fail;
      if (condition_bool == 0)
        break;
    }
    if (iterations++ >= runtime->limits.max_loop_iterations) {
      sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                        stmt->span.line, stmt->span.column,
                        "for iteration limit exceeded");
      goto fail;
    }
    flow = sx_run_block(runtime, program,
                        stmt->data.for_stmt.body_block_index,
                        execute_calls, 1, value, diag);
    if (flow < 0)
      goto fail;
    if (flow == SX_FLOW_RETURN) {
      runtime->loop_depth--;
      sx_leave_scope(runtime);
      return SX_FLOW_RETURN;
    }
    if (flow != SX_FLOW_BREAK &&
        stmt->data.for_stmt.step_stmt_index >= 0) {
      int step_flow;

      step_flow = sx_run_statement(runtime, program,
                                   &program->statements[stmt->data.for_stmt.step_stmt_index],
                                   execute_calls, value, diag);
      if (step_flow < 0)
        goto fail;
    }
    if (flow == SX_FLOW_BREAK)
      break;
  }

  runtime->loop_depth--;
  sx_leave_scope(runtime);
  sx_set_unit_value(value);
  return SX_FLOW_NEXT;

fail:
  runtime->loop_depth--;
  sx_leave_scope(runtime);
  return -1;
}

static int sx_run_statement(struct sx_runtime *runtime,
                            const struct sx_program *program,
                            const struct sx_stmt *stmt,
                            int execute_calls,
                            struct sx_value *value,
                            struct sx_diagnostic *diag)
{
  sx_set_unit_value(value);

  if (stmt->kind == SX_STMT_LET) {
    if (sx_eval_expr(runtime, program, &stmt->data.let_stmt.value,
                     value, execute_calls, diag) < 0)
      return -1;
    if (sx_register_binding(runtime, stmt->data.let_stmt.name,
                            value, &stmt->span, diag) < 0)
      return -1;
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_CALL) {
    if (sx_eval_call_expr(runtime, program, &stmt->data.call_stmt.call_expr,
                          &stmt->span, execute_calls, value, diag) < 0)
      return -1;
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_ASSIGN) {
    if (sx_eval_expr(runtime, program, &stmt->data.assign_stmt.value,
                     value, execute_calls, diag) < 0)
      return -1;
    if (sx_assign_binding(runtime, stmt->data.assign_stmt.name,
                          value, &stmt->span, diag) < 0)
      return -1;
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_BLOCK) {
    return sx_run_block(runtime, program,
                        stmt->data.block_stmt.block_index,
                        execute_calls, 1, value, diag);
  }

  if (stmt->kind == SX_STMT_IF) {
    struct sx_value condition_value;
    int condition_bool;

    sx_set_unit_value(&condition_value);
    if (sx_eval_expr(runtime, program, &stmt->data.if_stmt.condition,
                     &condition_value, execute_calls, diag) < 0)
      return -1;
    condition_bool = sx_value_to_bool(&condition_value,
                                      &stmt->data.if_stmt.condition.span, diag);
    if (condition_bool < 0)
      return -1;
    if (condition_bool != 0) {
      return sx_run_block(runtime, program,
                          stmt->data.if_stmt.then_block_index,
                          execute_calls, 1, value, diag);
    }
    if (stmt->data.if_stmt.else_block_index >= 0) {
      return sx_run_block(runtime, program,
                          stmt->data.if_stmt.else_block_index,
                          execute_calls, 1, value, diag);
    }
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_WHILE) {
    int iterations = 0;

    runtime->loop_depth++;
    while (1) {
      struct sx_value condition_value;
      int condition_bool;
      int flow;

      sx_set_unit_value(&condition_value);
      if (sx_eval_expr(runtime, program, &stmt->data.while_stmt.condition,
                       &condition_value, execute_calls, diag) < 0) {
        runtime->loop_depth--;
        return -1;
      }
      condition_bool = sx_value_to_bool(&condition_value,
                                        &stmt->data.while_stmt.condition.span, diag);
      if (condition_bool < 0) {
        runtime->loop_depth--;
        return -1;
      }
      if (condition_bool == 0)
        break;
      if (iterations++ >= runtime->limits.max_loop_iterations) {
        sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                          stmt->span.line, stmt->span.column,
                          "while iteration limit exceeded");
        runtime->loop_depth--;
        return -1;
      }
      flow = sx_run_block(runtime, program,
                          stmt->data.while_stmt.body_block_index,
                          execute_calls, 1, value, diag);
      if (flow < 0) {
        runtime->loop_depth--;
        return -1;
      }
      if (flow == SX_FLOW_RETURN) {
        runtime->loop_depth--;
        return SX_FLOW_RETURN;
      }
      if (flow == SX_FLOW_BREAK)
        break;
    }
    runtime->loop_depth--;
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_FOR)
    return sx_run_for_stmt(runtime, program, stmt,
                           execute_calls, value, diag);

  if (stmt->kind == SX_STMT_RETURN) {
    if (runtime->inside_function <= 0) {
      sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                        stmt->span.line, stmt->span.column,
                        "return outside function");
      return -1;
    }
    if (stmt->data.return_stmt.has_value == 0) {
      sx_set_unit_value(value);
      return SX_FLOW_RETURN;
    }
    if (sx_eval_expr(runtime, program, &stmt->data.return_stmt.value,
                     value, execute_calls, diag) < 0)
      return -1;
    return SX_FLOW_RETURN;
  }

  if (stmt->kind == SX_STMT_BREAK) {
    if (runtime->loop_depth <= 0) {
      sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                        stmt->span.line, stmt->span.column,
                        "break outside loop");
      return -1;
    }
    return SX_FLOW_BREAK;
  }

  if (stmt->kind == SX_STMT_CONTINUE) {
    if (runtime->loop_depth <= 0) {
      sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                        stmt->span.line, stmt->span.column,
                        "continue outside loop");
      return -1;
    }
    return SX_FLOW_CONTINUE;
  }

  sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                    stmt->span.line, stmt->span.column,
                    "unsupported statement");
  return -1;
}

static int sx_run_block(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        int block_index,
                        int execute_calls,
                        int create_scope,
                        struct sx_value *value,
                        struct sx_diagnostic *diag)
{
  struct sx_source_span scope_span;
  int stmt_index;
  int flow = SX_FLOW_NEXT;

  memset(&scope_span, 0, sizeof(scope_span));
  if (block_index < 0 || block_index >= program->block_count) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid block");
    return -1;
  }
  if (create_scope != 0 &&
      sx_enter_scope(runtime, &scope_span, diag) < 0)
    return -1;

  stmt_index = program->blocks[block_index].first_stmt_index;
  while (stmt_index >= 0) {
    flow = sx_run_statement(runtime, program,
                            &program->statements[stmt_index],
                            execute_calls, value, diag);
    if (flow < 0) {
      if (create_scope != 0)
        sx_leave_scope(runtime);
      return -1;
    }
    if (flow != SX_FLOW_NEXT) {
      if (create_scope != 0)
        sx_leave_scope(runtime);
      return flow;
    }
    stmt_index = program->statements[stmt_index].next_stmt_index;
  }

  if (create_scope != 0)
    sx_leave_scope(runtime);
  return SX_FLOW_NEXT;
}

static int sx_run_program(struct sx_runtime *runtime,
                          const struct sx_program *program,
                          int execute_calls,
                          struct sx_diagnostic *diag)
{
  struct sx_value value;
  int flow;

  sx_clear_diagnostic(diag);
  runtime->error_call_depth = 0;
  if (sx_validate_functions(program, diag) < 0)
    return -1;
  sx_set_unit_value(&value);
  flow = sx_run_block(runtime, program, program->top_level_block_index,
                      execute_calls, 0, &value, diag);
  if (flow < 0)
    return -1;
  if (flow == SX_FLOW_RETURN) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "return outside function");
    return -1;
  }
  return 0;
}

int sx_runtime_format_stack_trace(const struct sx_runtime *runtime,
                                  char *buf, int cap)
{
  const char (*stack)[SX_NAME_MAX];
  int depth;
  int len = 0;
  int i;

  if (buf == 0 || cap <= 0)
    return -1;
  buf[0] = '\0';
  if (runtime == 0)
    return 0;
  depth = runtime->error_call_depth > 0 ?
              runtime->error_call_depth : runtime->call_depth;
  stack = runtime->error_call_depth > 0 ?
              runtime->error_call_stack : runtime->call_stack;
  for (i = depth - 1; i >= 0; i--) {
    int name_len = (int)strlen(stack[i]);

    if (name_len <= 0)
      continue;
    if (len + 5 + name_len + 1 >= cap)
      return -1;
    memcpy(buf + len, "  at ", 5);
    len += 5;
    memcpy(buf + len, stack[i], (size_t)name_len);
    len += name_len;
    buf[len++] = '\n';
    buf[len] = '\0';
  }
  return len;
}

void sx_runtime_init(struct sx_runtime *runtime)
{
  if (runtime == 0)
    return;
  memset(runtime, 0, sizeof(*runtime));
  runtime->output = sx_default_output;
  runtime->output_ctx = 0;
  sx_runtime_default_limits(&runtime->limits);
}

void sx_runtime_dispose(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return;
  for (i = 0; i < SX_MAX_PIPE_HANDLES; i++)
    sx_close_pipe_handle(&runtime->pipes[i]);
  for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
    if (runtime->sockets[i].active == 0)
      continue;
    sx_close_socket_fd(runtime->sockets[i].fd);
    runtime->sockets[i].active = 0;
    runtime->sockets[i].fd = -1;
  }
}

void sx_runtime_default_limits(struct sx_runtime_limits *limits)
{
  if (limits == 0)
    return;
  limits->max_bindings = SX_MAX_BINDINGS;
  limits->max_scope_depth = SX_MAX_SCOPE_DEPTH;
  limits->max_call_depth = SX_MAX_CALL_DEPTH;
  limits->max_loop_iterations = 1024;
}

int sx_runtime_set_limits(struct sx_runtime *runtime,
                          const struct sx_runtime_limits *limits)
{
  struct sx_runtime_limits validated;

  if (runtime == 0 || limits == 0)
    return -1;
  sx_copy_runtime_limits(&validated, limits);
  if (validated.max_bindings <= 0 || validated.max_bindings > SX_MAX_BINDINGS)
    return -1;
  if (validated.max_scope_depth <= 0 ||
      validated.max_scope_depth > SX_MAX_SCOPE_DEPTH)
    return -1;
  if (validated.max_call_depth <= 0 ||
      validated.max_call_depth > SX_MAX_CALL_DEPTH)
    return -1;
  if (validated.max_loop_iterations <= 0)
    return -1;
  sx_copy_runtime_limits(&runtime->limits, &validated);
  return 0;
}

void sx_runtime_reset_session(struct sx_runtime *runtime)
{
  sx_output_fn output;
  void *output_ctx;
  struct sx_runtime_limits limits;
  int argc;
  char argv_copy[SX_MAX_RUNTIME_ARGS][SX_TEXT_MAX];

  if (runtime == 0)
    return;
  output = runtime->output != 0 ? runtime->output : sx_default_output;
  output_ctx = runtime->output_ctx;
  sx_copy_runtime_limits(&limits, &runtime->limits);
  argc = runtime->argc;
  memcpy(argv_copy, runtime->argv, sizeof(argv_copy));
  sx_runtime_dispose(runtime);
  memset(runtime, 0, sizeof(*runtime));
  runtime->output = output;
  runtime->output_ctx = output_ctx;
  runtime->argc = argc;
  memcpy(runtime->argv, argv_copy, sizeof(argv_copy));
  sx_copy_runtime_limits(&runtime->limits, &limits);
}

void sx_runtime_set_output(struct sx_runtime *runtime,
                           sx_output_fn output, void *ctx)
{
  if (runtime == 0)
    return;
  runtime->output = output != 0 ? output : sx_default_output;
  runtime->output_ctx = ctx;
}

int sx_runtime_set_argv(struct sx_runtime *runtime,
                        int argc, char *const argv[])
{
  int i;

  if (runtime == 0 || argc < 0)
    return -1;
  if (argc > SX_MAX_RUNTIME_ARGS)
    return -1;
  runtime->argc = argc;
  for (i = 0; i < SX_MAX_RUNTIME_ARGS; i++)
    runtime->argv[i][0] = '\0';
  for (i = 0; i < argc; i++) {
    if (sx_copy_text(runtime->argv[i], sizeof(runtime->argv[i]),
                     argv != 0 ? argv[i] : "") < 0)
      return -1;
  }
  return 0;
}

int sx_runtime_check_program(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             struct sx_diagnostic *diag)
{
  return sx_run_program(runtime, program, 0, diag);
}

int sx_runtime_execute_program(struct sx_runtime *runtime,
                               const struct sx_program *program,
                               struct sx_diagnostic *diag)
{
  return sx_run_program(runtime, program, 1, diag);
}
