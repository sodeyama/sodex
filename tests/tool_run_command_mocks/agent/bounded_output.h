#ifndef _AGENT_BOUNDED_OUTPUT_H
#define _AGENT_BOUNDED_OUTPUT_H

#define AGENT_BOUNDED_INLINE 1024
#define PATHNAME_MAX 4096

struct json_writer;

struct bounded_output {
    char inline_buf[AGENT_BOUNDED_INLINE + 1];
    int inline_len;
    int total_bytes;
    int artifact_fd;
    char artifact_path[PATHNAME_MAX];
};

void bounded_output_init(struct bounded_output *out);
int bounded_output_begin_artifact(struct bounded_output *out,
                                  const char *prefix,
                                  const char *suffix);
int bounded_output_append(struct bounded_output *out,
                          const char *data,
                          int len);
int bounded_output_finish(struct bounded_output *out, int keep_artifact);
int bounded_output_write_json(struct bounded_output *out,
                              struct json_writer *jw,
                              const char *full_key,
                              const char *head_key,
                              const char *tail_key);

#endif
