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

/* ---- Tool statistics ---- */

#define TOOL_STATS_MAX  16

struct tool_stat {
    char name[64];
    int  call_count;
    int  success_count;
    int  error_count;
    int  total_ticks;     /* cumulative execution time in PIT ticks */
};

struct tool_stats {
    struct tool_stat entries[TOOL_STATS_MAX];
    int count;
};

/* Reset all tool statistics */
void tool_stats_reset(struct tool_stats *stats);

/* Record a tool execution */
void tool_stats_record(struct tool_stats *stats,
                       const char *tool_name,
                       int is_error, int elapsed_ticks);

/* Print tool statistics summary to serial */
void tool_stats_print(const struct tool_stats *stats);

#endif /* _AGENT_TOOL_DISPATCH_H */
