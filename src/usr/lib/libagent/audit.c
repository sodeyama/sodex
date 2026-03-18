/*
 * audit.c - Audit logging for tool execution
 *
 * Appends structured log entries to a persistent audit log file.
 * Each entry is a single line recording tool actions, permissions
 * decisions, and errors.
 */

#include <agent/audit.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#else
#include <fs.h>
#include <debug.h>
#endif

int audit_init(void)
{
    /* Ensure /var/agent directory exists */
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
    return 0;
}

int audit_log(const struct audit_entry *entry)
{
    char line[AUDIT_MAX_ENTRY];
    int fd, len;

    if (!entry)
        return -1;

    /* Format: [timestamp] session=xxx step=N ACTION tool_name detail */
    len = snprintf(line, sizeof(line),
        "[%d] session=%s step=%d %s %s %s\n",
        entry->timestamp,
        entry->session_id,
        entry->step,
        entry->action,
        entry->tool_name,
        entry->detail);

    if (len <= 0 || len >= (int)sizeof(line))
        return -1;

    fd = open(AUDIT_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
#ifndef TEST_BUILD
        debug_printf("[AUDIT] cannot open %s\n", AUDIT_LOG_PATH);
#endif
        return -1;
    }

    write(fd, line, len);
    close(fd);

    return 0;
}

int audit_read_last(struct audit_entry *entries, int max_entries, int *count)
{
    static char buf[8192];
    int fd, nread, i, line_count;
    int line_starts[256];

    if (!entries || !count || max_entries <= 0)
        return -1;

    *count = 0;

    fd = open(AUDIT_LOG_PATH, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (nread <= 0)
        return 0;

    buf[nread] = '\0';

    /* Find all line start positions */
    line_count = 0;
    line_starts[0] = 0;
    line_count = 1;

    for (i = 0; i < nread && line_count < 256; i++) {
        if (buf[i] == '\n' && i + 1 < nread) {
            line_starts[line_count++] = i + 1;
        }
    }

    /* Take last max_entries lines */
    int start_idx = 0;
    if (line_count > max_entries)
        start_idx = line_count - max_entries;

    for (i = start_idx; i < line_count && *count < max_entries; i++) {
        char *line = &buf[line_starts[i]];
        char *end;
        struct audit_entry *e = &entries[*count];

        /* Find end of this line */
        end = line;
        while (*end && *end != '\n')
            end++;
        *end = '\0';

        /* Skip empty lines */
        if (line[0] == '\0')
            continue;

        memset(e, 0, sizeof(*e));

        /*
         * Parse: [timestamp] session=xxx step=N ACTION tool_name detail
         * Simplified parsing using sscanf-like approach with strstr
         */
        if (line[0] == '[') {
            char *p = line + 1;
            e->timestamp = strtol(p, &p, 10);

            /* Find session= */
            char *sp = strstr(line, "session=");
            if (sp) {
                sp += 8;
                int si = 0;
                while (*sp && *sp != ' ' && si < 32)
                    e->session_id[si++] = *sp++;
                e->session_id[si] = '\0';
            }

            /* Find step= */
            char *stp = strstr(line, "step=");
            if (stp) {
                stp += 5;
                e->step = strtol(stp, (void *)0, 10);
            }

            /* Parse remaining fields: ACTION tool_name detail
             * Find the position after step=N */
            if (stp) {
                char *fp = stp;
                while (*fp && *fp != ' ')
                    fp++;
                if (*fp == ' ')
                    fp++;

                /* ACTION */
                int ai = 0;
                while (*fp && *fp != ' ' && ai < 15)
                    e->action[ai++] = *fp++;
                e->action[ai] = '\0';
                if (*fp == ' ')
                    fp++;

                /* tool_name */
                int ti = 0;
                while (*fp && *fp != ' ' && ti < 63)
                    e->tool_name[ti++] = *fp++;
                e->tool_name[ti] = '\0';
                if (*fp == ' ')
                    fp++;

                /* detail (rest of line) */
                int di = 0;
                while (*fp && di < 255)
                    e->detail[di++] = *fp++;
                e->detail[di] = '\0';
            }

            (*count)++;
        }
    }

    return 0;
}

int audit_rotate(int max_size)
{
    static char buf[8192];
    int fd, nread, half_start, i;

    if (max_size <= 0)
        return -1;

    fd = open(AUDIT_LOG_PATH, O_RDONLY, 0);
    if (fd < 0)
        return 0; /* no file, nothing to rotate */

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (nread <= 0 || nread < max_size)
        return 0; /* file small enough */

    buf[nread] = '\0';

    /* Find the midpoint and the next newline after it */
    half_start = nread / 2;
    for (i = half_start; i < nread; i++) {
        if (buf[i] == '\n') {
            half_start = i + 1;
            break;
        }
    }

    if (half_start >= nread)
        return 0;

    /* Rewrite file with only the second half */
    fd = open(AUDIT_LOG_PATH, O_WRONLY | O_TRUNC, 0);
    if (fd < 0)
        return -1;

    write(fd, &buf[half_start], nread - half_start);
    close(fd);

#ifndef TEST_BUILD
    debug_printf("[AUDIT] rotated log, kept %d bytes\n", nread - half_start);
#endif

    return 1;
}
