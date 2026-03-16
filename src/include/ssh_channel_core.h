#ifndef _SSH_CHANNEL_CORE_H
#define _SSH_CHANNEL_CORE_H

#ifdef TEST_BUILD
#include <stdint.h>
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
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
#ifndef PRIVATE
#define PRIVATE static
#endif
#else
#include <sodex/const.h>
#include <types.h>
#endif

#define SSH_CHANNEL_TYPE_MAX 16
#define SSH_CHANNEL_REQUEST_UNKNOWN 0
#define SSH_CHANNEL_REQUEST_PTY 1
#define SSH_CHANNEL_REQUEST_WINDOW_CHANGE 2
#define SSH_CHANNEL_REQUEST_SHELL 3

struct ssh_channel_open_request {
  char type[SSH_CHANNEL_TYPE_MAX];
  u_int32_t peer_id;
  u_int32_t peer_window;
  u_int32_t peer_max_packet;
};

struct ssh_channel_request {
  int kind;
  u_int32_t recipient;
  int want_reply;
  u_int16_t cols;
  u_int16_t rows;
};

struct ssh_channel_data_request {
  u_int32_t recipient;
  const u_int8_t *data;
  int data_len;
};

struct ssh_channel_close_plan {
  int send_exit_status;
  int send_eof;
  int send_close;
};

PUBLIC int ssh_channel_parse_open(const u_int8_t *payload, int payload_len,
                                  struct ssh_channel_open_request *out);
PUBLIC int ssh_channel_parse_request(const u_int8_t *payload, int payload_len,
                                     struct ssh_channel_request *out);
PUBLIC int ssh_channel_parse_data(const u_int8_t *payload, int payload_len,
                                  struct ssh_channel_data_request *out);
PUBLIC void ssh_channel_plan_shutdown(int exit_status_sent,
                                      int eof_sent,
                                      int close_sent,
                                      struct ssh_channel_close_plan *plan);

#endif
