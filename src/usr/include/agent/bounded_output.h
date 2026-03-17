/*
 * bounded_output.h - Long output summary + artifact helpers
 */
#ifndef _AGENT_BOUNDED_OUTPUT_H
#define _AGENT_BOUNDED_OUTPUT_H

#include <fs.h>
#include <json.h>

#ifndef AGENT_ARTIFACT_DIR
#ifdef TEST_BUILD
#define AGENT_ARTIFACT_DIR "/tmp/agent_test_artifacts"
#else
#define AGENT_ARTIFACT_DIR "/var/agent/artifacts"
#endif
#endif

#define AGENT_BOUNDED_INLINE 1024
#define AGENT_BOUNDED_HEAD    256
#define AGENT_BOUNDED_TAIL    256

struct bounded_output {
    char inline_buf[AGENT_BOUNDED_INLINE + 1];
    int inline_len;
    char head[AGENT_BOUNDED_HEAD + 1];
    int head_len;
    char tail[AGENT_BOUNDED_TAIL + 1];
    int tail_len;
    int total_bytes;
    int omitted_bytes;
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

#endif /* _AGENT_BOUNDED_OUTPUT_H */
