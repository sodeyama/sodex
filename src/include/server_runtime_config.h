#ifndef _SERVER_RUNTIME_CONFIG_H
#define _SERVER_RUNTIME_CONFIG_H

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
#define SODEX_SERVER_RUNTIME_CONFIG_PATH "/etc/sodex-admin.conf"
#define SODEX_ADMIN_CONFIG_PATH SODEX_SERVER_RUNTIME_CONFIG_PATH

#define ADMIN_TOKEN_MAX 64
#define ADMIN_SECRET_MAX 192
#define ADMIN_HEX_SEED_MAX 65
#define ADMIN_HEX_PUBLICKEY_MAX 65
#define ADMIN_HEX_SECRETKEY_MAX 129

struct admin_ssh_config {
  u_int32_t allow_ip;
  u_int16_t ssh_port;
  u_int16_t ssh_signer_port;
  char ssh_password[ADMIN_SECRET_MAX];
  char ssh_hostkey_ed25519_seed[ADMIN_HEX_SEED_MAX];
  char ssh_hostkey_ed25519_public[ADMIN_HEX_PUBLICKEY_MAX];
  char ssh_hostkey_ed25519_secret[ADMIN_HEX_SECRETKEY_MAX];
  char ssh_rng_seed[ADMIN_HEX_SEED_MAX];
};

PUBLIC void server_runtime_reset(void);
PUBLIC int server_runtime_status_token_enabled(void);
PUBLIC int server_runtime_control_token_enabled(void);
PUBLIC int server_runtime_config_error_count(void);
PUBLIC int server_runtime_debug_shell_enabled(void);
PUBLIC int server_runtime_debug_shell_port(void);
PUBLIC int server_runtime_ssh_enabled(void);
PUBLIC int server_runtime_ssh_port(void);
PUBLIC const char *server_runtime_ssh_password(void);
PUBLIC int server_runtime_ssh_signer_port(void);
PUBLIC const char *server_runtime_ssh_hostkey_ed25519_seed(void);
PUBLIC const char *server_runtime_ssh_hostkey_ed25519_public(void);
PUBLIC const char *server_runtime_ssh_hostkey_ed25519_secret(void);
PUBLIC const char *server_runtime_ssh_rng_seed(void);
PUBLIC int server_runtime_copy_ssh_config(struct admin_ssh_config *out);

#ifdef TEST_BUILD
PUBLIC void server_runtime_set_debug_shell_port(int port);
PUBLIC void server_runtime_set_ssh_port(int port);
PUBLIC void server_runtime_set_ssh_password(const char *password);
PUBLIC void server_runtime_set_ssh_seeds(const char *hostkey_seed,
                                         const char *rng_seed);
PUBLIC void server_runtime_set_ssh_raw_hostkey(const char *public_key,
                                               const char *secret_key);
PUBLIC int server_runtime_load_config_text(const char *text, int len);
#endif

#endif
