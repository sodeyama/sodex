/*
 * tool_write_file.c - write_file tool implementation
 *
 * Writes content to a file on the filesystem.
 */

#include <agent/tool_handlers.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <fs.h>
#include <stdlib.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_WRITE_FILE[] =
    "{\"type\":\"object\","
    "\"properties\":{\"path\":{\"type\":\"string\","
    "\"description\":\"File path to write\"},"
    "\"content\":{\"type\":\"string\","
    "\"description\":\"Content to write\"}},"
    "\"required\":[\"path\",\"content\"]}";

int tool_write_file(const char *input_json, int input_len,
                    char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;
    char path[256];
    static char content[3072];
    int content_len;
    int fd;
    int bytes_written;
    struct json_writer jw;

    if (!input_json || !result_buf)
        return -1;

    /* Parse input JSON */
    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0) {
        debug_printf("[TOOL write_file] JSON parse error: %d\n", ntokens);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    /* Get path */
    tok = json_find_key(input_json, tokens, ntokens, 0, "path");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: path\"}");
    }
    if (json_token_str(input_json, &tokens[tok], path, sizeof(path)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"path too long\"}");
    }

    /* Get content */
    tok = json_find_key(input_json, tokens, ntokens, 0, "content");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: content\"}");
    }
    content_len = json_token_str(input_json, &tokens[tok],
                                 content, sizeof(content));
    if (content_len < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"content too large\"}");
    }

    debug_printf("[TOOL write_file] writing %d bytes to: %s\n",
                 content_len, path);

    /* Open file for writing (create + truncate) */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        debug_printf("[TOOL write_file] open failed: %s\n", path);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"cannot open file: %s\"}", path);
    }

    /* Write content */
    bytes_written = write(fd, content, content_len);
    close(fd);

    if (bytes_written < 0) {
        debug_printf("[TOOL write_file] write failed: %s\n", path);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"write failed: %s\"}", path);
    }

    /* Build result JSON */
    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "status");
    jw_string(&jw, "ok");
    jw_key(&jw, "bytes_written");
    jw_int(&jw, bytes_written);
    jw_object_end(&jw);

    return jw_finish(&jw);
}
