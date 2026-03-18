/*
 * permissions.h - Permission policy for tool execution
 */
#ifndef _AGENT_PERMISSIONS_H
#define _AGENT_PERMISSIONS_H

#define PERM_MAX_RULES       32
#define PERM_MAX_DENY_PATHS  16
#define PERM_MAX_DENY_CMDS   16

enum permission_mode {
    PERM_STRICT,
    PERM_STANDARD,
    PERM_PERMISSIVE,
};

struct permission_policy {
    enum permission_mode mode;

    /* Denied path prefixes (for write_file) */
    char deny_paths[PERM_MAX_DENY_PATHS][256];
    int deny_path_count;

    /* Denied command patterns (for run_command) */
    char deny_commands[PERM_MAX_DENY_CMDS][64];
    int deny_cmd_count;
};

void perm_init(struct permission_policy *policy);
void perm_set_default(struct permission_policy *policy);
int perm_check_tool(const struct permission_policy *policy,
                     const char *tool_name, const char *input_json, int input_len);
/* Returns 1 if allowed, 0 if denied */

int perm_load_policy(struct permission_policy *policy, const char *path);

#endif /* _AGENT_PERMISSIONS_H */
