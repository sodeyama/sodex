/*
 * path_utils.h - agent 用 path / JSON helper
 */
#ifndef _AGENT_PATH_UTILS_H
#define _AGENT_PATH_UTILS_H

#include <fs.h>

#define AGENT_PATH_MAX              256
#define AGENT_FILE_READ_LIMIT_MAX   65536
#define AGENT_FILE_READ_LIMIT_DEF   4096
#define AGENT_FILE_WRITE_MAX        3072
#define AGENT_DEFAULT_HOME          "/home/user"

int agent_json_get_string_field(const char *input_json, int input_len,
                                const char *key, char *out, int out_cap);
int agent_json_get_int_field(const char *input_json, int input_len,
                             const char *key, int *out);
int agent_normalize_path(const char *path, char *out, int out_cap);
int agent_resolve_path(const char *path, char *out, int out_cap);
int agent_json_get_normalized_path(const char *input_json, int input_len,
                                   const char *key, char *out, int out_cap);
int agent_path_join(const char *base, const char *name, char *out, int out_cap);
int agent_path_is_under(const char *path, const char *prefix);
int agent_write_error_json(char *result_buf, int result_cap,
                           const char *code, const char *message,
                           const char *path);

#endif /* _AGENT_PATH_UTILS_H */
