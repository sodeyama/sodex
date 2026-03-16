#ifndef _SSH_AUTH_CORE_H
#define _SSH_AUTH_CORE_H

#ifdef TEST_BUILD
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
#ifndef PRIVATE
#define PRIVATE static
#endif
#else
#include <sodex/const.h>
#include <types.h>
#endif

#define SSH_AUTH_TEXT_MAX 32

struct ssh_auth_request {
  char username[SSH_AUTH_TEXT_MAX];
  char service[SSH_AUTH_TEXT_MAX];
  char method[SSH_AUTH_TEXT_MAX];
  const u_int8_t *password;
  int password_len;
  int change_request;
};

struct ssh_auth_identity {
  int set;
  char username[SSH_AUTH_TEXT_MAX];
  char service[SSH_AUTH_TEXT_MAX];
};

PUBLIC void ssh_auth_identity_reset(struct ssh_auth_identity *identity);
PUBLIC int ssh_auth_parse_service_request(const u_int8_t *payload, int payload_len,
                                          char *service_name, int service_cap);
PUBLIC int ssh_auth_parse_request(const u_int8_t *payload, int payload_len,
                                  struct ssh_auth_request *out);
PUBLIC int ssh_auth_identity_capture(struct ssh_auth_identity *identity,
                                     const struct ssh_auth_request *request);
PUBLIC int ssh_auth_identity_matches(const struct ssh_auth_identity *identity,
                                     const struct ssh_auth_request *request);
PUBLIC int ssh_auth_password_request_matches(const struct ssh_auth_request *request,
                                             const char *username,
                                             const char *service,
                                             const char *expected_password);

#endif
