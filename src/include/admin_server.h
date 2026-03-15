#ifndef _ADMIN_SERVER_H
#define _ADMIN_SERVER_H

#ifdef TEST_BUILD
#include <stdint.h>
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
#ifndef PUBLIC
#define PUBLIC
#endif
#else
#include <sodex/const.h>
#include <sys/types.h>
#endif

#define SODEX_ADMIN_PORT 10023
#define SODEX_HTTP_PORT 8080
#define SODEX_ADMIN_CONFIG_PATH "/etc/sodex-admin.conf"

#define ADMIN_TOKEN_MAX 64
#define ADMIN_TEXT_REQUEST_MAX 192
#define ADMIN_HTTP_REQUEST_MAX 384
#define ADMIN_RESPONSE_MAX 512

enum admin_role {
  ADMIN_ROLE_NONE = 0,
  ADMIN_ROLE_HEALTH = 1,
  ADMIN_ROLE_STATUS = 2,
  ADMIN_ROLE_CONTROL = 3
};

enum admin_action {
  ADMIN_ACTION_INVALID = 0,
  ADMIN_ACTION_PING,
  ADMIN_ACTION_HEALTH,
  ADMIN_ACTION_STATUS,
  ADMIN_ACTION_AGENT_START,
  ADMIN_ACTION_AGENT_STOP,
  ADMIN_ACTION_LOG_TAIL
};

struct admin_request {
  int action;
  int required_role;
  int arg;
  char token[ADMIN_TOKEN_MAX];
};

struct http_request {
  char method[8];
  char path[64];
  char token[ADMIN_TOKEN_MAX];
};

PUBLIC void admin_server_init(void);
PUBLIC void admin_server_tick(void);
PUBLIC void http_server_init(void);
PUBLIC void http_server_tick(void);

PUBLIC void admin_runtime_reset(void);
PUBLIC int admin_parse_command(const char *line, int len,
                               struct admin_request *out);
PUBLIC int admin_authorize_request(const struct admin_request *req,
                                   u_int32_t peer_addr);
PUBLIC int admin_execute_request(const struct admin_request *req,
                                 char *response, int response_cap,
                                 int json_mode);
PUBLIC int admin_role_from_token(const char *token);
PUBLIC int admin_is_source_allowed(u_int32_t peer_addr);

PUBLIC int http_parse_request(const char *data, int len,
                              struct http_request *out);
PUBLIC int http_map_request(const struct http_request *req,
                            struct admin_request *out);
PUBLIC int http_build_response(int status_code, const char *body,
                               char *response, int response_cap,
                               const char *content_type);

#ifdef TEST_BUILD
PUBLIC void admin_runtime_set_tokens(const char *status_token,
                                     const char *control_token);
PUBLIC void admin_runtime_set_allow_ip(u_int32_t peer_addr);
PUBLIC void admin_runtime_set_tick(u_int32_t tick);
PUBLIC void admin_runtime_set_agent_running(int running);
PUBLIC void admin_runtime_append_test_audit(const char *message);
PUBLIC int admin_runtime_load_config_text(const char *text, int len);
#endif

#endif
