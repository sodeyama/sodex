/*
 * conversation.c - Multi-turn conversation management
 *
 * Manages conversation history (turns with content blocks) and
 * serializes to Claude Messages API JSON format.
 */

#include <agent/conversation.h>
#include <agent/claude_adapter.h>
#include <agent/tool_dispatch.h>
#include <json.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#endif

/* ---- Helpers ---- */

/* Safe string copy with truncation. Always null-terminates.
 * Returns number of bytes copied (excluding NUL). */
static int safe_copy(char *dst, int dst_cap, const char *src, int src_len)
{
    int copy_len;

    if (dst_cap <= 0)
        return 0;
    if (src_len < 0)
        src_len = strlen(src);
    copy_len = src_len;
    if (copy_len >= dst_cap)
        copy_len = dst_cap - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return copy_len;
}

static int append_text(char *dst, int cap, const char *text)
{
    int cur;
    int len;

    if (!dst || !text || cap <= 0)
        return -1;
    cur = strlen(dst);
    len = strlen(text);
    if (cur + len >= cap)
        len = cap - cur - 1;
    if (len <= 0)
        return -1;
    memcpy(dst + cur, text, (size_t)len);
    dst[cur + len] = '\0';
    return len;
}

/* ---- Public API ---- */

void conv_init(struct conversation *conv, const char *system_prompt)
{
    memset(conv, 0, sizeof(*conv));
    if (system_prompt) {
        conv->system_prompt_len = safe_copy(
            conv->system_prompt, CONV_SYSTEM_PROMPT_BUF,
            system_prompt, strlen(system_prompt));
    }
}

int conv_append_system_text(struct conversation *conv,
                             const char *header,
                             const char *text)
{
    int remaining;
    int add_len = 0;

    if (!conv || !text)
        return -1;

    remaining = CONV_SYSTEM_PROMPT_BUF - conv->system_prompt_len - 1;
    if (remaining <= 0)
        return -1;

    if (header && *header) {
        const char *prefix = "\n\n== ";
        const char *suffix = " ==\n";
        int prefix_len = strlen(prefix);
        int header_len = strlen(header);
        int suffix_len = strlen(suffix);

        if (prefix_len + header_len + suffix_len > remaining)
            return -1;

        memcpy(conv->system_prompt + conv->system_prompt_len,
               prefix, prefix_len);
        conv->system_prompt_len += prefix_len;
        memcpy(conv->system_prompt + conv->system_prompt_len,
               header, header_len);
        conv->system_prompt_len += header_len;
        memcpy(conv->system_prompt + conv->system_prompt_len,
               suffix, suffix_len);
        conv->system_prompt_len += suffix_len;
        remaining = CONV_SYSTEM_PROMPT_BUF - conv->system_prompt_len - 1;
    }

    add_len = strlen(text);
    if (add_len > remaining)
        add_len = remaining;
    if (add_len <= 0)
        return -1;

    memcpy(conv->system_prompt + conv->system_prompt_len, text, add_len);
    conv->system_prompt_len += add_len;
    conv->system_prompt[conv->system_prompt_len] = '\0';
    return add_len;
}

int conv_add_user_text(struct conversation *conv, const char *text)
{
    struct conv_turn *turn;
    struct conv_block *blk;
    int text_len;

    if (!conv || !text)
        return -1;
    if (conv->turn_count >= CONV_MAX_TURNS) {
#ifndef TEST_BUILD
        debug_printf("[CONV] turn limit reached (%d)\n", CONV_MAX_TURNS);
#endif
        return -1;
    }

    turn = &conv->turns[conv->turn_count];
    memset(turn, 0, sizeof(*turn));
    turn->role = "user";
    turn->block_count = 1;

    blk = &turn->blocks[0];
    blk->type = CONV_BLOCK_TEXT;
    text_len = strlen(text);
    blk->text.text_len = safe_copy(
        blk->text.text, CONV_TEXT_BUF, text, text_len);

    conv->turn_count++;
    return 0;
}

int conv_add_assistant_response(struct conversation *conv,
                                 const struct claude_response *resp)
{
    struct conv_turn *turn;
    struct conv_block *blk;
    int i;

    if (!conv || !resp)
        return -1;
    if (conv->turn_count >= CONV_MAX_TURNS) {
#ifndef TEST_BUILD
        debug_printf("[CONV] turn limit reached (%d)\n", CONV_MAX_TURNS);
#endif
        return -1;
    }
    if (resp->block_count <= 0)
        return -1;

    turn = &conv->turns[conv->turn_count];
    memset(turn, 0, sizeof(*turn));
    turn->role = "assistant";
    turn->block_count = 0;

    for (i = 0; i < resp->block_count && i < CONV_MAX_BLOCKS; i++) {
        const struct claude_content_block *src = &resp->blocks[i];
        blk = &turn->blocks[turn->block_count];
        memset(blk, 0, sizeof(*blk));

        if (src->type == CLAUDE_CONTENT_TEXT) {
            blk->type = CONV_BLOCK_TEXT;
            blk->text.text_len = safe_copy(
                blk->text.text, CONV_TEXT_BUF,
                src->text.text, src->text.text_len);
            turn->block_count++;
        } else if (src->type == CLAUDE_CONTENT_TOOL_USE) {
            blk->type = CONV_BLOCK_TOOL_USE;
            safe_copy(blk->tool_use.id, CLAUDE_MAX_TOOL_ID,
                      src->tool_use.id, strlen(src->tool_use.id));
            safe_copy(blk->tool_use.name, CLAUDE_MAX_TOOL_NAME,
                      src->tool_use.name, strlen(src->tool_use.name));
            blk->tool_use.input_json_len = safe_copy(
                blk->tool_use.input_json, CLAUDE_MAX_TOOL_INPUT,
                src->tool_use.input_json, src->tool_use.input_json_len);
            turn->block_count++;
        }
    }

    conv->turn_count++;
    return 0;
}

int conv_add_tool_results(struct conversation *conv,
                           const struct tool_dispatch_result *results,
                           int result_count)
{
    struct conv_turn *turn;
    struct conv_block *blk;
    int i;

    if (!conv || !results || result_count <= 0)
        return -1;
    if (conv->turn_count >= CONV_MAX_TURNS) {
#ifndef TEST_BUILD
        debug_printf("[CONV] turn limit reached (%d)\n", CONV_MAX_TURNS);
#endif
        return -1;
    }

    turn = &conv->turns[conv->turn_count];
    memset(turn, 0, sizeof(*turn));
    turn->role = "user";
    turn->block_count = 0;

    for (i = 0; i < result_count && i < CONV_MAX_BLOCKS; i++) {
        const struct tool_dispatch_result *res = &results[i];
        blk = &turn->blocks[turn->block_count];
        memset(blk, 0, sizeof(*blk));

        blk->type = CONV_BLOCK_TOOL_RESULT;
        safe_copy(blk->tool_result.tool_use_id, CLAUDE_MAX_TOOL_ID,
                  res->tool_use_id, strlen(res->tool_use_id));
        blk->tool_result.content_len = safe_copy(
            blk->tool_result.content, CONV_TEXT_BUF,
            res->result_json, res->result_len);
        blk->tool_result.is_error = res->is_error;
        turn->block_count++;
    }

    conv->turn_count++;
    return 0;
}

int conv_build_messages_json(const struct conversation *conv,
                              struct json_writer *jw)
{
    int t, b;

    if (!conv || !jw)
        return -1;

    jw_array_start(jw);

    for (t = 0; t < conv->turn_count; t++) {
        const struct conv_turn *turn = &conv->turns[t];

        jw_object_start(jw);

        jw_key(jw, "role");
        jw_string(jw, turn->role);

        jw_key(jw, "content");
        jw_array_start(jw);

        for (b = 0; b < turn->block_count; b++) {
            const struct conv_block *blk = &turn->blocks[b];

            jw_object_start(jw);

            switch (blk->type) {
            case CONV_BLOCK_TEXT:
                jw_key(jw, "type");
                jw_string(jw, "text");
                jw_key(jw, "text");
                jw_string_n(jw, blk->text.text, blk->text.text_len);
                break;

            case CONV_BLOCK_TOOL_USE:
                jw_key(jw, "type");
                jw_string(jw, "tool_use");
                jw_key(jw, "id");
                jw_string(jw, blk->tool_use.id);
                jw_key(jw, "name");
                jw_string(jw, blk->tool_use.name);
                jw_key(jw, "input");
                /* input is raw JSON, emit directly */
                if (blk->tool_use.input_json_len > 0) {
                    jw_raw(jw, blk->tool_use.input_json,
                           blk->tool_use.input_json_len);
                } else {
                    jw_raw(jw, "{}", 2);
                }
                break;

            case CONV_BLOCK_TOOL_RESULT:
                jw_key(jw, "type");
                jw_string(jw, "tool_result");
                jw_key(jw, "tool_use_id");
                jw_string(jw, blk->tool_result.tool_use_id);
                jw_key(jw, "content");
                jw_string_n(jw, blk->tool_result.content,
                            blk->tool_result.content_len);
                if (blk->tool_result.is_error) {
                    jw_key(jw, "is_error");
                    jw_bool(jw, 1);
                }
                break;
            }

            jw_object_end(jw);
        }

        jw_array_end(jw);
        jw_object_end(jw);
    }

    jw_array_end(jw);

    if (jw->error)
        return -1;
    return 0;
}

void conv_update_tokens(struct conversation *conv,
                         const struct claude_response *resp)
{
    if (!conv || !resp)
        return;
    conv->total_input_tokens += resp->input_tokens;
    conv->total_output_tokens += resp->output_tokens;
}

int conv_check_tokens(const struct conversation *conv)
{
    int total;

    if (!conv)
        return 2;
    total = conv->total_input_tokens + conv->total_output_tokens;
    if (total >= CONV_TOKEN_LIMIT)
        return 2;
    if (total >= CONV_TOKEN_WARNING)
        return 1;
    return 0;
}

int conv_truncate_oldest(struct conversation *conv, int keep_count)
{
    int remove_count;
    int i;

    if (!conv || keep_count < 0)
        return -1;
    if (keep_count >= conv->turn_count)
        return 0;  /* Nothing to remove */

    remove_count = conv->turn_count - keep_count;

    /* Shift remaining turns to the front */
    for (i = 0; i < keep_count; i++) {
        memcpy(&conv->turns[i], &conv->turns[remove_count + i],
               sizeof(struct conv_turn));
    }

    /* Zero out freed slots */
    for (i = keep_count; i < conv->turn_count; i++) {
        memset(&conv->turns[i], 0, sizeof(struct conv_turn));
    }

    conv->turn_count = keep_count;

#ifndef TEST_BUILD
    debug_printf("[CONV] truncated %d oldest turns, %d remaining\n",
                remove_count, keep_count);
#endif
    return remove_count;
}

int conv_compact(struct conversation *conv,
                 int keep_count,
                 const char *focus,
                 char *summary,
                 int summary_cap)
{
    int keep_from;
    int i;

    if (!conv || !summary || summary_cap <= 0)
        return -1;
    summary[0] = '\0';

    if (keep_count < 0)
        keep_count = 0;
    if (keep_count >= conv->turn_count)
        return 0;

    if (focus && *focus) {
        append_text(summary, summary_cap, "focus: ");
        append_text(summary, summary_cap, focus);
        append_text(summary, summary_cap, "\n");
    }

    keep_from = conv->turn_count - keep_count;
    if (keep_from < 0)
        keep_from = 0;

    for (i = 0; i < keep_from; i++) {
        int b;

        append_text(summary, summary_cap, "[");
        append_text(summary, summary_cap,
                    conv->turns[i].role ? conv->turns[i].role : "user");
        append_text(summary, summary_cap, "] ");

        for (b = 0; b < conv->turns[i].block_count; b++) {
            const struct conv_block *blk = &conv->turns[i].blocks[b];
            char line[160];

            if (blk->type == CONV_BLOCK_TEXT) {
                int copy_len = blk->text.text_len;

                if (copy_len > 100)
                    copy_len = 100;
                memcpy(line, blk->text.text, (size_t)copy_len);
                line[copy_len] = '\0';
                append_text(summary, summary_cap, line);
            } else if (blk->type == CONV_BLOCK_TOOL_USE) {
                snprintf(line, sizeof(line), "<tool:%s>",
                         blk->tool_use.name);
                append_text(summary, summary_cap, line);
            } else if (blk->type == CONV_BLOCK_TOOL_RESULT) {
                append_text(summary, summary_cap, "<tool_result>");
            }
            append_text(summary, summary_cap, " ");
        }
        append_text(summary, summary_cap, "\n");
    }

    conv_append_system_text(conv, "Compact Summary", summary);
    return conv_truncate_oldest(conv, keep_count);
}

int conv_total_tokens(const struct conversation *conv)
{
    if (!conv)
        return 0;
    return conv->total_input_tokens + conv->total_output_tokens;
}
