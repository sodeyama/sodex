/*
 * memory_store.h - Workspace memory helpers
 */
#ifndef _AGENT_MEMORY_STORE_H
#define _AGENT_MEMORY_STORE_H

#include <fs.h>

#ifndef AGENT_MEMORY_DIR
#ifdef TEST_BUILD
#define AGENT_MEMORY_DIR "/tmp/agent_test_memory"
#else
#define AGENT_MEMORY_DIR "/var/agent/memory"
#endif
#endif

#ifndef AGENT_USER_CLAUDE_PATH
#ifdef TEST_BUILD
#define AGENT_USER_CLAUDE_PATH "/tmp/agent_test_user_CLAUDE.md"
#else
#define AGENT_USER_CLAUDE_PATH "/etc/CLAUDE.md"
#endif
#endif

#define AGENT_MEMORY_SOURCE_MAX 16
#define AGENT_MEMORY_LABEL_MAX  64

struct agent_memory_source {
    char path[PATHNAME_MAX];
    char label[AGENT_MEMORY_LABEL_MAX];
};

unsigned int agent_hash_path(const char *path);
int agent_build_project_claude_path(const char *cwd, char *buf, int cap);
int agent_build_workspace_memory_path(const char *cwd, char *buf, int cap);
int agent_collect_memory_sources(const char *cwd,
                                 struct agent_memory_source *out,
                                 int max_sources);
int agent_memory_text_has_secret(const char *text);
int agent_memory_append_workspace(const char *cwd,
                                  const char *note,
                                  char *path_out, int path_cap);
int agent_memory_auto_note_command(const char *command,
                                   char *note_out, int note_cap);

#endif /* _AGENT_MEMORY_STORE_H */
