/*
 * tool_read_file.c - read_file tool implementation
 *
 * Reads a file from the filesystem and returns its content as JSON.
 */

#include <agent/tool_handlers.h>
#include <agent/bounded_output.h>
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

const char TOOL_SCHEMA_READ_FILE[] =
    "{\"type\":\"object\","
    "\"properties\":{\"path\":{\"type\":\"string\","
    "\"description\":\"File path to read\"}},"
    "\"required\":[\"path\"]}";

int tool_read_file(const char *input_json, int input_len,
                   char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;
    char path[256];
    int fd;
    int bytes_read;
    struct json_writer jw;
    static struct bounded_output bounded;

    if (!input_json || !result_buf)
        return -1;

    /* Parse input JSON to get path */
    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0) {
        debug_printf("[TOOL read_file] JSON parse error: %d\n", ntokens);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    tok = json_find_key(input_json, tokens, ntokens, 0, "path");
    if (tok < 0) {
        debug_printf("[TOOL read_file] missing 'path' field\n");
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: path\"}");
    }

    if (json_token_str(input_json, &tokens[tok], path, sizeof(path)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"path too long\"}");
    }

    debug_printf("[TOOL read_file] reading: %s\n", path);

    /* Open the file */
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        debug_printf("[TOOL read_file] open failed: %s\n", path);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"file not found: %s\"}", path);
    }

    bounded_output_init(&bounded);
    bounded_output_begin_artifact(&bounded, "read", ".txt");
    bytes_read = 0;
    for (;;) {
        char chunk[512];
        int ret = read(fd, chunk, sizeof(chunk));

        if (ret < 0) {
            close(fd);
            bounded_output_finish(&bounded, 0);
            debug_printf("[TOOL read_file] read failed: %s\n", path);
            return snprintf(result_buf, result_cap,
                            "{\"error\":\"read failed: %s\"}", path);
        }
        if (ret == 0)
            break;
        bounded_output_append(&bounded, chunk, ret);
        bytes_read += ret;
    }
    close(fd);
    bounded_output_finish(&bounded, bounded.total_bytes > AGENT_BOUNDED_INLINE);

    /* Build result JSON */
    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "path");
    jw_string(&jw, path);
    bounded_output_write_json(&bounded, &jw,
                              "content",
                              "content_head",
                              "content_tail");
    jw_key(&jw, "bytes_read");
    jw_int(&jw, bytes_read);
    jw_object_end(&jw);

    return jw_finish(&jw);
}
