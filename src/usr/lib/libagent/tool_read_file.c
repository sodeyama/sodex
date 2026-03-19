/*
 * tool_read_file.c - read_file tool implementation
 *
 * Reads a file from the filesystem and returns its content as JSON.
 */

#include <agent/tool_handlers.h>
#include <agent/bounded_output.h>
#include <agent/path_utils.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <fs.h>
#include <stdlib.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#else
#include <debug.h>
#endif

#ifdef TEST_BUILD
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_READ_FILE[] =
    "{\"type\":\"object\","
    "\"properties\":{\"path\":{\"type\":\"string\","
    "\"description\":\"File path to read. Relative paths use the current directory\"},"
    "\"offset\":{\"type\":\"integer\","
    "\"description\":\"Byte offset to start reading from\"},"
    "\"limit\":{\"type\":\"integer\","
    "\"description\":\"Maximum bytes to read (default 4096, max 65536)\"}},"
    "\"required\":[\"path\"]}";

int tool_read_file(const char *input_json, int input_len,
                   char *result_buf, int result_cap)
{
    char path[AGENT_PATH_MAX];
    int fd;
    int bytes_read;
    struct json_writer jw;
    static struct bounded_output bounded;
    int offset = 0;
    int limit = AGENT_FILE_READ_LIMIT_DEF;
    int limit_reached = 0;
    int file_has_more = 0;

    if (!input_json || !result_buf)
        return -1;

    if (agent_json_get_normalized_path(input_json, input_len,
                                       "path", path, sizeof(path)) < 0)
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_path",
                                      "path must resolve to a normalized location",
                                      (const char *)0);
    if (agent_json_get_int_field(input_json, input_len, "offset", &offset) == 0 &&
        offset < 0)
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_input",
                                      "offset must be >= 0",
                                      path);
    if (agent_json_get_int_field(input_json, input_len, "limit", &limit) == 0) {
        if (limit <= 0 || limit > AGENT_FILE_READ_LIMIT_MAX)
            return agent_write_error_json(result_buf, result_cap,
                                          "too_large",
                                          "limit is out of range",
                                          path);
    } else {
        limit = AGENT_FILE_READ_LIMIT_DEF;
    }

    debug_printf("[TOOL read_file] reading: %s\n", path);

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        debug_printf("[TOOL read_file] open failed: %s\n", path);
        return agent_write_error_json(result_buf, result_cap,
                                      "not_found",
                                      "cannot open file",
                                      path);
    }
    if (offset > 0 && lseek(fd, (off_t)offset, SEEK_SET) < 0) {
        close(fd);
        return agent_write_error_json(result_buf, result_cap,
                                      "io_error",
                                      "cannot seek to offset",
                                      path);
    }

    bounded_output_init(&bounded);
    bounded_output_begin_artifact(&bounded, "read", ".txt");
    bytes_read = 0;
    while (bytes_read < limit) {
        char chunk[512];
        int want = limit - bytes_read;
        int ret;

        if (want > (int)sizeof(chunk))
            want = (int)sizeof(chunk);
        ret = read(fd, chunk, (size_t)want);

        if (ret < 0) {
            close(fd);
            bounded_output_finish(&bounded, 0);
            debug_printf("[TOOL read_file] read failed: %s\n", path);
            return agent_write_error_json(result_buf, result_cap,
                                          "io_error",
                                          "read failed",
                                          path);
        }
        if (ret == 0)
            break;
        bounded_output_append(&bounded, chunk, ret);
        bytes_read += ret;
    }
    if (bytes_read == limit) {
        char extra;
        int ret = read(fd, &extra, 1);

        if (ret > 0) {
            limit_reached = 1;
            file_has_more = 1;
        }
    }
    close(fd);
    bounded_output_finish(&bounded,
                          bounded.total_bytes > AGENT_BOUNDED_INLINE ||
                          file_has_more);

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "path");
    jw_string(&jw, path);
    jw_key(&jw, "offset");
    jw_int(&jw, offset);
    jw_key(&jw, "limit");
    jw_int(&jw, limit);
    bounded_output_write_json(&bounded, &jw,
                              "content",
                              "content_head",
                              "content_tail");
    jw_key(&jw, "bytes_read");
    jw_int(&jw, bytes_read);
    jw_key(&jw, "limit_reached");
    jw_bool(&jw, limit_reached);
    jw_object_end(&jw);

    return jw_finish(&jw);
}
