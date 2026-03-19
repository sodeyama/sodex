/*
 * path_utils.c - agent 用 path / JSON helper
 */

#include <agent/path_utils.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#ifdef TEST_BUILD
#include <unistd.h>
#else
#include <stdlib.h>
#endif

static int safe_copy(char *dst, int cap, const char *src)
{
    int len;

    if (!dst || cap <= 0)
        return 0;
    if (!src)
        src = "";

    len = (int)strlen(src);
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    return len;
}

#ifndef TEST_BUILD
static int build_dentry_path(ext3_dentry *dentry, char *buf, int cap)
{
    int pos;
    int name_len;

    if (!dentry || !buf || cap <= 1)
        return -1;

    if (dentry->d_parent == 0 ||
        (dentry->d_namelen == 1 && dentry->d_name[0] == '/')) {
        buf[0] = '/';
        buf[1] = '\0';
        return 1;
    }

    pos = build_dentry_path(dentry->d_parent, buf, cap);
    if (pos < 0)
        return -1;

    if (pos > 1) {
        if (pos >= cap - 1)
            return -1;
        buf[pos++] = '/';
        buf[pos] = '\0';
    }

    name_len = dentry->d_namelen;
    if (name_len <= 0)
        return pos;
    if (pos + name_len >= cap)
        name_len = cap - pos - 1;
    if (name_len <= 0)
        return -1;

    memcpy(buf + pos, dentry->d_name, (size_t)name_len);
    pos += name_len;
    buf[pos] = '\0';
    return pos;
}
#endif

static int agent_get_current_path(char *buf, int cap)
{
#ifdef TEST_BUILD
    if (!buf || cap <= 1)
        return -1;
    if (!getcwd(buf, (size_t)cap))
        return -1;
    return (int)strlen(buf);
#else
    ext3_dentry *dentry;

    if (!buf || cap <= 1)
        return -1;
    dentry = getdentry();
    if (!dentry)
        return -1;
    return build_dentry_path(dentry, buf, cap);
#endif
}

int agent_json_get_string_field(const char *input_json, int input_len,
                                const char *key, char *out, int out_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;

    if (!input_json || !key || !out || out_cap <= 0)
        return -1;

    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0)
        return -1;

    tok = json_find_key(input_json, tokens, ntokens, 0, key);
    if (tok < 0)
        return -1;

    return json_token_str(input_json, &tokens[tok], out, out_cap);
}

int agent_json_get_int_field(const char *input_json, int input_len,
                             const char *key, int *out)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;

    if (!input_json || !key || !out)
        return -1;

    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0)
        return -1;

    tok = json_find_key(input_json, tokens, ntokens, 0, key);
    if (tok < 0)
        return -1;

    return json_token_int(input_json, &tokens[tok], out);
}

int agent_normalize_path(const char *path, char *out, int out_cap)
{
    const char *p;
    int len = 0;

    if (!path || !out || out_cap <= 1)
        return -1;
    if (path[0] != '/')
        return -1;

    out[len++] = '/';
    out[len] = '\0';
    p = path;

    while (*p) {
        const char *seg_start;
        int seg_len;

        while (*p == '/')
            p++;
        if (!*p)
            break;

        seg_start = p;
        while (*p && *p != '/')
            p++;
        seg_len = (int)(p - seg_start);

        if (seg_len == 1 && seg_start[0] == '.')
            continue;

        if (seg_len == 2 &&
            seg_start[0] == '.' &&
            seg_start[1] == '.') {
            if (len <= 1)
                return -1;

            len--;
            while (len > 1 && out[len - 1] != '/')
                len--;
            out[len] = '\0';
            continue;
        }

        if (len > 1) {
            if (len + 1 >= out_cap)
                return -1;
            out[len++] = '/';
        }
        if (len + seg_len >= out_cap)
            return -1;
        memcpy(out + len, seg_start, (size_t)seg_len);
        len += seg_len;
        out[len] = '\0';
    }

    if (len > 1 && out[len - 1] == '/') {
        len--;
        out[len] = '\0';
    }
    return len;
}

int agent_resolve_path(const char *path, char *out, int out_cap)
{
    char base[PATHNAME_MAX];
    char combined[PATHNAME_MAX];
    int len;

    if (!path || !out || out_cap <= 1)
        return -1;
    if (path[0] == '\0')
        return -1;
    if (path[0] == '/')
        return agent_normalize_path(path, out, out_cap);

    if (agent_get_current_path(base, sizeof(base)) < 0)
        safe_copy(base, sizeof(base), AGENT_DEFAULT_HOME);

    if (strcmp(base, "/") == 0)
        len = snprintf(combined, sizeof(combined), "/%s", path);
    else
        len = snprintf(combined, sizeof(combined), "%s/%s", base, path);
    if (len <= 0 || len >= (int)sizeof(combined))
        return -1;
    return agent_normalize_path(combined, out, out_cap);
}

int agent_json_get_normalized_path(const char *input_json, int input_len,
                                   const char *key, char *out, int out_cap)
{
    char raw[AGENT_PATH_MAX];

    if (agent_json_get_string_field(input_json, input_len,
                                    key, raw, sizeof(raw)) < 0)
        return -1;
    return agent_resolve_path(raw, out, out_cap);
}

int agent_path_join(const char *base, const char *name, char *out, int out_cap)
{
    char tmp[PATHNAME_MAX];
    int len;

    if (!base || !name || !out || out_cap <= 1)
        return -1;
    if (name[0] == '\0')
        return -1;

    if (strcmp(base, "/") == 0)
        len = snprintf(tmp, sizeof(tmp), "/%s", name);
    else
        len = snprintf(tmp, sizeof(tmp), "%s/%s", base, name);

    if (len <= 0 || len >= (int)sizeof(tmp))
        return -1;
    return agent_normalize_path(tmp, out, out_cap);
}

int agent_path_is_under(const char *path, const char *prefix)
{
    int plen;

    if (!path || !prefix)
        return 0;
    if (strcmp(prefix, "/") == 0)
        return path[0] == '/';

    plen = (int)strlen(prefix);
    if (strncmp(path, prefix, (size_t)plen) != 0)
        return 0;
    if (path[plen] == '\0')
        return 1;
    return path[plen] == '/';
}

int agent_write_error_json(char *result_buf, int result_cap,
                           const char *code, const char *message,
                           const char *path)
{
    struct json_writer jw;

    if (!result_buf || result_cap <= 0)
        return -1;

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "error");
    jw_object_start(&jw);
    jw_key(&jw, "code");
    jw_string(&jw, code ? code : "error");
    jw_key(&jw, "message");
    jw_string(&jw, message ? message : "operation failed");
    if (path && path[0] != '\0') {
        jw_key(&jw, "path");
        jw_string(&jw, path);
    }
    jw_object_end(&jw);
    jw_object_end(&jw);
    return jw_finish(&jw);
}
