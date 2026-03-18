/*
 * memory_store.c - Workspace memory helpers
 */

#include <agent/memory_store.h>
#include <sodex/const.h>
#include <string.h>
#include <stdio.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

PRIVATE int safe_copy(char *dst, int cap, const char *src)
{
    int len;

    if (!dst || cap <= 0)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    len = strlen(src);
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    return len;
}

PRIVATE int file_exists(const char *path)
{
    int fd;

    if (!path || !*path)
        return 0;
    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

PRIVATE int path_join(char *dst, int cap, const char *dir, const char *name)
{
    int dir_len;
    int name_len;

    if (!dst || !dir || !name || cap <= 1)
        return -1;

    dir_len = strlen(dir);
    name_len = strlen(name);

    if (strcmp(dir, "/") == 0) {
        if (name_len + 2 > cap)
            return -1;
        dst[0] = '/';
        memcpy(dst + 1, name, (size_t)name_len);
        dst[name_len + 1] = '\0';
        return name_len + 1;
    }

    if (dir_len + name_len + 2 > cap)
        return -1;
    memcpy(dst, dir, (size_t)dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, name, (size_t)name_len);
    dst[dir_len + name_len + 1] = '\0';
    return dir_len + name_len + 1;
}

PRIVATE int trim_to_parent(char *path)
{
    int len;

    if (!path)
        return -1;
    len = strlen(path);
    if (len <= 1)
        return -1;

    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    while (len > 1 && path[len - 1] != '/') {
        path[len - 1] = '\0';
        len--;
    }
    if (len > 1)
        path[len - 1] = '\0';
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
    return 0;
}

PRIVATE int str_contains(const char *haystack, const char *needle)
{
    return (haystack && needle && strstr(haystack, needle) != 0) ? 1 : 0;
}

PRIVATE int append_source(struct agent_memory_source *out,
                          int max_sources,
                          int count,
                          const char *path,
                          const char *label)
{
    int i;

    if (!out || !path || !label || count >= max_sources)
        return count;
    if (!file_exists(path))
        return count;

    for (i = 0; i < count; i++) {
        if (strcmp(out[i].path, path) == 0)
            return count;
    }

    safe_copy(out[count].path, sizeof(out[count].path), path);
    safe_copy(out[count].label, sizeof(out[count].label), label);
    return count + 1;
}

unsigned int agent_hash_path(const char *path)
{
    unsigned int hash = 5381U;

    if (!path)
        return 0U;
    while (*path) {
        hash = ((hash << 5) + hash) ^ (unsigned int)(unsigned char)(*path);
        path++;
    }
    return hash;
}

int agent_build_project_claude_path(const char *cwd, char *buf, int cap)
{
    if (!cwd || !buf || cap <= 1)
        return -1;
    return path_join(buf, cap, cwd, "CLAUDE.md");
}

int agent_build_workspace_memory_path(const char *cwd, char *buf, int cap)
{
    unsigned int hash;

    if (!cwd || !buf || cap <= 1)
        return -1;
    hash = agent_hash_path(cwd);
    return snprintf(buf, (size_t)cap, "%s/%08x.md", AGENT_MEMORY_DIR, hash);
}

int agent_collect_memory_sources(const char *cwd,
                                 struct agent_memory_source *out,
                                 int max_sources)
{
    static const char *parent_names[] = {
        "AGENTS.md",
        "AGENTS.local.md",
        "CLAUDE.local.md",
        0
    };
    static char project_path[PATHNAME_MAX];
    static char workspace_path[PATHNAME_MAX];
    static char scan[PATHNAME_MAX];
    int count = 0;
    int is_first = 1;

    if (!cwd || !out || max_sources <= 0)
        return -1;

    memset(out, 0, sizeof(*out) * (size_t)max_sources);

    count = append_source(out, max_sources, count,
                          AGENT_USER_CLAUDE_PATH,
                          "User Scope Instructions (/etc/CLAUDE.md)");

    if (agent_build_project_claude_path(cwd, project_path, sizeof(project_path)) >= 0) {
        count = append_source(out, max_sources, count,
                              project_path,
                              "Project Scope Instructions (./CLAUDE.md)");
    }

    count = append_source(out, max_sources, count,
                          AGENT_MEMORY_DIR "/global.md",
                          "Global Memory");

    safe_copy(scan, sizeof(scan), cwd);
    for (;;) {
        int idx;

        for (idx = 0; parent_names[idx] != 0; idx++) {
            static char path[PATHNAME_MAX];

            if (path_join(path, sizeof(path), scan, parent_names[idx]) >= 0) {
                count = append_source(out, max_sources, count,
                                      path, parent_names[idx]);
            }
        }

        if (!is_first) {
            static char path[PATHNAME_MAX];

            if (path_join(path, sizeof(path), scan, "CLAUDE.md") >= 0) {
                count = append_source(out, max_sources, count,
                                      path, "CLAUDE.md");
            }
        }

        if (strcmp(scan, "/") == 0)
            break;
        if (trim_to_parent(scan) < 0)
            break;
        is_first = 0;
    }

    if (agent_build_workspace_memory_path(cwd, workspace_path,
                                          sizeof(workspace_path)) >= 0) {
        count = append_source(out, max_sources, count,
                              workspace_path,
                              "Workspace Memory");
    }

    return count;
}

int agent_memory_text_has_secret(const char *text)
{
    if (!text || !*text)
        return 0;

    if (str_contains(text, "sk-ant-") ||
        str_contains(text, "sk-proj-") ||
        str_contains(text, "api_key") ||
        str_contains(text, "apikey") ||
        str_contains(text, "token") ||
        str_contains(text, "password") ||
        str_contains(text, "passwd") ||
        str_contains(text, "secret") ||
        str_contains(text, "bearer ") ||
        str_contains(text, "BEGIN ") ||
        str_contains(text, "PRIVATE KEY")) {
        return 1;
    }

    return 0;
}

int agent_memory_append_workspace(const char *cwd,
                                  const char *note,
                                  char *path_out, int path_cap)
{
    static char path[PATHNAME_MAX];
    static char existing[4096];
    int fd;
    int nread;

    if (!cwd || !note || !*note)
        return -1;
    if (agent_memory_text_has_secret(note))
        return -2;

#ifndef TEST_BUILD
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
#endif
    mkdir(AGENT_MEMORY_DIR, 0755);

    if (agent_build_workspace_memory_path(cwd, path, sizeof(path)) < 0)
        return -1;

    existing[0] = '\0';
    fd = open(path, O_RDONLY, 0);
    if (fd >= 0) {
        nread = read(fd, existing, sizeof(existing) - 1);
        close(fd);
        if (nread > 0)
            existing[nread] = '\0';
    }

    if (existing[0] != '\0' && strstr(existing, note) != 0) {
        if (path_out && path_cap > 0)
            safe_copy(path_out, path_cap, path);
        return 0;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return -1;
    write(fd, note, (size_t)strlen(note));
    write(fd, "\n", 1);
    close(fd);

    if (path_out && path_cap > 0)
        safe_copy(path_out, path_cap, path);
    return 0;
}

int agent_memory_auto_note_command(const char *command,
                                   char *note_out, int note_cap)
{
    if (!command || !note_out || note_cap <= 0)
        return -1;
    if (agent_memory_text_has_secret(command))
        return -1;

    if (strncmp(command, "make ", 5) != 0 &&
        strncmp(command, "python ", 7) != 0 &&
        strncmp(command, "python3 ", 8) != 0 &&
        strncmp(command, "docker ", 7) != 0 &&
        strncmp(command, "git ", 4) != 0 &&
        strncmp(command, "bin/", 4) != 0 &&
        strncmp(command, "./bin/", 6) != 0 &&
        strncmp(command, "SODEX_", 6) != 0) {
        note_out[0] = '\0';
        return -1;
    }

    return snprintf(note_out, (size_t)note_cap,
                    "よく使うコマンド: %s", command);
}
