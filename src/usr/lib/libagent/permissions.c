/*
 * permissions.c - Permission policy for tool execution
 *
 * Enforces access control for agent tool calls based on a configurable
 * permission policy with deny lists for paths and commands.
 */

#include <agent/permissions.h>
#include <agent/path_utils.h>
#include <json.h>
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

static int store_path_rule(char rules[][256], int *count, const char *value)
{
    char normalized[256];

    if (!rules || !count || !value)
        return -1;
    if (*count >= PERM_MAX_PATH_RULES)
        return -1;
    if (agent_normalize_path(value, normalized, sizeof(normalized)) < 0)
        return -1;

    safe_str(rules[*count], 256, normalized);
    (*count)++;
    return 0;
}

static int path_matches_any(const char *path, const char rules[][256], int count)
{
    int i;

    if (!path)
        return 0;
    for (i = 0; i < count; i++) {
        if (agent_path_is_under(path, rules[i]))
            return 1;
    }
    return 0;
}

static int path_allowed(const char *path,
                        const char allow_rules[][256], int allow_count,
                        const char deny_rules[][256], int deny_count)
{
    int allow_hit;

    if (!path)
        return 1;

    allow_hit = 1;
    if (allow_count > 0)
        allow_hit = path_matches_any(path, allow_rules, allow_count);
    if (!allow_hit)
        return 0;
    if (path_matches_any(path, deny_rules, deny_count))
        return 0;
    return 1;
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

    store_path_rule(policy->read_allow_paths, &policy->read_allow_count, "/");
    store_path_rule(policy->write_allow_paths, &policy->write_allow_count,
                    AGENT_DEFAULT_HOME "/");
    store_path_rule(policy->write_allow_paths, &policy->write_allow_count, "/tmp/");
    store_path_rule(policy->write_allow_paths, &policy->write_allow_count, "/var/agent/");
    store_path_rule(policy->write_deny_paths, &policy->write_deny_count, "/boot/");
    store_path_rule(policy->write_deny_paths, &policy->write_deny_count, "/etc/agent/");

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
    char path[AGENT_PATH_MAX];
    int path_len = -1;

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

    if ((strcmp(tool_name, "read_file") == 0 ||
         strcmp(tool_name, "list_dir") == 0 ||
         strcmp(tool_name, "write_file") == 0) &&
        input_json) {
        path_len = agent_json_get_normalized_path(input_json, input_len,
                                                  "path", path, sizeof(path));
    }

    /* Standard mode: allow all tools but enforce path and command policy */
    if (strcmp(tool_name, "read_file") == 0 ||
        strcmp(tool_name, "list_dir") == 0) {
        if (path_len > 0 &&
            !path_allowed(path,
                          policy->read_allow_paths, policy->read_allow_count,
                          policy->read_deny_paths, policy->read_deny_count)) {
#ifndef TEST_BUILD
            debug_printf("[PERM] denied read of %s\n", path);
#endif
            return 0;
        }
    }

    if (strcmp(tool_name, "write_file") == 0) {
        if (path_len > 0 &&
            !path_allowed(path,
                          policy->write_allow_paths, policy->write_allow_count,
                          policy->write_deny_paths, policy->write_deny_count)) {
#ifndef TEST_BUILD
            debug_printf("[PERM] denied write to %s\n", path);
#endif
            return 0;
        }
    }

    /* Check run_command against denied commands */
    if (strcmp(tool_name, "run_command") == 0 && input_json) {
        char command[512];
        int clen = agent_json_get_string_field(input_json, input_len,
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

            if (strncmp(line, "mode=", 5) == 0) {
                const char *val = line + 5;
                if (strcmp(val, "strict") == 0)
                    policy->mode = PERM_STRICT;
                else if (strcmp(val, "permissive") == 0)
                    policy->mode = PERM_PERMISSIVE;
                else
                    policy->mode = PERM_STANDARD;
            }
            else if (strncmp(line, "read_allow=", 11) == 0) {
                store_path_rule(policy->read_allow_paths,
                                &policy->read_allow_count, line + 11);
            }
            else if (strncmp(line, "read_deny=", 10) == 0) {
                store_path_rule(policy->read_deny_paths,
                                &policy->read_deny_count, line + 10);
            }
            else if (strncmp(line, "write_allow=", 12) == 0) {
                store_path_rule(policy->write_allow_paths,
                                &policy->write_allow_count, line + 12);
            }
            else if (strncmp(line, "write_deny=", 11) == 0) {
                store_path_rule(policy->write_deny_paths,
                                &policy->write_deny_count, line + 11);
            }
            else if (strncmp(line, "deny_path=", 10) == 0) {
                /* 旧設定との互換: deny_path は write_deny として扱う */
                store_path_rule(policy->write_deny_paths,
                                &policy->write_deny_count, line + 10);
            }
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
    debug_printf("[PERM] loaded policy: mode=%d, read_allow=%d, read_deny=%d, "
                 "write_allow=%d, write_deny=%d, deny_cmds=%d\n",
                 policy->mode,
                 policy->read_allow_count, policy->read_deny_count,
                 policy->write_allow_count, policy->write_deny_count,
                 policy->deny_cmd_count);
#endif
    return 0;
}
