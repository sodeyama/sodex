/*
 * session.c - Session persistence for agent conversations
 *
 * Stores conversation turns as JSONL files on the filesystem.
 * Each session has a unique hex ID and a .jsonl file containing
 * one JSON object per line (one per turn).
 * An index file tracks all active session IDs.
 */

#include <agent/session.h>
#include <agent/conversation.h>
#include <string.h>
#include <stdio.h>
#include <fs.h>

#ifndef TEST_BUILD
#include <debug.h>
#endif

/* ---- Internal helpers ---- */

/* Static counter for ID generation */
static unsigned int s_id_counter = 1;

/* Build session file path: /var/agent/sessions/<id>.jsonl */
static void session_build_path(char *buf, int cap, const char *session_id)
{
    snprintf(buf, cap, "%s/%s.jsonl", SESSION_DIR, session_id);
}

/* Escape a string for JSON output into dst. Returns bytes written (excl NUL).
 * Only escapes characters required by JSON: \ " and control chars. */
static int json_escape(char *dst, int dst_cap, const char *src, int src_len)
{
    int di = 0;
    int si;

    if (dst_cap <= 0)
        return 0;

    for (si = 0; si < src_len && di < dst_cap - 1; si++) {
        char c = src[si];
        if (c == '"' || c == '\\') {
            if (di + 2 >= dst_cap)
                break;
            dst[di++] = '\\';
            dst[di++] = c;
        } else if (c == '\n') {
            if (di + 2 >= dst_cap)
                break;
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r') {
            if (di + 2 >= dst_cap)
                break;
            dst[di++] = '\\';
            dst[di++] = 'r';
        } else if (c == '\t') {
            if (di + 2 >= dst_cap)
                break;
            dst[di++] = '\\';
            dst[di++] = 't';
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
    return di;
}

/* Write index file with all session IDs, one per line */
static int session_write_index(const struct session_index *index)
{
    char path[256];
    char line[SESSION_ID_LEN + 2];
    int fd, i, len;

    snprintf(path, sizeof(path), "%s/index.txt", SESSION_DIR);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    for (i = 0; i < index->count; i++) {
        len = snprintf(line, sizeof(line), "%s\n", index->entries[i].id);
        write(fd, line, len);
    }

    close(fd);
    return 0;
}

/* Read index file into session_index (IDs only) */
static int session_read_index(struct session_index *index)
{
    char path[256];
    char buf[2048];
    int fd, nread, i, line_start;

    memset(index, 0, sizeof(*index));
    snprintf(path, sizeof(path), "%s/index.txt", SESSION_DIR);

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (nread <= 0)
        return 0;

    buf[nread] = '\0';

    /* Parse line by line */
    line_start = 0;
    for (i = 0; i <= nread && index->count < SESSION_MAX_SESSIONS; i++) {
        if (i == nread || buf[i] == '\n') {
            int line_len = i - line_start;
            if (line_len > 0 && line_len <= SESSION_ID_LEN) {
                memcpy(index->entries[index->count].id, &buf[line_start], line_len);
                index->entries[index->count].id[line_len] = '\0';
                index->count++;
            }
            line_start = i + 1;
        }
    }

    return 0;
}

/* ---- Public API ---- */

void session_generate_id(char *id_buf)
{
    unsigned int a, b, c, d;

    a = s_id_counter++;
    b = a * 2654435761U;  /* Knuth multiplicative hash */
    c = b ^ 0xDEADBEEF;
    d = a * 7 + 0x1234;

    snprintf(id_buf, SESSION_ID_LEN + 1, "%08x%08x%08x%08x", a, b, c, d);
}

int session_create(struct session_meta *meta, const char *model)
{
    char path[256];
    char line[512];
    int fd, len;
    struct session_index index;

    if (!meta)
        return -1;

    /* Ensure session directory exists */
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
    mkdir(SESSION_DIR, 0755);

    /* Generate ID */
    session_generate_id(meta->id);
    meta->created_at = 0;  /* No real clock; could use a tick counter */
    meta->turn_count = 0;
    meta->total_tokens = 0;
    if (model) {
        int mlen = strlen(model);
        if (mlen >= 64)
            mlen = 63;
        memcpy(meta->model, model, mlen);
        meta->model[mlen] = '\0';
    } else {
        meta->model[0] = '\0';
    }

    /* Write metadata as first line of JSONL file */
    session_build_path(path, sizeof(path), meta->id);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
#ifndef TEST_BUILD
        debug_printf("[SESSION] failed to create %s\n", path);
#endif
        return -1;
    }

    len = snprintf(line, sizeof(line),
        "{\"type\":\"meta\",\"id\":\"%s\",\"model\":\"%s\"}\n",
        meta->id, meta->model);
    write(fd, line, len);
    close(fd);

    /* Update index */
    session_read_index(&index);
    if (index.count < SESSION_MAX_SESSIONS) {
        memcpy(&index.entries[index.count], meta, sizeof(*meta));
        index.count++;
        session_write_index(&index);
    }

#ifndef TEST_BUILD
    debug_printf("[SESSION] created session %s\n", meta->id);
#endif
    return 0;
}

int session_append_turn(const char *session_id,
                         const struct conv_turn *turn,
                         int input_tokens, int output_tokens)
{
    char path[256];
    char line[4096];
    char escaped[2048];
    int fd, len, b;

    if (!session_id || !turn)
        return -1;

    session_build_path(path, sizeof(path), session_id);

    fd = open(path, O_WRONLY | O_APPEND, 0);
    if (fd < 0) {
#ifndef TEST_BUILD
        debug_printf("[SESSION] cannot open %s for append\n", path);
#endif
        return -1;
    }

    /* Serialize each block in the turn as a JSONL line */
    for (b = 0; b < turn->block_count; b++) {
        const struct conv_block *blk = &turn->blocks[b];

        switch (blk->type) {
        case CONV_BLOCK_TEXT:
            json_escape(escaped, sizeof(escaped),
                        blk->text.text, blk->text.text_len);
            len = snprintf(line, sizeof(line),
                "{\"type\":\"turn\",\"role\":\"%s\",\"block\":\"text\","
                "\"text\":\"%s\",\"in_tok\":%d,\"out_tok\":%d}\n",
                turn->role, escaped, input_tokens, output_tokens);
            if (len > 0 && len < (int)sizeof(line))
                write(fd, line, len);
            break;

        case CONV_BLOCK_TOOL_USE:
            json_escape(escaped, sizeof(escaped),
                        blk->tool_use.name,
                        strlen(blk->tool_use.name));
            len = snprintf(line, sizeof(line),
                "{\"type\":\"turn\",\"role\":\"%s\",\"block\":\"tool_use\","
                "\"tool_id\":\"%s\",\"tool_name\":\"%s\","
                "\"in_tok\":%d,\"out_tok\":%d}\n",
                turn->role, blk->tool_use.id, escaped,
                input_tokens, output_tokens);
            if (len > 0 && len < (int)sizeof(line))
                write(fd, line, len);
            break;

        case CONV_BLOCK_TOOL_RESULT:
            json_escape(escaped, sizeof(escaped),
                        blk->tool_result.content,
                        blk->tool_result.content_len);
            len = snprintf(line, sizeof(line),
                "{\"type\":\"turn\",\"role\":\"%s\",\"block\":\"tool_result\","
                "\"tool_use_id\":\"%s\",\"is_error\":%d,"
                "\"content\":\"%s\",\"in_tok\":%d,\"out_tok\":%d}\n",
                turn->role, blk->tool_result.tool_use_id,
                blk->tool_result.is_error, escaped,
                input_tokens, output_tokens);
            if (len > 0 && len < (int)sizeof(line))
                write(fd, line, len);
            break;
        }
    }

    close(fd);
    return 0;
}

int session_load(const char *session_id, struct conversation *conv)
{
    char path[256];
    char buf[SESSION_MAX_FILE];
    int fd, nread, line_start, i;

    if (!session_id || !conv)
        return -1;

    session_build_path(path, sizeof(path), session_id);
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
#ifndef TEST_BUILD
        debug_printf("[SESSION] cannot open %s for load\n", path);
#endif
        return -1;
    }

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (nread <= 0)
        return -1;

    buf[nread] = '\0';

    /*
     * Parse JSONL lines. For each turn line, reconstruct the conversation.
     * We use a simplified parser: look for key fields with strstr().
     */
    line_start = 0;
    for (i = 0; i <= nread; i++) {
        if (i == nread || buf[i] == '\n') {
            char *line = &buf[line_start];
            int line_len = i - line_start;

            if (line_len <= 0) {
                line_start = i + 1;
                continue;
            }

            /* Temporarily null-terminate this line */
            buf[i] = '\0';

            /* Skip meta lines */
            if (strstr(line, "\"type\":\"meta\"")) {
                line_start = i + 1;
                continue;
            }

            /* Only process turn lines */
            if (!strstr(line, "\"type\":\"turn\"")) {
                line_start = i + 1;
                continue;
            }

            /* Check block type */
            if (strstr(line, "\"block\":\"text\"")) {
                /* Extract role */
                char *role_p = strstr(line, "\"role\":\"");
                char *text_p = strstr(line, "\"text\":\"");
                int is_user = 0;

                if (role_p) {
                    role_p += 8; /* skip "role":" */
                    if (strncmp(role_p, "user", 4) == 0)
                        is_user = 1;
                }

                if (text_p && is_user) {
                    /* Extract text value (simplified: find text between quotes) */
                    char text_val[CONV_TEXT_BUF];
                    int ti = 0;
                    char *tp = text_p + 8; /* skip "text":" */

                    /* Copy until closing unescaped quote */
                    while (*tp && *tp != '"' && ti < CONV_TEXT_BUF - 1) {
                        if (*tp == '\\' && *(tp + 1)) {
                            char nc = *(tp + 1);
                            if (nc == 'n') {
                                text_val[ti++] = '\n';
                            } else if (nc == 'r') {
                                text_val[ti++] = '\r';
                            } else if (nc == 't') {
                                text_val[ti++] = '\t';
                            } else {
                                text_val[ti++] = nc;
                            }
                            tp += 2;
                        } else {
                            text_val[ti++] = *tp;
                            tp++;
                        }
                    }
                    text_val[ti] = '\0';

                    /* Add as user text turn */
                    conv_add_user_text(conv, text_val);
                }
                /* For assistant text turns, we add a minimal text block */
                if (text_p && !is_user) {
                    struct conv_turn *turn;
                    struct conv_block *blk;
                    char text_val[CONV_TEXT_BUF];
                    int ti = 0;
                    char *tp = text_p + 8;

                    if (conv->turn_count >= CONV_MAX_TURNS)
                        break;

                    while (*tp && *tp != '"' && ti < CONV_TEXT_BUF - 1) {
                        if (*tp == '\\' && *(tp + 1)) {
                            char nc = *(tp + 1);
                            if (nc == 'n')
                                text_val[ti++] = '\n';
                            else if (nc == 'r')
                                text_val[ti++] = '\r';
                            else if (nc == 't')
                                text_val[ti++] = '\t';
                            else
                                text_val[ti++] = nc;
                            tp += 2;
                        } else {
                            text_val[ti++] = *tp;
                            tp++;
                        }
                    }
                    text_val[ti] = '\0';

                    turn = &conv->turns[conv->turn_count];
                    memset(turn, 0, sizeof(*turn));
                    turn->role = "assistant";
                    turn->block_count = 1;

                    blk = &turn->blocks[0];
                    blk->type = CONV_BLOCK_TEXT;
                    blk->text.text_len = ti;
                    if (ti >= CONV_TEXT_BUF)
                        ti = CONV_TEXT_BUF - 1;
                    memcpy(blk->text.text, text_val, ti);
                    blk->text.text[ti] = '\0';

                    conv->turn_count++;
                }
            }
            /* tool_use and tool_result blocks are stored as summaries;
             * we skip them during reload to keep things simple. */

            line_start = i + 1;
        }
    }

#ifndef TEST_BUILD
    debug_printf("[SESSION] loaded session %s, %d turns\n",
                 session_id, conv->turn_count);
#endif
    return 0;
}

int session_list(struct session_index *index)
{
    if (!index)
        return -1;
    return session_read_index(index);
}

int session_delete(const char *session_id)
{
    char path[256];
    struct session_index index;
    int i, found;

    if (!session_id)
        return -1;

    /* Remove session file */
    session_build_path(path, sizeof(path), session_id);
    unlink(path);

    /* Update index: remove this session */
    if (session_read_index(&index) == 0) {
        found = -1;
        for (i = 0; i < index.count; i++) {
            if (strcmp(index.entries[i].id, session_id) == 0) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            /* Shift remaining entries */
            for (i = found; i < index.count - 1; i++) {
                memcpy(&index.entries[i], &index.entries[i + 1],
                       sizeof(struct session_meta));
            }
            index.count--;
            session_write_index(&index);
        }
    }

#ifndef TEST_BUILD
    debug_printf("[SESSION] deleted session %s\n", session_id);
#endif
    return 0;
}

int session_cleanup(int max_sessions)
{
    struct session_index index;
    int removed = 0;

    if (session_read_index(&index) != 0)
        return -1;

    /* Delete oldest sessions (at the beginning of the list) */
    while (index.count > max_sessions && index.count > 0) {
        char path[256];
        session_build_path(path, sizeof(path), index.entries[0].id);
        unlink(path);

        /* Shift entries */
        int i;
        for (i = 0; i < index.count - 1; i++) {
            memcpy(&index.entries[i], &index.entries[i + 1],
                   sizeof(struct session_meta));
        }
        index.count--;
        removed++;
    }

    if (removed > 0) {
        session_write_index(&index);
#ifndef TEST_BUILD
        debug_printf("[SESSION] cleanup: removed %d old sessions\n", removed);
#endif
    }

    return removed;
}
