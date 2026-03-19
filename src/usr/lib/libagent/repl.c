/*
 * repl.c - REPL/session helpers
 */

#include <agent/agent.h>
#include <agent/session.h>
#include <json.h>
#include <utf8.h>
#include <wcwidth.h>
#include <string.h>

#define AGENT_TOOL_JSON_TOKENS 64

static int append_chunk(char *out, int out_cap, int *pos,
                        const char *src, int len)
{
    if (!out || out_cap <= 0 || !pos || !src || len < 0)
        return -1;
    if (*pos + len >= out_cap)
        return -1;
    memcpy(out + *pos, src, (size_t)len);
    *pos += len;
    out[*pos] = '\0';
    return 0;
}

static int append_byte(char *out, int out_cap, int *pos, char ch)
{
    return append_chunk(out, out_cap, pos, &ch, 1);
}

static int parse_tool_result(const char *result_json, int result_len,
                             struct json_token *tokens, int token_cap)
{
    struct json_parser jp;

    if (!result_json || result_len <= 0 || !tokens || token_cap <= 0)
        return -1;

    json_init(&jp);
    return json_parse(&jp, result_json, result_len, tokens, token_cap);
}

static int is_sentence_break(u_int32_t codepoint)
{
    return codepoint == 0x3002U ||
           codepoint == 0xff01U ||
           codepoint == 0xff1fU;
}

int agent_resume_latest_for_cwd(const char *cwd,
                                char *session_id_out,
                                int session_id_cap)
{
    static struct session_index index;
    int i;

    if (!cwd || !session_id_out || session_id_cap <= 0)
        return -1;

    session_id_out[0] = '\0';
    if (session_list(&index) != 0)
        return -1;

    for (i = index.count - 1; i >= 0; i--) {
        if (strcmp(index.entries[i].cwd, cwd) == 0) {
            int len = strlen(index.entries[i].id);

            if (len >= session_id_cap)
                len = session_id_cap - 1;
            memcpy(session_id_out, index.entries[i].id, (size_t)len);
            session_id_out[len] = '\0';
            return 0;
        }
    }

    return -1;
}

void agent_text_layout_init(struct agent_text_layout *layout, int wrap_cols)
{
    if (!layout)
        return;
    if (wrap_cols < 20)
        wrap_cols = 80;
    memset(layout, 0, sizeof(*layout));
    layout->wrap_cols = wrap_cols;
}

int agent_text_layout_format(struct agent_text_layout *layout,
                             const char *text, int text_len,
                             char *out, int out_cap)
{
    int in_pos = 0;
    int out_pos = 0;

    if (!layout || !text || !out || out_cap <= 1)
        return -1;
    if (text_len < 0)
        text_len = strlen(text);

    out[0] = '\0';
    while (in_pos < text_len) {
        u_int32_t codepoint = 0;
        int consumed = 0;
        int width = 1;

        if (text[in_pos] == '\r') {
            in_pos++;
            continue;
        }

        if (text[in_pos] == '\n') {
            if (append_byte(out, out_cap, &out_pos, '\n') < 0)
                return -1;
            layout->current_col = 0;
            layout->skip_leading_space = 0;
            in_pos++;
            continue;
        }

        if (utf8_decode_one(text + in_pos, text_len - in_pos,
                            &codepoint, &consumed) == 1 &&
            consumed > 0) {
            width = unicode_wcwidth(codepoint);
            if (codepoint == '\t')
                width = 4;
            if (width < 0)
                width = 1;
        } else {
            codepoint = (unsigned char)text[in_pos];
            consumed = 1;
            if (codepoint == '\t')
                width = 4;
        }

        if (layout->current_col == 0 &&
            (codepoint == ' ' || codepoint == '\t')) {
            in_pos += consumed;
            continue;
        }

        if (layout->wrap_cols > 0 &&
            width > 0 &&
            layout->current_col > 0 &&
            layout->current_col + width > layout->wrap_cols) {
            if (append_byte(out, out_cap, &out_pos, '\n') < 0)
                return -1;
            layout->current_col = 0;
            layout->skip_leading_space = 0;
            if (codepoint == ' ' || codepoint == '\t') {
                in_pos += consumed;
                continue;
            }
        }

        if (append_chunk(out, out_cap, &out_pos,
                         text + in_pos, consumed) < 0)
            return -1;

        if (width > 0)
            layout->current_col += width;
        layout->skip_leading_space = 0;

        if (is_sentence_break(codepoint)) {
            if (append_byte(out, out_cap, &out_pos, '\n') < 0)
                return -1;
            layout->current_col = 0;
            layout->skip_leading_space = 1;
        }

        in_pos += consumed;
    }

    return out_pos;
}

int agent_tool_result_copy_string_field(const char *result_json, int result_len,
                                        const char *key,
                                        char *out, int out_cap)
{
    struct json_token tokens[AGENT_TOOL_JSON_TOKENS];
    int token_count;
    int tok;

    if (!result_json || result_len <= 0 || !key || !out || out_cap <= 0)
        return -1;

    token_count = parse_tool_result(result_json, result_len,
                                    tokens, AGENT_TOOL_JSON_TOKENS);
    if (token_count < 0)
        return -1;
    tok = json_find_key(result_json, tokens, token_count, 0, key);
    if (tok < 0 || tokens[tok].type != JSON_STRING)
        return -1;
    return json_token_str(result_json, &tokens[tok], out, out_cap);
}

int agent_tool_result_get_exit_code(const char *result_json, int result_len,
                                    int *exit_code)
{
    struct json_token tokens[AGENT_TOOL_JSON_TOKENS];
    int token_count;
    int tok;

    if (!result_json || result_len <= 0 || !exit_code)
        return -1;

    token_count = parse_tool_result(result_json, result_len,
                                    tokens, AGENT_TOOL_JSON_TOKENS);
    if (token_count < 0)
        return -1;
    tok = json_find_key(result_json, tokens, token_count, 0, "exit_code");
    if (tok < 0)
        return -1;
    return json_token_int(result_json, &tokens[tok], exit_code);
}

int agent_tool_result_is_failure(const char *result_json, int result_len,
                                 int is_error)
{
    int exit_code = 0;
    char error[128];

    if (is_error)
        return 1;
    if (!result_json || result_len <= 0)
        return 0;
    if (agent_tool_result_copy_string_field(result_json, result_len,
                                            "error",
                                            error, sizeof(error)) >= 0) {
        return 1;
    }
    if (agent_tool_result_get_exit_code(result_json, result_len,
                                        &exit_code) == 0 &&
        exit_code != 0) {
        return 1;
    }
    return 0;
}

int agent_tool_result_same_failure(const char *tool_name_a,
                                   const char *result_json_a,
                                   int result_len_a,
                                   int is_error_a,
                                   const char *tool_name_b,
                                   const char *result_json_b,
                                   int result_len_b,
                                   int is_error_b)
{
    if (is_error_a != is_error_b)
        return 0;

    if (!tool_name_a)
        tool_name_a = "";
    if (!tool_name_b)
        tool_name_b = "";
    if (strcmp(tool_name_a, tool_name_b) != 0)
        return 0;

    if (!result_json_a)
        result_json_a = "";
    if (!result_json_b)
        result_json_b = "";
    if (result_len_a < 0)
        result_len_a = strlen(result_json_a);
    if (result_len_b < 0)
        result_len_b = strlen(result_json_b);
    if (result_len_a != result_len_b)
        return 0;
    if (result_len_a <= 0)
        return 1;
    return memcmp(result_json_a, result_json_b, (size_t)result_len_a) == 0;
}
