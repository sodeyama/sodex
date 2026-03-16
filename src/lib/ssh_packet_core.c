#include <ssh_packet_core.h>

#include <string.h>

PUBLIC void ssh_write_u32_be(u_int8_t *buf, u_int32_t value)
{
  buf[0] = (u_int8_t)(value >> 24);
  buf[1] = (u_int8_t)(value >> 16);
  buf[2] = (u_int8_t)(value >> 8);
  buf[3] = (u_int8_t)value;
}

PUBLIC u_int32_t ssh_read_u32_be(const u_int8_t *buf)
{
  return ((u_int32_t)buf[0] << 24) |
         ((u_int32_t)buf[1] << 16) |
         ((u_int32_t)buf[2] << 8) |
         (u_int32_t)buf[3];
}

PUBLIC void ssh_writer_init(struct ssh_writer *writer, u_int8_t *buf, int cap)
{
  writer->buf = buf;
  writer->cap = cap;
  writer->len = 0;
  writer->error = FALSE;
}

PUBLIC void ssh_writer_put_data(struct ssh_writer *writer,
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

PUBLIC void ssh_writer_put_byte(struct ssh_writer *writer, u_int8_t value)
{
  ssh_writer_put_data(writer, &value, 1);
}

PUBLIC void ssh_writer_put_bool(struct ssh_writer *writer, int value)
{
  ssh_writer_put_byte(writer, value ? 1 : 0);
}

PUBLIC void ssh_writer_put_u32(struct ssh_writer *writer, u_int32_t value)
{
  u_int8_t buf[4];

  ssh_write_u32_be(buf, value);
  ssh_writer_put_data(writer, buf, sizeof(buf));
}

PUBLIC void ssh_writer_put_string(struct ssh_writer *writer,
                                  const u_int8_t *data, int len)
{
  if (len < 0) {
    writer->error = TRUE;
    return;
  }
  ssh_writer_put_u32(writer, (u_int32_t)len);
  ssh_writer_put_data(writer, data, len);
}

PUBLIC void ssh_writer_put_cstring(struct ssh_writer *writer, const char *text)
{
  ssh_writer_put_string(writer, (const u_int8_t *)text, (int)strlen(text));
}

PUBLIC void ssh_writer_put_mpint(struct ssh_writer *writer,
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

PUBLIC void ssh_reader_init(struct ssh_reader *reader,
                            const u_int8_t *buf, int len)
{
  reader->buf = buf;
  reader->len = len;
  reader->pos = 0;
  reader->error = FALSE;
}

PUBLIC u_int8_t ssh_reader_get_byte(struct ssh_reader *reader)
{
  if (reader->error || reader->pos + 1 > reader->len) {
    reader->error = TRUE;
    return 0;
  }
  return reader->buf[reader->pos++];
}

PUBLIC int ssh_reader_get_bool(struct ssh_reader *reader)
{
  return ssh_reader_get_byte(reader) != 0;
}

PUBLIC u_int32_t ssh_reader_get_u32(struct ssh_reader *reader)
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

PUBLIC void ssh_reader_get_string(struct ssh_reader *reader,
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

PUBLIC int ssh_bytes_equal(const u_int8_t *lhs, int lhs_len, const char *rhs)
{
  int rhs_len = (int)strlen(rhs);
  int i;

  if (lhs_len != rhs_len)
    return FALSE;
  for (i = 0; i < lhs_len; i++) {
    if (lhs[i] != (u_int8_t)rhs[i])
      return FALSE;
  }
  return TRUE;
}

PUBLIC int ssh_namelist_has(const u_int8_t *data, int len, const char *name)
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

PUBLIC void ssh_move_bytes(u_int8_t *dest, const u_int8_t *src, int len)
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

PUBLIC int ssh_try_decode_plain_packet_buffer(u_int8_t *rx_buf, int *rx_len,
                                              u_int32_t *rx_seq,
                                              int packet_plain_max,
                                              u_int8_t *payload,
                                              int *payload_len)
{
  u_int32_t packet_length;
  int padding_len;
  int total_len;

  if (rx_buf == 0 || rx_len == 0 || rx_seq == 0 ||
      payload == 0 || payload_len == 0) {
    return -1;
  }
  if (*rx_len < 4)
    return 0;

  packet_length = ssh_read_u32_be(rx_buf);
  total_len = (int)packet_length + 4;
  if (packet_length < 5 || total_len > *rx_len)
    return 0;
  if (total_len > packet_plain_max)
    return -1;

  padding_len = rx_buf[4];
  if (padding_len < 4 || (int)packet_length - padding_len - 1 < 0)
    return -1;

  *payload_len = (int)packet_length - padding_len - 1;
  memcpy(payload, rx_buf + 5, (size_t)*payload_len);
  ssh_move_bytes(rx_buf, rx_buf + total_len, *rx_len - total_len);
  *rx_len -= total_len;
  (*rx_seq)++;
  return 1;
}
