/*
 * tool_manage_process.c - manage_process tool implementation
 *
 * Supports list, info, and kill actions for process management.
 */

#include <agent/tool_handlers.h>
#include <json.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#include <stdlib.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_MANAGE_PROCESS[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"action\":{\"type\":\"string\","
    "\"enum\":[\"list\",\"info\",\"kill\"],"
    "\"description\":\"Action to perform\"},"
    "\"pid\":{\"type\":\"integer\","
    "\"description\":\"Process ID (required for info/kill)\"},"
    "\"signal\":{\"type\":\"integer\","
    "\"description\":\"Signal number for kill (default: 15)\"}"
    "},\"required\":[\"action\"]}";

/* ---- action handlers ---- */

static int handle_list(char *buf, int cap)
{
    struct json_writer jw;

    jw_init(&jw, buf, cap);
    jw_object_start(&jw);

    jw_key(&jw, "action");
    jw_string(&jw, "list");

    jw_key(&jw, "processes");
    jw_array_start(&jw);

#ifndef TEST_BUILD
    /* Sodex does not expose a process table to userland yet.
     * Report a static entry for the agent itself. */
    jw_object_start(&jw);
    jw_key(&jw, "pid");
    jw_int(&jw, 0);
    jw_key(&jw, "name");
    jw_string(&jw, "agent");
    jw_key(&jw, "state");
    jw_string(&jw, "running");
    jw_object_end(&jw);
#else
    /* Test mock: return 2 fake processes */
    jw_object_start(&jw);
    jw_key(&jw, "pid");
    jw_int(&jw, 1);
    jw_key(&jw, "name");
    jw_string(&jw, "init");
    jw_key(&jw, "state");
    jw_string(&jw, "running");
    jw_object_end(&jw);

    jw_object_start(&jw);
    jw_key(&jw, "pid");
    jw_int(&jw, 2);
    jw_key(&jw, "name");
    jw_string(&jw, "agent");
    jw_key(&jw, "state");
    jw_string(&jw, "running");
    jw_object_end(&jw);
#endif

    jw_array_end(&jw);
    jw_object_end(&jw);

    return jw_finish(&jw);
}

static int handle_info(int pid, char *buf, int cap)
{
    struct json_writer jw;

    jw_init(&jw, buf, cap);
    jw_object_start(&jw);

    jw_key(&jw, "action");
    jw_string(&jw, "info");

    jw_key(&jw, "pid");
    jw_int(&jw, pid);

#ifndef TEST_BUILD
    /* Sodex does not expose per-process info to userland yet */
    jw_key(&jw, "error");
    jw_string(&jw, "process info not available in Sodex userland");
#else
    jw_key(&jw, "name");
    jw_string(&jw, "mock_process");
    jw_key(&jw, "state");
    jw_string(&jw, "running");
#endif

    jw_object_end(&jw);
    return jw_finish(&jw);
}

static int handle_kill(int pid, int sig, char *buf, int cap)
{
    struct json_writer jw;
    int result = -1;

    if (sig <= 0)
        sig = 15; /* SIGTERM default */

#ifndef TEST_BUILD
    result = kill(pid, sig);
#else
    result = 0; /* mock success */
#endif

    jw_init(&jw, buf, cap);
    jw_object_start(&jw);

    jw_key(&jw, "action");
    jw_string(&jw, "kill");

    jw_key(&jw, "pid");
    jw_int(&jw, pid);

    jw_key(&jw, "signal");
    jw_int(&jw, sig);

    jw_key(&jw, "success");
    jw_bool(&jw, result == 0 ? 1 : 0);

    jw_object_end(&jw);
    return jw_finish(&jw);
}

/* ---- main entry point ---- */

int tool_manage_process(const char *input_json, int input_len,
                        char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;
    char action[32];
    int pid;
    int sig;

    if (!input_json || input_len <= 0 || !result_buf || result_cap <= 0)
        return -1;

    debug_printf("[TOOL manage_process] input len=%d\n", input_len);

    /* Parse input JSON */
    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0) {
        debug_printf("[TOOL manage_process] JSON parse error: %d\n", ntokens);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    /* Extract "action" string */
    tok = json_find_key(input_json, tokens, ntokens, 0, "action");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: action\"}");
    }
    if (json_token_str(input_json, &tokens[tok], action, sizeof(action)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"action value too long\"}");
    }

    /* Dispatch on action */
    if (strcmp(action, "list") == 0) {
        return handle_list(result_buf, result_cap);
    }

    /* info and kill both require pid */
    tok = json_find_key(input_json, tokens, ntokens, 0, "pid");
    if (tok < 0 || json_token_int(input_json, &tokens[tok], &pid) != 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: pid\"}");
    }

    if (strcmp(action, "info") == 0) {
        return handle_info(pid, result_buf, result_cap);
    }

    if (strcmp(action, "kill") == 0) {
        sig = -1;
        tok = json_find_key(input_json, tokens, ntokens, 0, "signal");
        if (tok >= 0)
            json_token_int(input_json, &tokens[tok], &sig);
        return handle_kill(pid, sig, result_buf, result_cap);
    }

    return snprintf(result_buf, result_cap,
                    "{\"error\":\"unknown action: %s\"}", action);
}
