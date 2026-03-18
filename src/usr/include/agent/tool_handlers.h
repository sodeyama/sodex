/*
 * tool_handlers.h - Built-in tool handler declarations
 */
#ifndef _AGENT_TOOL_HANDLERS_H
#define _AGENT_TOOL_HANDLERS_H

#include <agent/tool_registry.h>

int tool_read_file(const char *input_json, int input_len,
                   char *result_buf, int result_cap);
int tool_write_file(const char *input_json, int input_len,
                    char *result_buf, int result_cap);
int tool_list_dir(const char *input_json, int input_len,
                  char *result_buf, int result_cap);
int tool_get_system_info(const char *input_json, int input_len,
                         char *result_buf, int result_cap);
int tool_run_command(const char *input_json, int input_len,
                     char *result_buf, int result_cap);
int tool_manage_process(const char *input_json, int input_len,
                        char *result_buf, int result_cap);

/* Initialize and register all built-in tools */
void tool_init(void);

/* JSON Schema strings (compile-time constants) */
extern const char TOOL_SCHEMA_READ_FILE[];
extern const char TOOL_SCHEMA_WRITE_FILE[];
extern const char TOOL_SCHEMA_LIST_DIR[];
extern const char TOOL_SCHEMA_GET_SYSTEM_INFO[];
extern const char TOOL_SCHEMA_RUN_COMMAND[];
extern const char TOOL_SCHEMA_MANAGE_PROCESS[];

#endif /* _AGENT_TOOL_HANDLERS_H */
