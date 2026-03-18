/*
 * test_memory_loader.c - Memory source and filter tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "agent/memory_store.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        failed++; \
        return; \
    } \
} while (0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while (0)

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;
    fputs(text, fp);
    fclose(fp);
    return 0;
}

static int find_source(struct agent_memory_source *sources,
                       int count,
                       const char *path)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(sources[i].path, path) == 0)
            return i;
    }
    return -1;
}

static void reset_paths(void)
{
    system("rm -rf /tmp/agent_memory_loader_case");
    system("rm -rf " AGENT_MEMORY_DIR);
    system("rm -f " AGENT_USER_CLAUDE_PATH);
}

static void test_source_order_and_filters(void)
{
    struct agent_memory_source sources[AGENT_MEMORY_SOURCE_MAX];
    char cwd[256];
    char workspace_path[PATHNAME_MAX];
    char note[256];
    int count;
    int pos_user;
    int pos_project;
    int pos_global;
    int pos_agents;
    int pos_local;
    int pos_workspace;

    TEST_START("source_order_and_filters");
    reset_paths();

    mkdir("/tmp/agent_memory_loader_case", 0755);
    mkdir("/tmp/agent_memory_loader_case/parent", 0755);
    mkdir("/tmp/agent_memory_loader_case/parent/project", 0755);
    mkdir(AGENT_MEMORY_DIR, 0755);

    strcpy(cwd, "/tmp/agent_memory_loader_case/parent/project");
    ASSERT(write_text_file(AGENT_USER_CLAUDE_PATH, "user scope\n") == 0,
           "write user scope");
    ASSERT(write_text_file(AGENT_MEMORY_DIR "/global.md", "global memory\n") == 0,
           "write global");
    ASSERT(write_text_file("/tmp/agent_memory_loader_case/parent/AGENTS.md",
                           "parent agents\n") == 0,
           "write agents");
    ASSERT(write_text_file("/tmp/agent_memory_loader_case/parent/CLAUDE.local.md",
                           "parent local\n") == 0,
           "write local");
    ASSERT(write_text_file("/tmp/agent_memory_loader_case/parent/project/CLAUDE.md",
                           "project scope\n") == 0,
           "write project");
    ASSERT(agent_build_workspace_memory_path(cwd,
                                            workspace_path,
                                            sizeof(workspace_path)) >= 0,
           "workspace path");
    ASSERT(write_text_file(workspace_path, "workspace memory\n") == 0,
           "write workspace");

    count = agent_collect_memory_sources(cwd, sources, AGENT_MEMORY_SOURCE_MAX);
    ASSERT(count >= 6, "source count");

    pos_user = find_source(sources, count, AGENT_USER_CLAUDE_PATH);
    pos_project = find_source(sources, count,
                              "/tmp/agent_memory_loader_case/parent/project/CLAUDE.md");
    pos_global = find_source(sources, count, AGENT_MEMORY_DIR "/global.md");
    pos_agents = find_source(sources, count,
                             "/tmp/agent_memory_loader_case/parent/AGENTS.md");
    pos_local = find_source(sources, count,
                            "/tmp/agent_memory_loader_case/parent/CLAUDE.local.md");
    pos_workspace = find_source(sources, count, workspace_path);

    ASSERT(pos_user >= 0, "user scope missing");
    ASSERT(pos_project > pos_user, "project after user");
    ASSERT(pos_global > pos_project, "global after project");
    ASSERT(pos_agents > pos_global, "agents after global");
    ASSERT(pos_local > pos_agents, "local after agents");
    ASSERT(pos_workspace > pos_local, "workspace last");

    ASSERT(agent_memory_text_has_secret("token=abcd") == 1, "secret detect");
    ASSERT(agent_memory_text_has_secret("make -C src test") == 0, "non secret");
    ASSERT(agent_memory_auto_note_command("make -C src test-qemu",
                                          note, sizeof(note)) > 0,
           "auto note");
    ASSERT(strstr(note, "よく使うコマンド") != NULL, "note text");
    TEST_PASS("source_order_and_filters");
}

int main(void)
{
    printf("=== memory loader tests ===\n\n");
    test_source_order_and_filters();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
