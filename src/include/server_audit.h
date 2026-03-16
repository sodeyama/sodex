#ifndef _SERVER_AUDIT_H
#define _SERVER_AUDIT_H

#include <server_runtime_config.h>

enum admin_listener_kind {
  ADMIN_LISTENER_ADMIN = 1,
  ADMIN_LISTENER_HTTP = 2,
  ADMIN_LISTENER_SSH = 4,
  ADMIN_LISTENER_DEBUG_SHELL = 8
};

PUBLIC void server_audit_line(const char *line);
PUBLIC void server_audit_note_listener_ready(int listener_kind);

#endif
