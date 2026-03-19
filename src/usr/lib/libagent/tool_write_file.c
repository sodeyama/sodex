/*
 * tool_write_file.c - write_file tool implementation
 *
 * Writes content to a file on the filesystem.
 */

#include <agent/tool_handlers.h>
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

const char TOOL_SCHEMA_WRITE_FILE[] =
    "{\"type\":\"object\","
    "\"properties\":{\"path\":{\"type\":\"string\","
    "\"description\":\"File path to write. Relative paths use the current directory\"},"
    "\"content\":{\"type\":\"string\","
    "\"description\":\"Content to write (max 3072 bytes)\"},"
    "\"mode\":{\"type\":\"string\","
    "\"description\":\"Write mode: overwrite, create, append\"}},"
    "\"required\":[\"path\",\"content\"]}";

int tool_write_file(const char *input_json, int input_len,
                    char *result_buf, int result_cap)
{
    char path[AGENT_PATH_MAX];
    static char content[AGENT_FILE_WRITE_MAX + 1];
    char mode[16];
    int content_len;
    int fd;
    int bytes_written;
    struct json_writer jw;
    int flags;

    if (!input_json || !result_buf)
        return -1;

    if (agent_json_get_normalized_path(input_json, input_len,
                                       "path", path, sizeof(path)) < 0)
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_path",
                                      "path must resolve to a normalized location",
                                      (const char *)0);

    content_len = agent_json_get_string_field(input_json, input_len,
                                              "content",
                                              content, sizeof(content));
    if (content_len < 0) {
        return agent_write_error_json(result_buf, result_cap,
                                      "too_large",
                                      "content is missing or too large",
                                      path);
    }
    if (agent_json_get_string_field(input_json, input_len,
                                    "mode", mode, sizeof(mode)) < 0) {
        strcpy(mode, "overwrite");
    }

    flags = O_WRONLY | O_CREAT;
    if (strcmp(mode, "overwrite") == 0) {
        flags |= O_TRUNC;
    } else if (strcmp(mode, "append") == 0) {
        flags |= O_APPEND;
    } else if (strcmp(mode, "create") == 0) {
        flags |= O_EXCL;
    } else {
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_input",
                                      "mode must be overwrite, create, or append",
                                      path);
    }

    debug_printf("[TOOL write_file] writing %d bytes to: %s\n",
                 content_len, path);

    fd = open(path, flags, 0644);
    if (fd < 0) {
        debug_printf("[TOOL write_file] open failed: %s\n", path);
        return agent_write_error_json(result_buf, result_cap,
                                      (strcmp(mode, "create") == 0)
                                          ? "create_failed"
                                          : "io_error",
                                      "cannot open file for writing",
                                      path);
    }

    bytes_written = 0;
    while (bytes_written < content_len) {
        int ret = write(fd, content + bytes_written,
                        (size_t)(content_len - bytes_written));

        if (ret <= 0) {
            close(fd);
            debug_printf("[TOOL write_file] write failed: %s\n", path);
            return agent_write_error_json(result_buf, result_cap,
                                          "io_error",
                                          "write failed",
                                          path);
        }
        bytes_written += ret;
    }
    close(fd);

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "status");
    jw_string(&jw, "ok");
    jw_key(&jw, "path");
    jw_string(&jw, path);
    jw_key(&jw, "mode");
    jw_string(&jw, mode);
    jw_key(&jw, "bytes_written");
    jw_int(&jw, bytes_written);
    jw_object_end(&jw);

    return jw_finish(&jw);
}
