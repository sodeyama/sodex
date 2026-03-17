/*
 * tool_registry.h - Tool registration and lookup
 */
#ifndef _AGENT_TOOL_REGISTRY_H
#define _AGENT_TOOL_REGISTRY_H

#include <json.h>

#define TOOL_MAX_TOOLS      16
#define TOOL_MAX_NAME       64
#define TOOL_MAX_DESC       256
#define TOOL_RESULT_BUF     4096

/* Tool handler function type.
 * input_json: the tool's input parameters as JSON string
 * input_len: length of input_json
 * result_buf: buffer to write result JSON into
 * result_cap: capacity of result_buf
 * Returns: length of result written, or negative on error */
typedef int (*tool_handler_fn)(const char *input_json, int input_len,
                                char *result_buf, int result_cap);

struct tool_def {
    char name[TOOL_MAX_NAME];
    char description[TOOL_MAX_DESC];
    const char *input_schema_json;  /* JSON Schema string (compile-time constant) */
    tool_handler_fn handler;
};

/* Initialize the registry */
void tool_registry_init(void);

/* Register a tool. Returns 0 on success, -1 if full. */
int tool_register(const char *name, const char *description,
                  const char *input_schema_json, tool_handler_fn handler);

/* Find a tool by name. Returns pointer or NULL. */
const struct tool_def *tool_find(const char *name);

/* Get all registered tools. Returns count. */
int tool_list(const struct tool_def **out, int max);

/* Get count of registered tools */
int tool_count(void);

#endif /* _AGENT_TOOL_REGISTRY_H */
