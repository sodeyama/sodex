/*
 * tool_dispatch.c - Tool dispatch from Claude API tool_use blocks
 *
 * Looks up tools by name in the registry and executes their handlers.
 * Also builds the tools definition array for Claude API requests.
 */

#include <agent/tool_dispatch.h>
#include <agent/tool_registry.h>
#include <agent/claude_adapter.h>
#include <json.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

int tool_dispatch(const struct claude_tool_use *tu,
                  struct tool_dispatch_result *out)
{
    const struct tool_def *td;
    int result_len;

    if (!tu || !out)
        return -1;

    memset(out, 0, sizeof(*out));

    /* Copy tool_use_id */
    strncpy(out->tool_use_id, tu->id, sizeof(out->tool_use_id) - 1);
    out->tool_use_id[sizeof(out->tool_use_id) - 1] = '\0';

    /* Look up tool */
    td = tool_find(tu->name);
    if (!td) {
        debug_printf("[TOOL] tool not found: '%s'\n", tu->name);
        /* Return error result */
        result_len = snprintf(out->result_json, TOOL_RESULT_BUF,
                              "{\"error\":\"tool not found: %s\"}", tu->name);
        out->result_len = result_len;
        out->is_error = 1;
        return -1;
    }

    debug_printf("[TOOL] dispatching '%s' (id=%s)\n", tu->name, tu->id);

    /* Call handler */
    result_len = td->handler(tu->input_json, tu->input_json_len,
                             out->result_json, TOOL_RESULT_BUF);

    if (result_len < 0) {
        /* Handler returned error */
        debug_printf("[TOOL] handler error for '%s': %d\n", tu->name, result_len);
        result_len = snprintf(out->result_json, TOOL_RESULT_BUF,
                              "{\"error\":\"tool execution failed\"}");
        out->result_len = result_len;
        out->is_error = 1;
        return 0;  /* dispatch succeeded, tool failed */
    }

    out->result_len = result_len;
    out->is_error = 0;
    debug_printf("[TOOL] '%s' completed, result_len=%d\n", tu->name, result_len);
    return 0;
}

int tool_dispatch_all(const struct claude_response *resp,
                      struct tool_dispatch_result *results, int max_results)
{
    int i;
    int executed = 0;

    if (!resp || !results || max_results <= 0)
        return 0;

    for (i = 0; i < resp->block_count && executed < max_results; i++) {
        if (resp->blocks[i].type == CLAUDE_CONTENT_TOOL_USE) {
            tool_dispatch(&resp->blocks[i].tool_use, &results[executed]);
            executed++;
        }
    }

    debug_printf("[TOOL] dispatched %d tool calls\n", executed);
    return executed;
}

int tool_build_definitions(struct json_writer *jw)
{
    const struct tool_def *tools[TOOL_MAX_TOOLS];
    int count;
    int i;

    if (!jw)
        return -1;

    count = tool_list(tools, TOOL_MAX_TOOLS);

    jw_array_start(jw);

    for (i = 0; i < count; i++) {
        jw_object_start(jw);

        jw_key(jw, "name");
        jw_string(jw, tools[i]->name);

        jw_key(jw, "description");
        jw_string(jw, tools[i]->description);

        jw_key(jw, "input_schema");
        if (tools[i]->input_schema_json) {
            int slen = strlen(tools[i]->input_schema_json);
            jw_raw(jw, tools[i]->input_schema_json, slen);
        } else {
            /* Empty schema */
            jw_raw(jw, "{\"type\":\"object\",\"properties\":{}}", 33);
        }

        jw_object_end(jw);
    }

    jw_array_end(jw);

    return count;
}
