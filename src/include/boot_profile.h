#ifndef _BOOT_PROFILE_H
#define _BOOT_PROFILE_H

#include <sys/types.h>

#define BOOT_PROFILE_VERSION 1U

#define BOOT_PROFILE_TERMINAL_CLASSIC 0U
#define BOOT_PROFILE_TERMINAL_AGENT 1U

struct boot_profile_info {
  u_int32_t version;
  u_int32_t terminal_profile;
  u_int32_t flags;
  u_int32_t reserved;
};

#endif /* _BOOT_PROFILE_H */
