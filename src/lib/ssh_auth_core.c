#include <ssh_auth_core.h>

#include <ssh_packet_core.h>

#include <string.h>

PRIVATE int ssh_auth_copy_text(char *dest, int cap,
                               const u_int8_t *src, int len)
{
  int i;

  if (dest == 0 || cap <= 0 || src == 0 || len < 0)
    return -1;
  if (len >= cap)
    return -1;
  for (i = 0; i < len; i++) {
    dest[i] = (char)src[i];
  }
  dest[len] = '\0';
  return 0;
}

PUBLIC void ssh_auth_identity_reset(struct ssh_auth_identity *identity)
{
  if (identity == 0)
    return;
  memset(identity, 0, sizeof(*identity));
}

PUBLIC int ssh_auth_parse_service_request(const u_int8_t *payload, int payload_len,
                                          char *service_name, int service_cap)
{
  struct ssh_reader reader;
  const u_int8_t *name = 0;
  int name_len = 0;

  if (payload == 0 || payload_len <= 1)
    return -1;
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &name, &name_len);
  if (reader.error)
    return -1;
  return ssh_auth_copy_text(service_name, service_cap, name, name_len);
}

PUBLIC int ssh_auth_parse_request(const u_int8_t *payload, int payload_len,
                                  struct ssh_auth_request *out)
{
  struct ssh_reader reader;
  const u_int8_t *username = 0;
  const u_int8_t *service = 0;
  const u_int8_t *method = 0;
  const u_int8_t *password = 0;
  int username_len = 0;
  int service_len = 0;
  int method_len = 0;
  int password_len = 0;

  if (payload == 0 || payload_len <= 1 || out == 0)
    return -1;

  memset(out, 0, sizeof(*out));
  ssh_reader_init(&reader, payload + 1, payload_len - 1);
  ssh_reader_get_string(&reader, &username, &username_len);
  ssh_reader_get_string(&reader, &service, &service_len);
  ssh_reader_get_string(&reader, &method, &method_len);
  if (reader.error)
    return -1;
  if (ssh_auth_copy_text(out->username, sizeof(out->username),
                         username, username_len) < 0) {
    return -1;
  }
  if (ssh_auth_copy_text(out->service, sizeof(out->service),
                         service, service_len) < 0) {
    return -1;
  }
  if (ssh_auth_copy_text(out->method, sizeof(out->method),
                         method, method_len) < 0) {
    return -1;
  }
  if (strcmp(out->method, "password") != 0)
    return 0;
  out->change_request = ssh_reader_get_bool(&reader);
  ssh_reader_get_string(&reader, &password, &password_len);
  if (reader.error)
    return -1;
  out->password = password;
  out->password_len = password_len;
  return 0;
}

PUBLIC int ssh_auth_identity_capture(struct ssh_auth_identity *identity,
                                     const struct ssh_auth_request *request)
{
  if (identity == 0 || request == 0)
    return -1;
  if (!identity->set) {
    if (ssh_auth_copy_text(identity->username, sizeof(identity->username),
                           (const u_int8_t *)request->username,
                           (int)strlen(request->username)) < 0) {
      return -1;
    }
    if (ssh_auth_copy_text(identity->service, sizeof(identity->service),
                           (const u_int8_t *)request->service,
                           (int)strlen(request->service)) < 0) {
      return -1;
    }
    identity->set = TRUE;
  }
  return 0;
}

PUBLIC int ssh_auth_identity_matches(const struct ssh_auth_identity *identity,
                                     const struct ssh_auth_request *request)
{
  if (identity == 0 || request == 0 || !identity->set)
    return TRUE;
  return strcmp(identity->username, request->username) == 0 &&
         strcmp(identity->service, request->service) == 0;
}

PUBLIC int ssh_auth_password_request_matches(const struct ssh_auth_request *request,
                                             const char *username,
                                             const char *service,
                                             const char *expected_password)
{
  int expected_len;
  int i;

  if (request == 0 || username == 0 || service == 0 || expected_password == 0)
    return FALSE;
  if (request->change_request)
    return FALSE;
  if (strcmp(request->method, "password") != 0)
    return FALSE;
  if (strcmp(request->username, username) != 0)
    return FALSE;
  if (strcmp(request->service, service) != 0)
    return FALSE;

  expected_len = (int)strlen(expected_password);
  if (request->password_len != expected_len)
    return FALSE;
  for (i = 0; i < request->password_len; i++) {
    if (request->password[i] != (u_int8_t)expected_password[i])
      return FALSE;
  }
  return TRUE;
}
