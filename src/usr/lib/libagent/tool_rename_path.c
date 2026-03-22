#include <agent/tool_handlers.h>
#include <agent/path_utils.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef TEST_BUILD
#include <unistd.h>
#else
#include <debug.h>
#endif

#ifdef TEST_BUILD
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_RENAME_PATH[] =
    "{\"type\":\"object\","
    "\"properties\":{\"from\":{\"type\":\"string\","
    "\"description\":\"Source path to rename. Relative paths use the current directory\"},"
    "\"to\":{\"type\":\"string\","
    "\"description\":\"Destination path to rename to. Relative paths use the current directory\"}},"
    "\"required\":[\"from\",\"to\"]}";

int tool_rename_path(const char *input_json, int input_len,
                     char *result_buf, int result_cap)
{
    char from[AGENT_PATH_MAX];
    char to[AGENT_PATH_MAX];
    struct json_writer jw;

    if (!input_json || !result_buf)
        return -1;

    if (agent_json_get_normalized_path(input_json, input_len,
                                       "from", from, sizeof(from)) < 0) {
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_path",
                                      "from must resolve to a normalized location",
                                      (const char *)0);
    }
    if (agent_json_get_normalized_path(input_json, input_len,
                                       "to", to, sizeof(to)) < 0) {
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_path",
                                      "to must resolve to a normalized location",
                                      (const char *)0);
    }
    if (strcmp(from, to) == 0) {
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_input",
                                      "from and to must differ",
                                      from);
    }

    debug_printf("[TOOL rename_path] %s -> %s\n", from, to);

    if (rename(from, to) < 0) {
        return agent_write_error_json(result_buf, result_cap,
                                      "rename_failed",
                                      "cannot rename path",
                                      from);
    }

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "status");
    jw_string(&jw, "ok");
    jw_key(&jw, "from");
    jw_string(&jw, from);
    jw_key(&jw, "to");
    jw_string(&jw, to);
    jw_object_end(&jw);
    return jw_finish(&jw);
}
