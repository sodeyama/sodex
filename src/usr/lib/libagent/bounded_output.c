/*
 * bounded_output.c - Long output summary + artifact helpers
 */

#include <agent/bounded_output.h>
#include <sodex/const.h>
#include <string.h>
#include <stdio.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

PRIVATE unsigned int s_artifact_counter = 1;

void bounded_output_init(struct bounded_output *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->artifact_fd = -1;
}

int bounded_output_begin_artifact(struct bounded_output *out,
                                  const char *prefix,
                                  const char *suffix)
{
    unsigned int id;
    const char *name_prefix;
    const char *name_suffix;

    if (!out)
        return -1;

#ifndef TEST_BUILD
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
#endif
    mkdir(AGENT_ARTIFACT_DIR, 0755);

    id = s_artifact_counter++;
    name_prefix = prefix ? prefix : "artifact";
    name_suffix = suffix ? suffix : ".txt";
    snprintf(out->artifact_path, sizeof(out->artifact_path),
             "%s/%s_%08x%s",
             AGENT_ARTIFACT_DIR, name_prefix, id, name_suffix);
    out->artifact_fd = open(out->artifact_path,
                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out->artifact_fd < 0) {
        out->artifact_path[0] = '\0';
        return -1;
    }
    return 0;
}

int bounded_output_append(struct bounded_output *out,
                          const char *data,
                          int len)
{
    int copy_len;

    if (!out || !data || len <= 0)
        return -1;

    if (out->artifact_fd >= 0)
        write(out->artifact_fd, data, (size_t)len);

    copy_len = len;
    if (out->inline_len < AGENT_BOUNDED_INLINE) {
        int remain = AGENT_BOUNDED_INLINE - out->inline_len;

        if (copy_len > remain)
            copy_len = remain;
        memcpy(out->inline_buf + out->inline_len, data, (size_t)copy_len);
        out->inline_len += copy_len;
        out->inline_buf[out->inline_len] = '\0';
    }

    if (out->head_len < AGENT_BOUNDED_HEAD) {
        int remain = AGENT_BOUNDED_HEAD - out->head_len;

        copy_len = len;
        if (copy_len > remain)
            copy_len = remain;
        memcpy(out->head + out->head_len, data, (size_t)copy_len);
        out->head_len += copy_len;
        out->head[out->head_len] = '\0';
    }

    if (len >= AGENT_BOUNDED_TAIL) {
        memcpy(out->tail, data + (len - AGENT_BOUNDED_TAIL),
               (size_t)AGENT_BOUNDED_TAIL);
        out->tail_len = AGENT_BOUNDED_TAIL;
    } else {
        int shift = out->tail_len + len - AGENT_BOUNDED_TAIL;

        if (shift > 0) {
            memmove(out->tail, out->tail + shift,
                    (size_t)(out->tail_len - shift));
            out->tail_len -= shift;
        }
        memcpy(out->tail + out->tail_len, data, (size_t)len);
        out->tail_len += len;
    }
    out->tail[out->tail_len] = '\0';

    out->total_bytes += len;
    return 0;
}

int bounded_output_finish(struct bounded_output *out, int keep_artifact)
{
    if (!out)
        return -1;

    out->omitted_bytes = out->total_bytes - out->head_len - out->tail_len;
    if (out->omitted_bytes < 0)
        out->omitted_bytes = 0;

    if (out->artifact_fd >= 0) {
        close(out->artifact_fd);
        out->artifact_fd = -1;
    }

    if (!keep_artifact && out->artifact_path[0] != '\0') {
        unlink(out->artifact_path);
        out->artifact_path[0] = '\0';
    }
    return 0;
}

int bounded_output_write_json(struct bounded_output *out,
                              struct json_writer *jw,
                              const char *full_key,
                              const char *head_key,
                              const char *tail_key)
{
    if (!out || !jw)
        return -1;

    if (out->total_bytes <= AGENT_BOUNDED_INLINE) {
        jw_key(jw, full_key ? full_key : "output");
        jw_string_n(jw, out->inline_buf, out->inline_len);
        return 0;
    }

    jw_key(jw, head_key ? head_key : "output_head");
    jw_string_n(jw, out->head, out->head_len);
    jw_key(jw, tail_key ? tail_key : "output_tail");
    jw_string_n(jw, out->tail, out->tail_len);
    jw_key(jw, "omitted_bytes");
    jw_int(jw, out->omitted_bytes);
    if (out->artifact_path[0] != '\0') {
        jw_key(jw, "artifact_path");
        jw_string(jw, out->artifact_path);
    }
    jw_key(jw, "truncated");
    jw_bool(jw, 1);
    return 0;
}
