/*
 * permissions.c - Permission policy for tool execution
 *
 * Enforces access control for agent tool calls based on a configurable
 * permission policy with deny lists for paths and commands.
 */

#include <agent/permissions.h>
#include <string.h>
#include <stdio.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#else
#include <fs.h>
#include <debug.h>
#endif

/* ---- Internal helpers ---- */

/* Copy a string safely, always null-terminate */
static void safe_str(char *dst, int dst_cap, const char *src)
{
    int len;
    if (!src || dst_cap <= 0)
        return;
    len = strlen(src);
    if (len >= dst_cap)
        len = dst_cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/*
 * Extract a JSON string value for a given key from input_json.
 * Simplified parser: finds "key":"value" pattern.
 * Returns length of extracted value, or -1 if not found.
 */
static int json_extract_string(const char *json, int json_len,
                                const char *key, char *out, int out_cap)
{
    char pattern[128];
    const char *p;
    int pi, ki;

    if (!json || !key || !out || out_cap <= 0)
        return -1;

    /* Build pattern: "key":" */
    ki = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (ki <= 0)
        return -1;

    p = strstr(json, pattern);
    if (!p)
        return -1;

    p += ki; /* skip past pattern */

    /* Extract value until closing unescaped quote */
    pi = 0;
    while (*p && *p != '"' && pi < out_cap - 1) {
        if (*p == '\\' && *(p + 1)) {
            out[pi++] = *(p + 1);
            p += 2;
        } else {
            out[pi++] = *p;
            p++;
        }
    }
    out[pi] = '\0';
    return pi;
}

/* ---- Public API ---- */

void perm_init(struct permission_policy *policy)
{
    if (!policy)
        return;
    memset(policy, 0, sizeof(*policy));
    policy->mode = PERM_STANDARD;
}

void perm_set_default(struct permission_policy *policy)
{
    if (!policy)
        return;

    perm_init(policy);
    policy->mode = PERM_STANDARD;

    /* Default denied paths */
    safe_str(policy->deny_paths[0], 256, "/boot/");
    safe_str(policy->deny_paths[1], 256, "/etc/agent/");
    policy->deny_path_count = 2;

    /* Default denied commands */
    safe_str(policy->deny_commands[0], 64, "rm -rf");
    safe_str(policy->deny_commands[1], 64, "dd if=");
    safe_str(policy->deny_commands[2], 64, "mkfs");
    policy->deny_cmd_count = 3;
}

int perm_check_tool(const struct permission_policy *policy,
                     const char *tool_name, const char *input_json, int input_len)
{
    int i;

    if (!policy || !tool_name)
        return 0; /* deny on invalid input */

    /* Permissive mode: allow everything */
    if (policy->mode == PERM_PERMISSIVE)
        return 1;

    /* Strict mode: only allow safe read-only tools */
    if (policy->mode == PERM_STRICT) {
        if (strcmp(tool_name, "read_file") == 0 ||
            strcmp(tool_name, "list_dir") == 0 ||
            strcmp(tool_name, "get_system_info") == 0) {
            return 1;
        }
#ifndef TEST_BUILD
        debug_printf("[PERM] strict mode: blocked tool %s\n", tool_name);
#endif
        return 0;
    }

    /* Standard mode: allow all tools but enforce deny lists */

    /* Check write_file against denied paths */
    if (strcmp(tool_name, "write_file") == 0 && input_json) {
        char file_path[256];
        int plen = json_extract_string(input_json, input_len,
                                        "path", file_path, sizeof(file_path));
        if (plen > 0) {
            for (i = 0; i < policy->deny_path_count; i++) {
                if (strncmp(file_path, policy->deny_paths[i],
                            strlen(policy->deny_paths[i])) == 0) {
#ifndef TEST_BUILD
                    debug_printf("[PERM] denied write to %s (matches %s)\n",
                                 file_path, policy->deny_paths[i]);
#endif
                    return 0;
                }
            }
        }
    }

    /* Check run_command against denied commands */
    if (strcmp(tool_name, "run_command") == 0 && input_json) {
        char command[512];
        int clen = json_extract_string(input_json, input_len,
                                        "command", command, sizeof(command));
        if (clen > 0) {
            for (i = 0; i < policy->deny_cmd_count; i++) {
                if (strstr(command, policy->deny_commands[i]) != (void *)0) {
#ifndef TEST_BUILD
                    debug_printf("[PERM] denied command: %s (matches %s)\n",
                                 command, policy->deny_commands[i]);
#endif
                    return 0;
                }
            }
        }
    }

    return 1; /* allowed */
}

int perm_load_policy(struct permission_policy *policy, const char *path)
{
    char buf[2048];
    int fd, nread, line_start, i;

    if (!policy || !path)
        return -1;

    perm_init(policy);

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
#ifndef TEST_BUILD
        debug_printf("[PERM] cannot open policy file %s\n", path);
#endif
        return -1;
    }

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (nread <= 0)
        return -1;

    buf[nread] = '\0';

    /* Parse key=value lines */
    line_start = 0;
    for (i = 0; i <= nread; i++) {
        if (i == nread || buf[i] == '\n') {
            char *line = &buf[line_start];
            int line_len = i - line_start;

            /* Null-terminate line */
            buf[i] = '\0';

            /* Skip empty lines and comments */
            if (line_len <= 0 || line[0] == '#') {
                line_start = i + 1;
                continue;
            }

            /* Parse mode=... */
            if (strncmp(line, "mode=", 5) == 0) {
                const char *val = line + 5;
                if (strcmp(val, "strict") == 0)
                    policy->mode = PERM_STRICT;
                else if (strcmp(val, "permissive") == 0)
                    policy->mode = PERM_PERMISSIVE;
                else
                    policy->mode = PERM_STANDARD;
            }
            /* Parse deny_path=... */
            else if (strncmp(line, "deny_path=", 10) == 0) {
                const char *val = line + 10;
                if (policy->deny_path_count < PERM_MAX_DENY_PATHS) {
                    safe_str(policy->deny_paths[policy->deny_path_count],
                             256, val);
                    policy->deny_path_count++;
                }
            }
            /* Parse deny_cmd=... */
            else if (strncmp(line, "deny_cmd=", 9) == 0) {
                const char *val = line + 9;
                if (policy->deny_cmd_count < PERM_MAX_DENY_CMDS) {
                    safe_str(policy->deny_commands[policy->deny_cmd_count],
                             64, val);
                    policy->deny_cmd_count++;
                }
            }

            line_start = i + 1;
        }
    }

#ifndef TEST_BUILD
    debug_printf("[PERM] loaded policy: mode=%d, %d deny_paths, %d deny_cmds\n",
                 policy->mode, policy->deny_path_count, policy->deny_cmd_count);
#endif
    return 0;
}
