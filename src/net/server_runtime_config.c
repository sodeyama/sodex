#include <server_runtime_config.h>
#include <admin_server.h>

PUBLIC void server_runtime_reset(void)
{
  admin_runtime_reset();
}

PUBLIC int server_runtime_status_token_enabled(void)
{
  return admin_runtime_status_token_enabled();
}

PUBLIC int server_runtime_control_token_enabled(void)
{
  return admin_runtime_control_token_enabled();
}

PUBLIC int server_runtime_config_error_count(void)
{
  return admin_runtime_config_error_count();
}

PUBLIC int server_runtime_debug_shell_enabled(void)
{
  return admin_runtime_debug_shell_enabled();
}

PUBLIC int server_runtime_debug_shell_port(void)
{
  return admin_runtime_debug_shell_port();
}

PUBLIC int server_runtime_ssh_enabled(void)
{
  return admin_runtime_ssh_enabled();
}

PUBLIC int server_runtime_ssh_port(void)
{
  return admin_runtime_ssh_port();
}

PUBLIC const char *server_runtime_ssh_password(void)
{
  return admin_runtime_ssh_password();
}

PUBLIC int server_runtime_ssh_signer_port(void)
{
  return admin_runtime_ssh_signer_port();
}

PUBLIC const char *server_runtime_ssh_hostkey_ed25519_seed(void)
{
  return admin_runtime_ssh_hostkey_ed25519_seed();
}

PUBLIC const char *server_runtime_ssh_hostkey_ed25519_public(void)
{
  return admin_runtime_ssh_hostkey_ed25519_public();
}

PUBLIC const char *server_runtime_ssh_hostkey_ed25519_secret(void)
{
  return admin_runtime_ssh_hostkey_ed25519_secret();
}

PUBLIC const char *server_runtime_ssh_rng_seed(void)
{
  return admin_runtime_ssh_rng_seed();
}

PUBLIC int server_runtime_copy_ssh_config(struct admin_ssh_config *out)
{
  return admin_runtime_copy_ssh_config(out);
}

#ifdef TEST_BUILD
PUBLIC void server_runtime_set_debug_shell_port(int port)
{
  admin_runtime_set_debug_shell_port(port);
}

PUBLIC void server_runtime_set_ssh_port(int port)
{
  admin_runtime_set_ssh_port(port);
}

PUBLIC void server_runtime_set_ssh_password(const char *password)
{
  admin_runtime_set_ssh_password(password);
}

PUBLIC void server_runtime_set_ssh_seeds(const char *hostkey_seed,
                                         const char *rng_seed)
{
  admin_runtime_set_ssh_seeds(hostkey_seed, rng_seed);
}

PUBLIC void server_runtime_set_ssh_raw_hostkey(const char *public_key,
                                               const char *secret_key)
{
  admin_runtime_set_ssh_raw_hostkey(public_key, secret_key);
}

PUBLIC int server_runtime_load_config_text(const char *text, int len)
{
  return admin_runtime_load_config_text(text, len);
}
#endif
