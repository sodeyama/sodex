#ifndef _DEBUG_SHELL_SERVER_H
#define _DEBUG_SHELL_SERVER_H

#include <admin_server.h>

PUBLIC int debug_shell_parse_preface(const char *line, int len,
                                     char *token, int token_cap);
PUBLIC void debug_shell_server_init(void);
PUBLIC void debug_shell_server_tick(void);

#endif
