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

static int result_has_error_field(const char *result_json, int result_len)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int token_count;

    if (!result_json || result_len <= 0)
        return 0;

    json_init(&jp);
    token_count = json_parse(&jp, result_json, result_len, tokens, 32);
    if (token_count < 0)
        return 0;
    return json_find_key(result_json, tokens, token_count, 0, "error") >= 0;
}

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
    out->is_error = result_has_error_field(out->result_json, result_len);
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

/* ---- Tool statistics ---- */

void tool_stats_reset(struct tool_stats *stats)
{
    if (!stats)
        return;
    memset(stats, 0, sizeof(*stats));
}

void tool_stats_record(struct tool_stats *stats,
                       const char *tool_name,
                       int is_error, int elapsed_ticks)
{
    int i;
    struct tool_stat *entry;

    if (!stats || !tool_name)
        return;

    /* Find existing entry */
    entry = (struct tool_stat *)0;
    for (i = 0; i < stats->count; i++) {
        if (strcmp(stats->entries[i].name, tool_name) == 0) {
            entry = &stats->entries[i];
            break;
        }
    }

    /* Create new entry if not found */
    if (!entry) {
        if (stats->count >= TOOL_STATS_MAX)
            return;
        entry = &stats->entries[stats->count];
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->name, tool_name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        stats->count++;
    }

    entry->call_count++;
    if (is_error)
        entry->error_count++;
    else
        entry->success_count++;
    if (elapsed_ticks > 0)
        entry->total_ticks += elapsed_ticks;
}

void tool_stats_print(const struct tool_stats *stats)
{
    int i;

    if (!stats || stats->count == 0) {
        debug_printf("[TOOL-STATS] no tool calls recorded\n");
        return;
    }

    debug_printf("[TOOL-STATS] ---- Tool Statistics ----\n");
    for (i = 0; i < stats->count; i++) {
        const struct tool_stat *e = &stats->entries[i];
        int avg_ticks = 0;

        if (e->call_count > 0)
            avg_ticks = e->total_ticks / e->call_count;

        debug_printf("[TOOL-STATS] %s: calls=%d ok=%d err=%d avg_ticks=%d\n",
                     e->name, e->call_count, e->success_count,
                     e->error_count, avg_ticks);
    }
    debug_printf("[TOOL-STATS] ---- End ----\n");
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
