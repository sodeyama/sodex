#ifndef _SSH_RUNTIME_POLICY_H
#define _SSH_RUNTIME_POLICY_H

#ifdef TEST_BUILD
#include <stdint.h>
typedef uint32_t u_int32_t;
#endif

#define SSH_AUTH_RETRY_LIMIT 3
#define SSH_AUTH_TIMEOUT_TICKS 1000U
#define SSH_NO_CHANNEL_TIMEOUT_TICKS 2000U
#define SSH_IDLE_TIMEOUT_TICKS 6000U
#define SSH_AUTH_FAILURE_DELAY_TICKS 20U

#define SSH_TIMEOUT_PHASE_AUTH 0
#define SSH_TIMEOUT_PHASE_NO_CHANNEL 1
#define SSH_TIMEOUT_PHASE_IDLE 2

static inline int ssh_auth_failure_should_close(int failed_attempts)
{
  return failed_attempts >= SSH_AUTH_RETRY_LIMIT;
}

static inline u_int32_t ssh_auth_failure_delay_until(u_int32_t now_ticks)
{
  return now_ticks + SSH_AUTH_FAILURE_DELAY_TICKS;
}

static inline int ssh_auth_failure_delay_pending(u_int32_t now_ticks,
                                                 u_int32_t delay_until_tick)
{
  return delay_until_tick != 0 &&
         (int)(delay_until_tick - now_ticks) > 0;
}

static inline int ssh_timeout_phase(int auth_done,
                                    int channel_open,
                                    int shell_running)
{
  if (!auth_done)
    return SSH_TIMEOUT_PHASE_AUTH;
  if (!channel_open || !shell_running)
    return SSH_TIMEOUT_PHASE_NO_CHANNEL;
  return SSH_TIMEOUT_PHASE_IDLE;
}

static inline u_int32_t ssh_timeout_ticks_for_phase(int phase)
{
  switch (phase) {
  case SSH_TIMEOUT_PHASE_AUTH:
    return SSH_AUTH_TIMEOUT_TICKS;
  case SSH_TIMEOUT_PHASE_NO_CHANNEL:
    return SSH_NO_CHANNEL_TIMEOUT_TICKS;
  default:
    return SSH_IDLE_TIMEOUT_TICKS;
  }
}

static inline const char *ssh_timeout_close_reason_for_phase(int phase)
{
  switch (phase) {
  case SSH_TIMEOUT_PHASE_AUTH:
    return "auth_timeout";
  case SSH_TIMEOUT_PHASE_NO_CHANNEL:
    return "no_channel_timeout";
  default:
    return "idle_timeout";
  }
}

static inline const char *ssh_timeout_audit_label_for_phase(int phase)
{
  switch (phase) {
  case SSH_TIMEOUT_PHASE_AUTH:
    return "ssh_auth_timeout_ticks";
  case SSH_TIMEOUT_PHASE_NO_CHANNEL:
    return "ssh_no_channel_timeout_ticks";
  default:
    return "ssh_idle_timeout_ticks";
  }
}

static inline const char *ssh_timeout_close_reason(int auth_done)
{
  return ssh_timeout_close_reason_for_phase(auth_done ?
                                            SSH_TIMEOUT_PHASE_IDLE :
                                            SSH_TIMEOUT_PHASE_AUTH);
}

static inline const char *ssh_timeout_audit_label(int auth_done)
{
  return ssh_timeout_audit_label_for_phase(auth_done ?
                                           SSH_TIMEOUT_PHASE_IDLE :
                                           SSH_TIMEOUT_PHASE_AUTH);
}

#endif
