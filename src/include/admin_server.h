#ifndef _ADMIN_SERVER_H
#define _ADMIN_SERVER_H

#include <server_runtime_config.h>
#include <server_audit.h>

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

enum admin_auth_result {
  ADMIN_AUTH_DENY = 0,
  ADMIN_AUTH_ALLOW = 1,
  ADMIN_AUTH_THROTTLED = 2
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
PUBLIC int admin_build_error_response(const char *reason,
                                      u_int32_t retry_after_ticks,
                                      char *response, int response_cap);
PUBLIC int admin_retry_after_seconds(u_int32_t retry_after_ticks);
PUBLIC int admin_role_from_token(const char *token);
PUBLIC int admin_is_source_allowed(u_int32_t peer_addr);
PUBLIC int admin_authorize_peer(u_int32_t peer_addr,
                                u_int32_t *retry_after_ticks);
PUBLIC int admin_authorize_request_detailed(const struct admin_request *req,
                                            u_int32_t peer_addr,
                                            u_int32_t *retry_after_ticks);
PUBLIC int admin_runtime_status_token_enabled(void);
PUBLIC int admin_runtime_control_token_enabled(void);
PUBLIC int admin_runtime_config_error_count(void);
PUBLIC int admin_runtime_debug_shell_enabled(void);
PUBLIC int admin_runtime_debug_shell_port(void);
PUBLIC int admin_runtime_ssh_enabled(void);
PUBLIC int admin_runtime_ssh_port(void);
PUBLIC const char *admin_runtime_ssh_password(void);
PUBLIC int admin_runtime_ssh_signer_port(void);
PUBLIC const char *admin_runtime_ssh_hostkey_ed25519_seed(void);
PUBLIC const char *admin_runtime_ssh_hostkey_ed25519_public(void);
PUBLIC const char *admin_runtime_ssh_hostkey_ed25519_secret(void);
PUBLIC const char *admin_runtime_ssh_rng_seed(void);
PUBLIC int admin_runtime_copy_ssh_config(struct admin_ssh_config *out);
PUBLIC void admin_runtime_audit_line(const char *line);
PUBLIC void admin_runtime_note_listener_ready(int listener_kind);

PUBLIC int http_parse_request(const char *data, int len,
                              struct http_request *out);
PUBLIC int http_map_request(const struct http_request *req,
                            struct admin_request *out);
PUBLIC int http_build_response(int status_code, const char *body,
                               char *response, int response_cap,
                               const char *content_type,
                               int retry_after_seconds);

#ifdef TEST_BUILD
PUBLIC void admin_runtime_set_tokens(const char *status_token,
                                     const char *control_token);
PUBLIC void admin_runtime_set_allow_ip(u_int32_t peer_addr);
PUBLIC void admin_runtime_set_tick(u_int32_t tick);
PUBLIC void admin_runtime_set_agent_running(int running);
PUBLIC void admin_runtime_append_test_audit(const char *message);
PUBLIC int admin_runtime_load_config_text(const char *text, int len);
PUBLIC void admin_runtime_set_debug_shell_port(int port);
PUBLIC void admin_runtime_set_ssh_port(int port);
PUBLIC void admin_runtime_set_ssh_password(const char *password);
PUBLIC void admin_runtime_set_ssh_seeds(const char *hostkey_seed,
                                        const char *rng_seed);
PUBLIC void admin_runtime_set_ssh_raw_hostkey(const char *public_key,
                                              const char *secret_key);
#endif

#endif
