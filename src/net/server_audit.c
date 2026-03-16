#include <server_audit.h>
#include <admin_server.h>

PUBLIC void server_audit_line(const char *line)
{
  admin_runtime_audit_line(line);
}

PUBLIC void server_audit_note_listener_ready(int listener_kind)
{
  admin_runtime_note_listener_ready(listener_kind);
}
