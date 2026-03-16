#ifndef _SSH_PACKET_CORE_H
#define _SSH_PACKET_CORE_H

#ifdef TEST_BUILD
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u_int8_t;
typedef uint32_t u_int32_t;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef PUBLIC
#define PUBLIC
#endif
#else
#include <sodex/const.h>
#include <types.h>
#endif

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

PUBLIC void ssh_write_u32_be(u_int8_t *buf, u_int32_t value);
PUBLIC u_int32_t ssh_read_u32_be(const u_int8_t *buf);
PUBLIC void ssh_writer_init(struct ssh_writer *writer, u_int8_t *buf, int cap);
PUBLIC void ssh_writer_put_data(struct ssh_writer *writer,
                                const u_int8_t *data, int len);
PUBLIC void ssh_writer_put_byte(struct ssh_writer *writer, u_int8_t value);
PUBLIC void ssh_writer_put_bool(struct ssh_writer *writer, int value);
PUBLIC void ssh_writer_put_u32(struct ssh_writer *writer, u_int32_t value);
PUBLIC void ssh_writer_put_string(struct ssh_writer *writer,
                                  const u_int8_t *data, int len);
PUBLIC void ssh_writer_put_cstring(struct ssh_writer *writer, const char *text);
PUBLIC void ssh_writer_put_mpint(struct ssh_writer *writer,
                                 const u_int8_t *data, int len);
PUBLIC void ssh_reader_init(struct ssh_reader *reader,
                            const u_int8_t *buf, int len);
PUBLIC u_int8_t ssh_reader_get_byte(struct ssh_reader *reader);
PUBLIC int ssh_reader_get_bool(struct ssh_reader *reader);
PUBLIC u_int32_t ssh_reader_get_u32(struct ssh_reader *reader);
PUBLIC void ssh_reader_get_string(struct ssh_reader *reader,
                                  const u_int8_t **data, int *len);
PUBLIC int ssh_bytes_equal(const u_int8_t *lhs, int lhs_len, const char *rhs);
PUBLIC int ssh_namelist_has(const u_int8_t *data, int len, const char *name);
PUBLIC void ssh_move_bytes(u_int8_t *dest, const u_int8_t *src, int len);
PUBLIC int ssh_try_decode_plain_packet_buffer(u_int8_t *rx_buf, int *rx_len,
                                              u_int32_t *rx_seq,
                                              int packet_plain_max,
                                              u_int8_t *payload,
                                              int *payload_len);

#endif
