/*
 * session.c - Session persistence for agent conversations
 *
 * 会話を append-only の JSONL として保存し、
 * text / tool_use / tool_result / compact / rename を復元する。
 */

#include <agent/session.h>
#include <agent/conversation.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fs.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

#define SESSION_LINE_MAX   16384
#define SESSION_INDEX_PATH SESSION_DIR "/index.txt"

/* ---- Internal helpers ---- */

static unsigned int s_id_counter = 1;

static void safe_copy(char *dst, int dst_cap, const char *src)
{
    int len;

    if (!dst || dst_cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_cap)
        len = dst_cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static unsigned int session_hash_path(const char *path)
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

static void session_build_path(char *buf, int cap, const char *session_id)
{
    snprintf(buf, (size_t)cap, "%s/%s.jsonl", SESSION_DIR, session_id);
}

static void session_ensure_dirs(void)
{
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
    mkdir(SESSION_DIR, 0755);
}

static int json_get_string(const char *js,
                           const struct json_token *tokens,
                           int token_count,
                           int obj_tok,
                           const char *key,
                           char *out, int out_cap)
{
    int tok;

    tok = json_find_key(js, tokens, token_count, obj_tok, key);
    if (tok < 0)
        return -1;
    return json_token_str(js, &tokens[tok], out, out_cap);
}

static int json_get_int_default(const char *js,
                                const struct json_token *tokens,
                                int token_count,
                                int obj_tok,
                                const char *key,
                                int defval)
{
    int tok;
    int out;

    tok = json_find_key(js, tokens, token_count, obj_tok, key);
    if (tok < 0)
        return defval;
    if (json_token_int(js, &tokens[tok], &out) < 0)
        return defval;
    return out;
}

static int json_get_bool_default(const char *js,
                                 const struct json_token *tokens,
                                 int token_count,
                                 int obj_tok,
                                 const char *key,
                                 int defval)
{
    int tok;
    int out;

    tok = json_find_key(js, tokens, token_count, obj_tok, key);
    if (tok < 0)
        return defval;
    if (json_token_bool(js, &tokens[tok], &out) < 0)
        return defval;
    return out;
}

static int json_copy_raw(const char *js,
                         const struct json_token *tok,
                         char *out, int out_cap)
{
    int len;

    if (!js || !tok || !out || out_cap <= 0)
        return -1;

    len = tok->end - tok->start;
    if (len >= out_cap)
        len = out_cap - 1;
    if (len < 0)
        return -1;

    memcpy(out, js + tok->start, len);
    out[len] = '\0';
    return len;
}

static int session_write_index(const struct session_index *index)
{
    int fd;
    int i;
    char line[SESSION_ID_LEN + 2];
    int len;

    fd = open(SESSION_INDEX_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    for (i = 0; i < index->count; i++) {
        len = snprintf(line, sizeof(line), "%s\n", index->entries[i].id);
        if (len > 0)
            write(fd, line, (size_t)len);
    }

    close(fd);
    return 0;
}

static int session_read_index(struct session_index *index)
{
    static char buf[2048];
    int fd;
    int nread;
    int line_start;
    int i;

    if (!index)
        return -1;

    memset(index, 0, sizeof(*index));

    fd = open(SESSION_INDEX_PATH, O_RDONLY, 0);
    if (fd < 0)
        return 0;

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (nread <= 0)
        return 0;

    buf[nread] = '\0';
    line_start = 0;
    for (i = 0; i <= nread && index->count < SESSION_MAX_SESSIONS; i++) {
        int line_len;

        if (i != nread && buf[i] != '\n')
            continue;

        line_len = i - line_start;
        if (line_len > 0 && line_len <= SESSION_ID_LEN) {
            memcpy(index->entries[index->count].id, &buf[line_start], (size_t)line_len);
            index->entries[index->count].id[line_len] = '\0';
            index->count++;
        }
        line_start = i + 1;
    }

    return 0;
}

static void session_mark_recent(const char *session_id)
{
    struct session_index index;
    int i;
    int found = -1;

    if (!session_id)
        return;

    if (session_read_index(&index) != 0)
        return;

    for (i = 0; i < index.count; i++) {
        if (strcmp(index.entries[i].id, session_id) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        if (index.count >= SESSION_MAX_SESSIONS)
            return;
        safe_copy(index.entries[index.count].id,
                  sizeof(index.entries[index.count].id),
                  session_id);
        index.count++;
        session_write_index(&index);
        return;
    }

    if (found == index.count - 1)
        return;

    {
        struct session_meta tmp;

        memcpy(&tmp, &index.entries[found], sizeof(tmp));
        for (i = found; i < index.count - 1; i++) {
            memcpy(&index.entries[i], &index.entries[i + 1],
                   sizeof(struct session_meta));
        }
        memcpy(&index.entries[index.count - 1], &tmp, sizeof(tmp));
    }

    session_write_index(&index);
}

static int session_append_line(const char *session_id,
                               const char *line,
                               int len)
{
    char path[256];
    int fd;

    if (!session_id || !line || len <= 0)
        return -1;

    session_build_path(path, sizeof(path), session_id);
    fd = open(path, O_WRONLY | O_APPEND, 0);
    if (fd < 0)
        return -1;

    write(fd, line, (size_t)len);
    close(fd);
    session_mark_recent(session_id);
    return 0;
}

static int session_write_message_json(char *buf, int cap,
                                      const struct conv_turn *turn,
                                      int input_tokens, int output_tokens)
{
    struct json_writer jw;
    int len;
    int i;

    jw_init(&jw, buf, cap);
    jw_object_start(&jw);
    jw_key(&jw, "type");
    jw_string(&jw, "message");
    jw_key(&jw, "role");
    jw_string(&jw, turn->role ? turn->role : "user");
    jw_key(&jw, "content");
    jw_array_start(&jw);

    for (i = 0; i < turn->block_count; i++) {
        const struct conv_block *blk = &turn->blocks[i];

        jw_object_start(&jw);
        if (blk->type == CONV_BLOCK_TEXT) {
            jw_key(&jw, "type");
            jw_string(&jw, "text");
            jw_key(&jw, "text");
            jw_string_n(&jw, blk->text.text, blk->text.text_len);
        } else if (blk->type == CONV_BLOCK_TOOL_USE) {
            jw_key(&jw, "type");
            jw_string(&jw, "tool_use");
            jw_key(&jw, "id");
            jw_string(&jw, blk->tool_use.id);
            jw_key(&jw, "name");
            jw_string(&jw, blk->tool_use.name);
            jw_key(&jw, "input");
            if (blk->tool_use.input_json_len > 0)
                jw_raw(&jw, blk->tool_use.input_json, blk->tool_use.input_json_len);
            else
                jw_raw(&jw, "{}", 2);
        } else if (blk->type == CONV_BLOCK_TOOL_RESULT) {
            jw_key(&jw, "type");
            jw_string(&jw, "tool_result");
            jw_key(&jw, "tool_use_id");
            jw_string(&jw, blk->tool_result.tool_use_id);
            jw_key(&jw, "content");
            jw_string_n(&jw, blk->tool_result.content,
                        blk->tool_result.content_len);
            jw_key(&jw, "is_error");
            jw_bool(&jw, blk->tool_result.is_error);
        }
        jw_object_end(&jw);
    }

    jw_array_end(&jw);
    jw_key(&jw, "input_tokens");
    jw_int(&jw, input_tokens);
    jw_key(&jw, "output_tokens");
    jw_int(&jw, output_tokens);
    jw_object_end(&jw);
    len = jw_finish(&jw);
    if (len < 0 || len >= cap - 1)
        return -1;
    buf[len++] = '\n';
    buf[len] = '\0';
    return len;
}

static int session_load_message_line(const char *line,
                                     struct conversation *conv,
                                     const struct json_token *tokens,
                                     int token_count)
{
    int role_tok;
    int content_tok;
    int i;
    struct conv_turn *turn;

    if (!line || !conv)
        return -1;
    if (conv->turn_count >= CONV_MAX_TURNS)
        return -1;

    role_tok = json_find_key(line, tokens, token_count, 0, "role");
    content_tok = json_find_key(line, tokens, token_count, 0, "content");
    if (role_tok < 0 || content_tok < 0)
        return -1;

    turn = &conv->turns[conv->turn_count];
    memset(turn, 0, sizeof(*turn));
    turn->role = json_token_eq(line, &tokens[role_tok], "assistant")
               ? "assistant" : "user";

    for (i = 0; i < tokens[content_tok].size && i < CONV_MAX_BLOCKS; i++) {
        int blk_tok;
        int type_tok;
        char type[32];
        struct conv_block *blk;

        blk_tok = json_array_get(tokens, token_count, content_tok, i);
        if (blk_tok < 0)
            break;
        type_tok = json_find_key(line, tokens, token_count, blk_tok, "type");
        if (type_tok < 0)
            continue;
        if (json_token_str(line, &tokens[type_tok], type, sizeof(type)) < 0)
            continue;

        blk = &turn->blocks[turn->block_count];
        memset(blk, 0, sizeof(*blk));

        if (strcmp(type, "text") == 0) {
            blk->type = CONV_BLOCK_TEXT;
            blk->text.text_len = json_get_string(
                line, tokens, token_count, blk_tok,
                "text", blk->text.text, sizeof(blk->text.text));
            if (blk->text.text_len < 0)
                blk->text.text_len = 0;
            turn->block_count++;
        } else if (strcmp(type, "tool_use") == 0) {
            int input_tok;

            blk->type = CONV_BLOCK_TOOL_USE;
            json_get_string(line, tokens, token_count, blk_tok,
                            "id", blk->tool_use.id, sizeof(blk->tool_use.id));
            json_get_string(line, tokens, token_count, blk_tok,
                            "name", blk->tool_use.name, sizeof(blk->tool_use.name));
            input_tok = json_find_key(line, tokens, token_count, blk_tok, "input");
            if (input_tok >= 0) {
                blk->tool_use.input_json_len = json_copy_raw(
                    line, &tokens[input_tok],
                    blk->tool_use.input_json,
                    sizeof(blk->tool_use.input_json));
            }
            if (blk->tool_use.input_json_len < 0)
                blk->tool_use.input_json_len = 0;
            turn->block_count++;
        } else if (strcmp(type, "tool_result") == 0) {
            blk->type = CONV_BLOCK_TOOL_RESULT;
            json_get_string(line, tokens, token_count, blk_tok,
                            "tool_use_id",
                            blk->tool_result.tool_use_id,
                            sizeof(blk->tool_result.tool_use_id));
            blk->tool_result.content_len = json_get_string(
                line, tokens, token_count, blk_tok,
                "content", blk->tool_result.content,
                sizeof(blk->tool_result.content));
            if (blk->tool_result.content_len < 0)
                blk->tool_result.content_len = 0;
            blk->tool_result.is_error = json_get_bool_default(
                line, tokens, token_count, blk_tok, "is_error", 0);
            turn->block_count++;
        }
    }

    conv->turn_count++;
    conv->total_input_tokens += json_get_int_default(
        line, tokens, token_count, 0, "input_tokens", 0);
    conv->total_output_tokens += json_get_int_default(
        line, tokens, token_count, 0, "output_tokens", 0);
    return 0;
}

/* ---- Public API ---- */

void session_generate_id(char *id_buf)
{
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int d;

    a = s_id_counter++;
    b = a * 2654435761U;
    c = b ^ 0xDEADBEEF;
    d = a * 7U + 0x1234U;

    snprintf(id_buf, SESSION_ID_LEN + 1, "%08x%08x%08x%08x", a, b, c, d);
}

int session_create(struct session_meta *meta,
                    const char *model,
                    const char *cwd)
{
    char path[256];
    char line[1024];
    struct json_writer jw;
    int fd;
    struct session_index index;

    if (!meta)
        return -1;

    session_ensure_dirs();
    memset(meta, 0, sizeof(*meta));
    session_generate_id(meta->id);
    safe_copy(meta->name, sizeof(meta->name), "main");
    safe_copy(meta->cwd, sizeof(meta->cwd), cwd ? cwd : "/");
    meta->cwd_hash = session_hash_path(meta->cwd);
    safe_copy(meta->model, sizeof(meta->model), model ? model : "");

    jw_init(&jw, line, sizeof(line));
    jw_object_start(&jw);
    jw_key(&jw, "type");
    jw_string(&jw, "meta");
    jw_key(&jw, "id");
    jw_string(&jw, meta->id);
    jw_key(&jw, "name");
    jw_string(&jw, meta->name);
    jw_key(&jw, "cwd");
    jw_string(&jw, meta->cwd);
    jw_key(&jw, "cwd_hash");
    jw_int(&jw, (int)meta->cwd_hash);
    jw_key(&jw, "model");
    jw_string(&jw, meta->model);
    jw_object_end(&jw);
    fd = jw_finish(&jw);
    if (fd < 0 || fd >= (int)sizeof(line) - 1)
        return -1;
    line[fd++] = '\n';
    line[fd] = '\0';

    session_build_path(path, sizeof(path), meta->id);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    write(fd, line, (size_t)strlen(line));
    close(fd);

    session_read_index(&index);
    if (index.count < SESSION_MAX_SESSIONS) {
        safe_copy(index.entries[index.count].id,
                  sizeof(index.entries[index.count].id), meta->id);
        index.count++;
        session_write_index(&index);
    }

    debug_printf("[SESSION] created session %s cwd=%s\n", meta->id, meta->cwd);
    return 0;
}

int session_append_turn(const char *session_id,
                         const struct conv_turn *turn,
                         int input_tokens, int output_tokens)
{
    static char line[SESSION_LINE_MAX];
    int len;

    if (!session_id || !turn)
        return -1;

    len = session_write_message_json(line, sizeof(line), turn,
                                     input_tokens, output_tokens);
    if (len < 0)
        return -1;
    return session_append_line(session_id, line, len);
}

int session_append_compact(const char *session_id,
                            const char *summary,
                            int from_turn, int to_turn)
{
    char line[2048];
    struct json_writer jw;
    int len;

    if (!session_id || !summary)
        return -1;

    jw_init(&jw, line, sizeof(line));
    jw_object_start(&jw);
    jw_key(&jw, "type");
    jw_string(&jw, "compact");
    jw_key(&jw, "summary");
    jw_string(&jw, summary);
    jw_key(&jw, "from_turn");
    jw_int(&jw, from_turn);
    jw_key(&jw, "to_turn");
    jw_int(&jw, to_turn);
    jw_object_end(&jw);
    len = jw_finish(&jw);
    if (len < 0 || len >= (int)sizeof(line) - 1)
        return -1;
    line[len++] = '\n';
    line[len] = '\0';

    return session_append_line(session_id, line, len);
}

int session_append_rename(const char *session_id, const char *name)
{
    char line[512];
    struct json_writer jw;
    int len;

    if (!session_id || !name)
        return -1;

    jw_init(&jw, line, sizeof(line));
    jw_object_start(&jw);
    jw_key(&jw, "type");
    jw_string(&jw, "rename");
    jw_key(&jw, "name");
    jw_string(&jw, name);
    jw_object_end(&jw);
    len = jw_finish(&jw);
    if (len < 0 || len >= (int)sizeof(line) - 1)
        return -1;
    line[len++] = '\n';
    line[len] = '\0';

    return session_append_line(session_id, line, len);
}

int session_read_meta(const char *session_id, struct session_meta *meta)
{
    char path[256];
    static char buf[SESSION_MAX_FILE];
    int fd;
    int nread;
    int line_start;
    int i;
    int meta_seen = 0;

    if (!session_id || !meta)
        return -1;

    memset(meta, 0, sizeof(*meta));
    safe_copy(meta->id, sizeof(meta->id), session_id);

    session_build_path(path, sizeof(path), session_id);
    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (nread <= 0)
        return -1;

    buf[nread] = '\0';
    line_start = 0;
    for (i = 0; i <= nread; i++) {
        int line_len;
        int ntokens;
        struct json_parser jp;
        struct json_token tokens[256];
        char type[32];

        if (i != nread && buf[i] != '\n')
            continue;

        line_len = i - line_start;
        if (line_len <= 0) {
            line_start = i + 1;
            continue;
        }

        buf[i] = '\0';
        json_init(&jp);
        ntokens = json_parse(&jp, &buf[line_start], line_len, tokens, 256);
        if (ntokens < 0) {
            line_start = i + 1;
            continue;
        }
        if (json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "type", type, sizeof(type)) < 0) {
            line_start = i + 1;
            continue;
        }

        if (strcmp(type, "meta") == 0) {
            json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "id", meta->id, sizeof(meta->id));
            json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "name", meta->name, sizeof(meta->name));
            json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "cwd", meta->cwd, sizeof(meta->cwd));
            meta->cwd_hash = (unsigned int)json_get_int_default(
                &buf[line_start], tokens, ntokens, 0, "cwd_hash", 0);
            json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "model", meta->model, sizeof(meta->model));
            meta_seen = 1;
        } else if (strcmp(type, "message") == 0) {
            meta->turn_count++;
            meta->total_tokens += json_get_int_default(
                &buf[line_start], tokens, ntokens, 0, "input_tokens", 0);
            meta->total_tokens += json_get_int_default(
                &buf[line_start], tokens, ntokens, 0, "output_tokens", 0);
        } else if (strcmp(type, "rename") == 0) {
            json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "name", meta->name, sizeof(meta->name));
        } else if (strcmp(type, "compact") == 0) {
            meta->compact_count++;
            json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "summary", meta->summary, sizeof(meta->summary));
        }

        line_start = i + 1;
    }

    if (!meta_seen)
        return -1;
    if (meta->name[0] == '\0')
        safe_copy(meta->name, sizeof(meta->name), "main");
    return 0;
}

int session_load(const char *session_id, struct conversation *conv)
{
    char path[256];
    static char buf[SESSION_MAX_FILE];
    int fd;
    int nread;
    int line_start;
    int i;

    if (!session_id || !conv)
        return -1;

    session_build_path(path, sizeof(path), session_id);
    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (nread <= 0)
        return -1;

    buf[nread] = '\0';
    line_start = 0;
    for (i = 0; i <= nread; i++) {
        int line_len;
        int ntokens;
        struct json_parser jp;
        struct json_token tokens[256];
        char type[32];

        if (i != nread && buf[i] != '\n')
            continue;

        line_len = i - line_start;
        if (line_len <= 0) {
            line_start = i + 1;
            continue;
        }

        buf[i] = '\0';
        json_init(&jp);
        ntokens = json_parse(&jp, &buf[line_start], line_len, tokens, 256);
        if (ntokens < 0) {
            line_start = i + 1;
            continue;
        }
        if (json_get_string(&buf[line_start], tokens, ntokens, 0,
                            "type", type, sizeof(type)) < 0) {
            line_start = i + 1;
            continue;
        }

        if (strcmp(type, "message") == 0) {
            session_load_message_line(&buf[line_start], conv, tokens, ntokens);
        } else if (strcmp(type, "compact") == 0) {
            char summary[SESSION_SUMMARY_LEN];

            if (json_get_string(&buf[line_start], tokens, ntokens, 0,
                                "summary", summary, sizeof(summary)) >= 0) {
                conv_append_system_text(conv, "Compact Summary", summary);
            }
        }

        line_start = i + 1;
    }

    debug_printf("[SESSION] loaded session %s turns=%d\n",
                 session_id, conv->turn_count);
    return 0;
}

int session_list(struct session_index *index)
{
    struct session_index ids;
    int i;

    if (!index)
        return -1;

    memset(index, 0, sizeof(*index));
    if (session_read_index(&ids) != 0)
        return -1;

    for (i = 0; i < ids.count && index->count < SESSION_MAX_SESSIONS; i++) {
        if (session_read_meta(ids.entries[i].id,
                              &index->entries[index->count]) == 0) {
            index->count++;
        } else {
            safe_copy(index->entries[index->count].id,
                      sizeof(index->entries[index->count].id),
                      ids.entries[i].id);
            safe_copy(index->entries[index->count].name,
                      sizeof(index->entries[index->count].name), "main");
            index->count++;
        }
    }

    return 0;
}

int session_delete(const char *session_id)
{
    char path[256];
    struct session_index index;
    int i;
    int found = -1;

    if (!session_id)
        return -1;

    session_build_path(path, sizeof(path), session_id);
    unlink(path);

    if (session_read_index(&index) == 0) {
        for (i = 0; i < index.count; i++) {
            if (strcmp(index.entries[i].id, session_id) == 0) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            for (i = found; i < index.count - 1; i++) {
                memcpy(&index.entries[i], &index.entries[i + 1],
                       sizeof(struct session_meta));
            }
            index.count--;
            session_write_index(&index);
        }
    }

    return 0;
}

int session_cleanup(int max_sessions)
{
    struct session_index index;
    int removed = 0;

    if (session_read_index(&index) != 0)
        return -1;

    while (index.count > max_sessions && index.count > 0) {
        char path[256];
        int i;

        session_build_path(path, sizeof(path), index.entries[0].id);
        unlink(path);
        for (i = 0; i < index.count - 1; i++) {
            memcpy(&index.entries[i], &index.entries[i + 1],
                   sizeof(struct session_meta));
        }
        index.count--;
        removed++;
    }

    if (removed > 0)
        session_write_index(&index);
    return removed;
}
