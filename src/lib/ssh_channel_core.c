#include <ssh_channel_core.h>

#include <ssh_packet_core.h>

#include <string.h>

PRIVATE int ssh_channel_copy_type(char *dest, int cap,
                                  const u_int8_t *src, int len)
{
  int i;

  if (dest == 0 || cap <= 0 || src == 0 || len < 0 || len >= cap)
    return -1;
  for (i = 0; i < len; i++) {
    dest[i] = (char)src[i];
  }
  dest[len] = '\0';
  return 0;
}

PUBLIC int ssh_channel_parse_open(const u_int8_t *payload, int payload_len,
                                  struct ssh_channel_open_request *out)
{
  struct ssh_reader reader;
  const u_int8_t *type = 0;
  int type_len = 0;

  if (payload == 0 || payload_len <= 1 || out == 0)
    return -1;

  memset(out, 0, sizeof(*out));
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &type, &type_len);
  out->peer_id = ssh_reader_get_u32(&reader);
  out->peer_window = ssh_reader_get_u32(&reader);
  out->peer_max_packet = ssh_reader_get_u32(&reader);
  if (reader.error)
    return -1;
  return ssh_channel_copy_type(out->type, sizeof(out->type), type, type_len);
}

PUBLIC int ssh_channel_parse_request(const u_int8_t *payload, int payload_len,
                                     struct ssh_channel_request *out)
{
  struct ssh_reader reader;
  const u_int8_t *request = 0;
  const u_int8_t *ignored = 0;
  int request_len = 0;
  int ignored_len = 0;

  if (payload == 0 || payload_len <= 1 || out == 0)
    return -1;

  memset(out, 0, sizeof(*out));
  out->kind = SSH_CHANNEL_REQUEST_UNKNOWN;
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  out->recipient = ssh_reader_get_u32(&reader);
  ssh_reader_get_string(&reader, &request, &request_len);
  out->want_reply = ssh_reader_get_bool(&reader);
  if (reader.error)
    return -1;

  if (ssh_bytes_equal(request, request_len, "pty-req")) {
    ssh_reader_get_string(&reader, &ignored, &ignored_len);
    out->cols = (u_int16_t)ssh_reader_get_u32(&reader);
    out->rows = (u_int16_t)ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    ssh_reader_get_string(&reader, &ignored, &ignored_len);
    out->kind = SSH_CHANNEL_REQUEST_PTY;
  } else if (ssh_bytes_equal(request, request_len, "window-change")) {
    out->cols = (u_int16_t)ssh_reader_get_u32(&reader);
    out->rows = (u_int16_t)ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    ssh_reader_get_u32(&reader);
    out->kind = SSH_CHANNEL_REQUEST_WINDOW_CHANGE;
  } else if (ssh_bytes_equal(request, request_len, "shell")) {
    out->kind = SSH_CHANNEL_REQUEST_SHELL;
  }

  return reader.error ? -1 : 0;
}

PUBLIC int ssh_channel_parse_data(const u_int8_t *payload, int payload_len,
                                  struct ssh_channel_data_request *out)
{
  struct ssh_reader reader;
  const u_int8_t *data = 0;
  int data_len = 0;

  if (payload == 0 || payload_len <= 1 || out == 0)
    return -1;

  memset(out, 0, sizeof(*out));
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  out->recipient = ssh_reader_get_u32(&reader);
  ssh_reader_get_string(&reader, &data, &data_len);
  if (reader.error)
    return -1;
  out->data = data;
  out->data_len = data_len;
  return 0;
}

PUBLIC void ssh_channel_plan_shutdown(int exit_status_sent,
                                      int eof_sent,
                                      int close_sent,
                                      struct ssh_channel_close_plan *plan)
{
  if (plan == 0)
    return;
  memset(plan, 0, sizeof(*plan));
  plan->send_exit_status = exit_status_sent ? FALSE : TRUE;
  plan->send_eof = eof_sent ? FALSE : TRUE;
  plan->send_close = close_sent ? FALSE : TRUE;
}
