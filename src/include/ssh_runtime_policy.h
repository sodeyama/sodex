#ifndef _SSH_RUNTIME_POLICY_H
#define _SSH_RUNTIME_POLICY_H

#define SSH_AUTH_RETRY_LIMIT 3

static inline int ssh_auth_failure_should_close(int failed_attempts)
{
  return failed_attempts >= SSH_AUTH_RETRY_LIMIT;
}

static inline const char *ssh_timeout_close_reason(int auth_done)
{
  return auth_done ? "idle_timeout" : "auth_timeout";
}

static inline const char *ssh_timeout_audit_label(int auth_done)
{
  return auth_done ? "ssh_idle_timeout_ticks" : "ssh_auth_timeout_ticks";
}

#endif
