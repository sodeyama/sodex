/*
 * tool_run_command.c - run_command tool implementation
 *
 * Executes commands on the Sodex OS. Currently supports a set of
 * built-in commands since full shell pipe+capture is not yet available.
 */

#include <agent/tool_handlers.h>
#include <json.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_RUN_COMMAND[] =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"Command to execute\"}},"
    "\"required\":[\"command\"]}";

/* Handle built-in commands that we can respond to without execve */
static int handle_builtin(const char *cmd, struct json_writer *jw)
{
    if (strcmp(cmd, "uname") == 0 || strcmp(cmd, "uname -a") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "Sodex 1.0 i486 (custom kernel)");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    if (strcmp(cmd, "uptime") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "uptime: information not available via syscall");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    if (strcmp(cmd, "whoami") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "root");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    if (strcmp(cmd, "arch") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "i486");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    if (strcmp(cmd, "hostname") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "sodex");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    if (strcmp(cmd, "pwd") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "/");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    if (strcmp(cmd, "help") == 0) {
        jw_object_start(jw);
        jw_key(jw, "output");
        jw_string(jw, "Built-in commands: uname, uptime, whoami, "
                       "arch, hostname, pwd, help");
        jw_key(jw, "exit_code");
        jw_int(jw, 0);
        jw_object_end(jw);
        return 0;
    }

    return -1;  /* Not a builtin */
}

int tool_run_command(const char *input_json, int input_len,
                     char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;
    char command[512];
    struct json_writer jw;

    if (!input_json || !result_buf)
        return -1;

    /* Parse input JSON */
    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    tok = json_find_key(input_json, tokens, ntokens, 0, "command");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: command\"}");
    }
    if (json_token_str(input_json, &tokens[tok], command, sizeof(command)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"command too long\"}");
    }

    debug_printf("[TOOL run_command] command: %s\n", command);

    jw_init(&jw, result_buf, result_cap);

    /* Try built-in commands first */
    if (handle_builtin(command, &jw) == 0) {
        return jw_finish(&jw);
    }

    /* Command not recognized as builtin - return informative message */
    jw_object_start(&jw);
    jw_key(&jw, "output");
    jw_string(&jw, "");
    jw_key(&jw, "error");
    jw_string(&jw, "command execution with output capture is not yet "
                    "supported on Sodex. Use read_file/write_file/list_dir "
                    "tools for filesystem operations.");
    jw_key(&jw, "command");
    jw_string(&jw, command);
    jw_key(&jw, "exit_code");
    jw_int(&jw, 127);
    jw_object_end(&jw);

    return jw_finish(&jw);
}
