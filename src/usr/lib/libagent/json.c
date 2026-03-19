/*
 * json.c - Minimal JSON parser and writer for freestanding environment
 *
 * Parser: jsmn-inspired tokenizer with parent links.
 * Writer: Buffer-based with overflow detection.
 * No malloc, no floating point.
 */

#include <json.h>
#include <string.h>

/* ================================================================
 * Parser
 * ================================================================ */

void json_init(struct json_parser *parser)
{
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
    parser->error = 0;
}

static struct json_token *alloc_token(struct json_parser *parser,
                                       struct json_token *tokens,
                                       int num_tokens)
{
    struct json_token *tok;
    if (parser->toknext >= num_tokens)
        return (struct json_token *)0;
    tok = &tokens[parser->toknext++];
    tok->type = JSON_NONE;
    tok->start = -1;
    tok->end = -1;
    tok->size = 0;
    tok->parent = -1;
    return tok;
}

static int parse_string(struct json_parser *parser,
                        const char *js, int len,
                        struct json_token *tokens, int num_tokens)
{
    struct json_token *tok;
    int start = parser->pos;

    /* Skip opening quote */
    parser->pos++;

    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];

        if (c == '\\') {
            parser->pos++;
            if (parser->pos >= len)
                return JSON_ERR_PARTIAL;
            /* Skip escaped char */
            switch (js[parser->pos]) {
            case '"': case '\\': case '/': case 'b':
            case 'f': case 'n': case 'r': case 't':
                break;
            case 'u':
                /* Skip 4 hex digits */
                {
                    int i;
                    for (i = 0; i < 4; i++) {
                        parser->pos++;
                        if (parser->pos >= len)
                            return JSON_ERR_PARTIAL;
                        c = js[parser->pos];
                        if (!((c >= '0' && c <= '9') ||
                              (c >= 'a' && c <= 'f') ||
                              (c >= 'A' && c <= 'F')))
                            return JSON_ERR_INVALID;
                    }
                }
                break;
            default:
                return JSON_ERR_INVALID;
            }
            continue;
        }

        if (c == '"') {
            tok = alloc_token(parser, tokens, num_tokens);
            if (!tok)
                return JSON_ERR_NOMEM;
            tok->type = JSON_STRING;
            tok->start = start + 1;  /* After opening quote */
            tok->end = parser->pos;  /* Before closing quote */
            tok->parent = parser->toksuper;
            return JSON_OK;
        }
    }

    return JSON_ERR_PARTIAL;
}

static int parse_primitive(struct json_parser *parser,
                           const char *js, int len,
                           struct json_token *tokens, int num_tokens)
{
    struct json_token *tok;
    int start = parser->pos;
    enum json_type type = JSON_NONE;
    char c;

    c = js[start];
    if (c == 't' || c == 'f')
        type = JSON_BOOL;
    else if (c == 'n')
        type = JSON_NULL;
    else if (c == '-' || (c >= '0' && c <= '9'))
        type = JSON_NUMBER;
    else
        return JSON_ERR_INVALID;

    for (; parser->pos < len; parser->pos++) {
        c = js[parser->pos];
        switch (c) {
        case '\t': case '\r': case '\n': case ' ':
        case ',': case ']': case '}': case ':':
            goto found;
        default:
            if (c < 32 || c >= 127)
                return JSON_ERR_INVALID;
            break;
        }
    }

found:
    tok = alloc_token(parser, tokens, num_tokens);
    if (!tok)
        return JSON_ERR_NOMEM;
    tok->type = type;
    tok->start = start;
    tok->end = parser->pos;
    tok->parent = parser->toksuper;
    parser->pos--;  /* Will be incremented by main loop */
    return JSON_OK;
}

int json_parse(struct json_parser *parser,
               const char *js, int len,
               struct json_token *tokens, int num_tokens)
{
    int r;
    struct json_token *tok;
    int i;

    if (!tokens || num_tokens <= 0)
        return JSON_ERR_NOMEM;

    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];

        switch (c) {
        case '{': case '[': {
            tok = alloc_token(parser, tokens, num_tokens);
            if (!tok)
                return JSON_ERR_NOMEM;
            if (parser->toksuper >= 0)
                tokens[parser->toksuper].size++;
            tok->type = (c == '{') ? JSON_OBJECT : JSON_ARRAY;
            tok->start = parser->pos;
            tok->parent = parser->toksuper;
            parser->toksuper = parser->toknext - 1;
            break;
        }

        case '}': case ']': {
            enum json_type type = (c == '}') ? JSON_OBJECT : JSON_ARRAY;

            /* Find the matching open bracket */
            for (i = parser->toknext - 1; i >= 0; i--) {
                if (tokens[i].type == type && tokens[i].end == -1) {
                    tokens[i].end = parser->pos + 1;
                    parser->toksuper = tokens[i].parent;
                    goto bracket_found;
                }
            }
            return JSON_ERR_INVALID;  /* Unmatched bracket */
        bracket_found:
            break;
        }

        case '"':
            r = parse_string(parser, js, len, tokens, num_tokens);
            if (r < 0)
                return r;
            if (parser->toksuper >= 0)
                tokens[parser->toksuper].size++;
            break;

        case '\t': case '\r': case '\n': case ' ':
            break;

        case ':':
            /* Key : value separator. The key string becomes a parent. */
            parser->toksuper = parser->toknext - 1;
            break;

        case ',':
            /* After a value, go back to the container */
            for (i = parser->toknext - 1; i >= 0; i--) {
                if (tokens[i].type == JSON_OBJECT || tokens[i].type == JSON_ARRAY) {
                    if (tokens[i].end == -1) {
                        parser->toksuper = i;
                        break;
                    }
                }
            }
            break;

        default:
            r = parse_primitive(parser, js, len, tokens, num_tokens);
            if (r < 0)
                return r;
            if (parser->toksuper >= 0)
                tokens[parser->toksuper].size++;
            break;
        }
    }

    /* Check for unclosed containers */
    for (i = 0; i < parser->toknext; i++) {
        if ((tokens[i].type == JSON_OBJECT || tokens[i].type == JSON_ARRAY)
            && tokens[i].end == -1)
            return JSON_ERR_PARTIAL;
    }

    return parser->toknext;
}

/* ================================================================
 * Accessor API
 * ================================================================ */

int json_token_eq(const char *js, const struct json_token *tok, const char *s)
{
    int toklen = tok->end - tok->start;
    int slen = strlen(s);
    if (toklen != slen)
        return 0;
    return (memcmp(js + tok->start, s, slen) == 0) ? 1 : 0;
}

/* Skip over a token and all its children. Returns index of next sibling. */
static int skip_token(const struct json_token *tokens, int token_count, int idx)
{
    if (idx < 0 || idx >= token_count)
        return token_count;

    if (tokens[idx].type == JSON_OBJECT) {
        int pairs = tokens[idx].size;
        int cur = idx + 1;
        int p;
        for (p = 0; p < pairs; p++) {
            cur = skip_token(tokens, token_count, cur);  /* skip key */
            cur = skip_token(tokens, token_count, cur);  /* skip value */
        }
        return cur;
    } else if (tokens[idx].type == JSON_ARRAY) {
        int elems = tokens[idx].size;
        int cur = idx + 1;
        int e;
        for (e = 0; e < elems; e++) {
            cur = skip_token(tokens, token_count, cur);
        }
        return cur;
    } else {
        return idx + 1;
    }
}

int json_find_key(const char *js,
                  const struct json_token *tokens, int token_count,
                  int obj_token, const char *key)
{
    int i, pairs;
    int cur;

    if (obj_token < 0 || obj_token >= token_count)
        return -1;
    if (tokens[obj_token].type != JSON_OBJECT)
        return -1;

    pairs = tokens[obj_token].size;
    cur = obj_token + 1;

    for (i = 0; i < pairs; i++) {
        if (cur >= token_count)
            return -1;
        /* cur points to key (string) */
        if (tokens[cur].type == JSON_STRING && json_token_eq(js, &tokens[cur], key)) {
            /* Return the value token (next after key) */
            return cur + 1;
        }
        /* Skip key + value */
        cur = skip_token(tokens, token_count, cur);   /* skip key */
        cur = skip_token(tokens, token_count, cur);   /* skip value */
    }
    return -1;
}

int json_array_get(const struct json_token *tokens, int token_count,
                   int array_token, int index)
{
    int cur, i;

    if (array_token < 0 || array_token >= token_count)
        return -1;
    if (tokens[array_token].type != JSON_ARRAY)
        return -1;
    if (index < 0 || index >= tokens[array_token].size)
        return -1;

    cur = array_token + 1;
    for (i = 0; i < index; i++) {
        cur = skip_token(tokens, token_count, cur);
        if (cur >= token_count)
            return -1;
    }
    return cur;
}

static int json_hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int json_parse_hex4(const char *src, u_int32_t *out)
{
    u_int32_t value = 0;
    int i;

    if (!src || !out)
        return -1;

    for (i = 0; i < 4; i++) {
        int digit = json_hex_value(src[i]);
        if (digit < 0)
            return -1;
        value = (value << 4) | (u_int32_t)digit;
    }

    *out = value;
    return 0;
}

static int json_append_utf8(char *out, int out_cap, int *pos, u_int32_t codepoint)
{
    int j;

    if (!out || !pos || out_cap <= 0)
        return -1;

    j = *pos;
    if (codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        codepoint = '?';
    }

    if (codepoint <= 0x7F) {
        if (j + 1 >= out_cap)
            return -1;
        out[j++] = (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        if (j + 2 >= out_cap)
            return -1;
        out[j++] = (char)(0xC0 | (codepoint >> 6));
        out[j++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        if (j + 3 >= out_cap)
            return -1;
        out[j++] = (char)(0xE0 | (codepoint >> 12));
        out[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[j++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        if (j + 4 >= out_cap)
            return -1;
        out[j++] = (char)(0xF0 | (codepoint >> 18));
        out[j++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[j++] = (char)(0x80 | (codepoint & 0x3F));
    }

    *pos = j;
    return 0;
}

int json_token_str(const char *js, const struct json_token *tok,
                   char *out, int out_cap)
{
    int toklen, i, j;

    if (!tok || tok->type != JSON_STRING || !out || out_cap <= 0)
        return -1;

    toklen = tok->end - tok->start;
    j = 0;

    for (i = 0; i < toklen && j < out_cap - 1; i++) {
        char c = js[tok->start + i];
        if (c == '\\' && i + 1 < toklen) {
            i++;
            c = js[tok->start + i];
            switch (c) {
            case '"':  out[j++] = '"'; break;
            case '\\': out[j++] = '\\'; break;
            case '/':  out[j++] = '/'; break;
            case 'b':  out[j++] = '\b'; break;
            case 'f':  out[j++] = '\f'; break;
            case 'n':  out[j++] = '\n'; break;
            case 'r':  out[j++] = '\r'; break;
            case 't':  out[j++] = '\t'; break;
            case 'u': {
                u_int32_t codepoint = 0;

                if (i + 4 >= toklen ||
                    json_parse_hex4(js + tok->start + i + 1, &codepoint) < 0) {
                    out[j++] = '?';
                    break;
                }
                i += 4;

                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    u_int32_t low = 0;

                    if (i + 6 < toklen &&
                        js[tok->start + i + 1] == '\\' &&
                        js[tok->start + i + 2] == 'u' &&
                        json_parse_hex4(js + tok->start + i + 3, &low) == 0 &&
                        low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 +
                            (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                        i += 6;
                    }
                }

                if (json_append_utf8(out, out_cap, &j, codepoint) < 0)
                    goto done;
                break;
            }
            default:
                out[j++] = c;
                break;
            }
        } else {
            out[j++] = c;
        }
    }
done:
    out[j] = '\0';
    return j;
}

int json_token_int(const char *js, const struct json_token *tok, int *out)
{
    int val = 0;
    int neg = 0;
    int i;

    if (!tok || tok->type != JSON_NUMBER || !out)
        return -1;

    i = tok->start;
    if (js[i] == '-') {
        neg = 1;
        i++;
    }
    for (; i < tok->end; i++) {
        char c = js[i];
        if (c < '0' || c > '9')
            break;  /* Stop at decimal point or exponent */
        val = val * 10 + (c - '0');
    }
    *out = neg ? -val : val;
    return 0;
}

int json_token_bool(const char *js, const struct json_token *tok, int *out)
{
    if (!tok || tok->type != JSON_BOOL || !out)
        return -1;

    if (js[tok->start] == 't')
        *out = 1;
    else
        *out = 0;
    return 0;
}

/* ================================================================
 * JSON Writer
 * ================================================================ */

static void jw_putc(struct json_writer *jw, char c)
{
    if (jw->len < jw->cap - 1)
        jw->buf[jw->len++] = c;
    else
        jw->error = 1;
}

static void jw_puts(struct json_writer *jw, const char *s)
{
    while (*s)
        jw_putc(jw, *s++);
}

static void jw_maybe_comma(struct json_writer *jw)
{
    if (jw->need_comma)
        jw_putc(jw, ',');
    jw->need_comma = 0;
}

static void jw_write_escaped(struct json_writer *jw, const char *s, int len)
{
    int i;
    jw_putc(jw, '"');
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  jw_putc(jw, '\\'); jw_putc(jw, '"'); break;
        case '\\': jw_putc(jw, '\\'); jw_putc(jw, '\\'); break;
        case '\b': jw_putc(jw, '\\'); jw_putc(jw, 'b'); break;
        case '\f': jw_putc(jw, '\\'); jw_putc(jw, 'f'); break;
        case '\n': jw_putc(jw, '\\'); jw_putc(jw, 'n'); break;
        case '\r': jw_putc(jw, '\\'); jw_putc(jw, 'r'); break;
        case '\t': jw_putc(jw, '\\'); jw_putc(jw, 't'); break;
        default:
            if (c < 0x20) {
                /* Control character: \u00XX */
                char hex[7];
                hex[0] = '\\'; hex[1] = 'u'; hex[2] = '0'; hex[3] = '0';
                hex[4] = "0123456789abcdef"[(c >> 4) & 0xf];
                hex[5] = "0123456789abcdef"[c & 0xf];
                hex[6] = '\0';
                jw_puts(jw, hex);
            } else {
                jw_putc(jw, c);
            }
            break;
        }
    }
    jw_putc(jw, '"');
}

void jw_init(struct json_writer *jw, char *buf, int cap)
{
    jw->buf = buf;
    jw->cap = cap;
    jw->len = 0;
    jw->error = 0;
    jw->need_comma = 0;
    jw->depth = 0;
}

void jw_object_start(struct json_writer *jw)
{
    jw_maybe_comma(jw);
    jw_putc(jw, '{');
    jw->need_comma = 0;
    jw->depth++;
}

void jw_object_end(struct json_writer *jw)
{
    jw_putc(jw, '}');
    jw->need_comma = 1;
    jw->depth--;
}

void jw_array_start(struct json_writer *jw)
{
    jw_maybe_comma(jw);
    jw_putc(jw, '[');
    jw->need_comma = 0;
    jw->depth++;
}

void jw_array_end(struct json_writer *jw)
{
    jw_putc(jw, ']');
    jw->need_comma = 1;
    jw->depth--;
}

void jw_key(struct json_writer *jw, const char *key)
{
    jw_maybe_comma(jw);
    jw_write_escaped(jw, key, strlen(key));
    jw_putc(jw, ':');
    jw->need_comma = 0;
}

void jw_string(struct json_writer *jw, const char *value)
{
    jw_maybe_comma(jw);
    jw_write_escaped(jw, value, strlen(value));
    jw->need_comma = 1;
}

void jw_string_n(struct json_writer *jw, const char *value, int len)
{
    jw_maybe_comma(jw);
    jw_write_escaped(jw, value, len);
    jw->need_comma = 1;
}

void jw_int(struct json_writer *jw, int value)
{
    char tmp[12];
    int i = 0;
    int neg = 0;
    u_int32_t uval;

    jw_maybe_comma(jw);

    if (value < 0) {
        neg = 1;
        uval = (u_int32_t)(-(value + 1)) + 1;
    } else {
        uval = (u_int32_t)value;
    }

    if (uval == 0) {
        tmp[i++] = '0';
    } else {
        while (uval > 0) {
            tmp[i++] = '0' + (uval % 10);
            uval /= 10;
        }
    }
    if (neg)
        jw_putc(jw, '-');
    while (i > 0)
        jw_putc(jw, tmp[--i]);

    jw->need_comma = 1;
}

void jw_bool(struct json_writer *jw, int value)
{
    jw_maybe_comma(jw);
    if (value)
        jw_puts(jw, "true");
    else
        jw_puts(jw, "false");
    jw->need_comma = 1;
}

void jw_null(struct json_writer *jw)
{
    jw_maybe_comma(jw);
    jw_puts(jw, "null");
    jw->need_comma = 1;
}

void jw_raw(struct json_writer *jw, const char *raw, int len)
{
    int i;
    jw_maybe_comma(jw);
    for (i = 0; i < len; i++)
        jw_putc(jw, raw[i]);
    jw->need_comma = 1;
}

int jw_finish(struct json_writer *jw)
{
    if (jw->error)
        return -1;
    if (jw->len < jw->cap)
        jw->buf[jw->len] = '\0';
    else {
        jw->buf[jw->cap - 1] = '\0';
        return -1;
    }
    return jw->len;
}
