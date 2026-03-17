/*
 * tool_dispatch.h - Tool dispatch from Claude API tool_use blocks
 */
#ifndef _AGENT_TOOL_DISPATCH_H
#define _AGENT_TOOL_DISPATCH_H

#include <agent/claude_adapter.h>
#include <agent/tool_registry.h>

/* Dispatch result */
struct tool_dispatch_result {
    char tool_use_id[64];
    char result_json[TOOL_RESULT_BUF];
    int result_len;
    int is_error;
};

/* Execute a tool_use block. Looks up handler in registry and calls it.
 * Returns 0 on success, -1 if tool not found. */
int tool_dispatch(const struct claude_tool_use *tu,
                  struct tool_dispatch_result *out);

/* Execute all tool_use blocks in a response.
 * Returns number of tools executed. */
int tool_dispatch_all(const struct claude_response *resp,
                      struct tool_dispatch_result *results, int max_results);

/* Build tools JSON array for Claude API request.
 * Writes the "tools" array content into the json_writer. */
int tool_build_definitions(struct json_writer *jw);

#endif /* _AGENT_TOOL_DISPATCH_H */
