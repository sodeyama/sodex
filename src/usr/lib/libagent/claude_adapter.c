/*
 * claude_adapter.c - Claude Messages API adapter
 *
 * Handles request JSON generation, SSE event parsing, and
 * response accumulation for the Claude Messages API.
 */

#include <agent/claude_adapter.h>
#include <agent/api_config.h>
#include <agent/llm_provider.h>
#include <json.h>
#include <sse_parser.h>
#include <string.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
#include <stdio.h>
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

/* ---- Config ---- */

static const struct api_endpoint claude_endpoint = {
    .host = "api.anthropic.com",
    .path = "/v1/messages",
    .port = 443,
};

static const struct api_header claude_headers[] = {
    { "content-type",      "application/json" },
    { "anthropic-version", "2023-06-01" },
    { "x-api-key",         "" },  /* Set at runtime */
    { (const char *)0,     (const char *)0 }
};

/* ---- Response init ---- */

void claude_response_init(struct claude_response *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->stop_reason = CLAUDE_STOP_NONE;
    resp->current_block_index = -1;
}

/* ---- Request builder ---- */

int claude_build_request(
    struct json_writer *jw,
    const char *model,
    const struct claude_message *msgs, int msg_count,
    const char *system_prompt,
    int max_tokens,
    int stream)
{
    int i;

    if (!jw || !model || !msgs || msg_count <= 0)
        return CLAUDE_ERR_BUF_OVERFLOW;

    jw_object_start(jw);

    jw_key(jw, "model");
    jw_string(jw, model);

    jw_key(jw, "max_tokens");
    jw_int(jw, max_tokens);

    if (system_prompt) {
        jw_key(jw, "system");
        jw_string(jw, system_prompt);
    }

    if (stream) {
        jw_key(jw, "stream");
        jw_bool(jw, 1);
    }

    jw_key(jw, "messages");
    jw_array_start(jw);
    for (i = 0; i < msg_count; i++) {
        jw_object_start(jw);
        jw_key(jw, "role");
        jw_string(jw, msgs[i].role);
        jw_key(jw, "content");
        jw_string(jw, msgs[i].content);
        jw_object_end(jw);
    }
    jw_array_end(jw);

    jw_object_end(jw);

    return (jw_finish(jw) < 0) ? CLAUDE_ERR_BUF_OVERFLOW : CLAUDE_OK;
}

/* ---- Non-streaming response parser ---- */

int claude_parse_response(
    const char *json_str, int json_len,
    struct claude_response *out)
{
    struct json_parser jp;
    struct json_token tokens[256];
    int ntokens;
    int tok;

    if (!json_str || json_len <= 0 || !out)
        return CLAUDE_ERR_JSON_PARSE;

    claude_response_init(out);

    json_init(&jp);
    ntokens = json_parse(&jp, json_str, json_len, tokens, 256);
    if (ntokens < 0)
        return CLAUDE_ERR_JSON_PARSE;

    /* id */
    tok = json_find_key(json_str, tokens, ntokens, 0, "id");
    if (tok >= 0)
        json_token_str(json_str, &tokens[tok], out->id, sizeof(out->id));

    /* model */
    tok = json_find_key(json_str, tokens, ntokens, 0, "model");
    if (tok >= 0)
        json_token_str(json_str, &tokens[tok], out->model, sizeof(out->model));

    /* stop_reason */
    tok = json_find_key(json_str, tokens, ntokens, 0, "stop_reason");
    if (tok >= 0) {
        if (json_token_eq(json_str, &tokens[tok], "end_turn"))
            out->stop_reason = CLAUDE_STOP_END_TURN;
        else if (json_token_eq(json_str, &tokens[tok], "tool_use"))
            out->stop_reason = CLAUDE_STOP_TOOL_USE;
        else if (json_token_eq(json_str, &tokens[tok], "max_tokens"))
            out->stop_reason = CLAUDE_STOP_MAX_TOKENS;
    }

    /* usage */
    tok = json_find_key(json_str, tokens, ntokens, 0, "usage");
    if (tok >= 0) {
        int ut = json_find_key(json_str, tokens, ntokens, tok, "input_tokens");
        if (ut >= 0)
            json_token_int(json_str, &tokens[ut], &out->input_tokens);
        ut = json_find_key(json_str, tokens, ntokens, tok, "output_tokens");
        if (ut >= 0)
            json_token_int(json_str, &tokens[ut], &out->output_tokens);
    }

    /* content array */
    tok = json_find_key(json_str, tokens, ntokens, 0, "content");
    if (tok >= 0 && tokens[tok].type == JSON_ARRAY) {
        int count = tokens[tok].size;
        int i;
        for (i = 0; i < count && i < CLAUDE_MAX_BLOCKS; i++) {
            int elem = json_array_get(tokens, ntokens, tok, i);
            if (elem < 0) break;

            int type_tok = json_find_key(json_str, tokens, ntokens, elem, "type");
            if (type_tok < 0) continue;

            if (json_token_eq(json_str, &tokens[type_tok], "text")) {
                out->blocks[i].type = CLAUDE_CONTENT_TEXT;
                int text_tok = json_find_key(json_str, tokens, ntokens, elem, "text");
                if (text_tok >= 0) {
                    out->blocks[i].text.text_len = json_token_str(
                        json_str, &tokens[text_tok],
                        out->blocks[i].text.text,
                        CLAUDE_MAX_TEXT);
                }
            } else if (json_token_eq(json_str, &tokens[type_tok], "tool_use")) {
                out->blocks[i].type = CLAUDE_CONTENT_TOOL_USE;
                int id_tok = json_find_key(json_str, tokens, ntokens, elem, "id");
                if (id_tok >= 0)
                    json_token_str(json_str, &tokens[id_tok],
                                   out->blocks[i].tool_use.id, CLAUDE_MAX_TOOL_ID);
                int name_tok = json_find_key(json_str, tokens, ntokens, elem, "name");
                if (name_tok >= 0)
                    json_token_str(json_str, &tokens[name_tok],
                                   out->blocks[i].tool_use.name, CLAUDE_MAX_TOOL_NAME);
                int input_tok = json_find_key(json_str, tokens, ntokens, elem, "input");
                if (input_tok >= 0) {
                    int start = tokens[input_tok].start;
                    int end = tokens[input_tok].end;
                    int slen = end - start;
                    if (slen > CLAUDE_MAX_TOOL_INPUT - 1)
                        slen = CLAUDE_MAX_TOOL_INPUT - 1;
                    memcpy(out->blocks[i].tool_use.input_json,
                           json_str + start, slen);
                    out->blocks[i].tool_use.input_json[slen] = '\0';
                    out->blocks[i].tool_use.input_json_len = slen;
                }
            }
            out->block_count = i + 1;
        }
    }

    /* Check for error response */
    tok = json_find_key(json_str, tokens, ntokens, 0, "type");
    if (tok >= 0 && json_token_eq(json_str, &tokens[tok], "error")) {
        out->stop_reason = CLAUDE_STOP_ERROR;
        return CLAUDE_ERR_API;
    }

    return CLAUDE_OK;
}

/* ---- SSE event parser (streaming) ---- */

int claude_parse_sse_event(
    const struct sse_event *event,
    struct claude_response *state)
{
    struct json_parser jp;
    struct json_token tokens[128];
    int ntokens;
    int tok;

    if (!event || !state)
        return CLAUDE_ERR_JSON_PARSE;

    /* Parse the JSON data */
    json_init(&jp);
    ntokens = json_parse(&jp, event->data, event->data_len, tokens, 128);
    if (ntokens < 0) {
        debug_printf("[CLAUDE] SSE json parse error: %d\n", ntokens);
        return CLAUDE_ERR_JSON_PARSE;
    }

    /* Determine event type from event_name */
    if (strcmp(event->event_name, "message_start") == 0) {
        /* Extract message id, model from nested "message" object */
        int msg_tok = json_find_key(event->data, tokens, ntokens, 0, "message");
        if (msg_tok >= 0) {
            tok = json_find_key(event->data, tokens, ntokens, msg_tok, "id");
            if (tok >= 0)
                json_token_str(event->data, &tokens[tok],
                              state->id, sizeof(state->id));
            tok = json_find_key(event->data, tokens, ntokens, msg_tok, "model");
            if (tok >= 0)
                json_token_str(event->data, &tokens[tok],
                              state->model, sizeof(state->model));
            /* usage from message_start */
            int usage_tok = json_find_key(event->data, tokens, ntokens, msg_tok, "usage");
            if (usage_tok >= 0) {
                tok = json_find_key(event->data, tokens, ntokens, usage_tok, "input_tokens");
                if (tok >= 0)
                    json_token_int(event->data, &tokens[tok], &state->input_tokens);
            }
        }
        debug_printf("[SSE] event: message_start\n");

    } else if (strcmp(event->event_name, "content_block_start") == 0) {
        int index = -1;
        tok = json_find_key(event->data, tokens, ntokens, 0, "index");
        if (tok >= 0)
            json_token_int(event->data, &tokens[tok], &index);

        int cb_tok = json_find_key(event->data, tokens, ntokens, 0, "content_block");
        if (cb_tok >= 0 && index >= 0 && index < CLAUDE_MAX_BLOCKS) {
            int type_tok = json_find_key(event->data, tokens, ntokens, cb_tok, "type");
            if (type_tok >= 0) {
                if (json_token_eq(event->data, &tokens[type_tok], "text")) {
                    state->blocks[index].type = CLAUDE_CONTENT_TEXT;
                    state->blocks[index].text.text[0] = '\0';
                    state->blocks[index].text.text_len = 0;
                    debug_printf("[SSE] event: content_block_start (type=text)\n");
                } else if (json_token_eq(event->data, &tokens[type_tok], "tool_use")) {
                    state->blocks[index].type = CLAUDE_CONTENT_TOOL_USE;
                    state->blocks[index].tool_use.input_json[0] = '\0';
                    state->blocks[index].tool_use.input_json_len = 0;
                    /* Extract id and name */
                    int id_tok = json_find_key(event->data, tokens, ntokens, cb_tok, "id");
                    if (id_tok >= 0)
                        json_token_str(event->data, &tokens[id_tok],
                                      state->blocks[index].tool_use.id,
                                      CLAUDE_MAX_TOOL_ID);
                    int name_tok = json_find_key(event->data, tokens, ntokens, cb_tok, "name");
                    if (name_tok >= 0)
                        json_token_str(event->data, &tokens[name_tok],
                                      state->blocks[index].tool_use.name,
                                      CLAUDE_MAX_TOOL_NAME);
                    debug_printf("[SSE] event: content_block_start (type=tool_use, name=%s)\n",
                                state->blocks[index].tool_use.name);
                }
            }
            if (index >= state->block_count)
                state->block_count = index + 1;
            state->current_block_index = index;
        }

    } else if (strcmp(event->event_name, "content_block_delta") == 0) {
        int index = -1;
        tok = json_find_key(event->data, tokens, ntokens, 0, "index");
        if (tok >= 0)
            json_token_int(event->data, &tokens[tok], &index);

        int delta_tok = json_find_key(event->data, tokens, ntokens, 0, "delta");
        if (delta_tok >= 0 && index >= 0 && index < CLAUDE_MAX_BLOCKS) {
            int type_tok = json_find_key(event->data, tokens, ntokens, delta_tok, "type");
            if (type_tok >= 0) {
                if (json_token_eq(event->data, &tokens[type_tok], "text_delta")) {
                    /* Append text */
                    int text_tok = json_find_key(event->data, tokens, ntokens, delta_tok, "text");
                    if (text_tok >= 0) {
                        char tmp[CLAUDE_MAX_TEXT];
                        int tlen = json_token_str(event->data, &tokens[text_tok],
                                                  tmp, sizeof(tmp));
                        if (tlen > 0) {
                            int cur = state->blocks[index].text.text_len;
                            int avail = CLAUDE_MAX_TEXT - cur - 1;
                            int copy = tlen < avail ? tlen : avail;
                            if (copy > 0) {
                                memcpy(state->blocks[index].text.text + cur,
                                       tmp, copy);
                                state->blocks[index].text.text_len += copy;
                                state->blocks[index].text.text[cur + copy] = '\0';
                            }
                            debug_printf("[SSE] event: content_block_delta (text=\"%s\")\n", tmp);
                        }
                    }
                } else if (json_token_eq(event->data, &tokens[type_tok], "input_json_delta")) {
                    /* Append partial JSON for tool input */
                    int pj_tok = json_find_key(event->data, tokens, ntokens, delta_tok, "partial_json");
                    if (pj_tok >= 0) {
                        char tmp[CLAUDE_MAX_TOOL_INPUT];
                        int tlen = json_token_str(event->data, &tokens[pj_tok],
                                                  tmp, sizeof(tmp));
                        if (tlen > 0) {
                            int cur = state->blocks[index].tool_use.input_json_len;
                            int avail = CLAUDE_MAX_TOOL_INPUT - cur - 1;
                            int copy = tlen < avail ? tlen : avail;
                            if (copy > 0) {
                                memcpy(state->blocks[index].tool_use.input_json + cur,
                                       tmp, copy);
                                state->blocks[index].tool_use.input_json_len += copy;
                                state->blocks[index].tool_use.input_json[cur + copy] = '\0';
                            }
                        }
                    }
                }
            }
        }

    } else if (strcmp(event->event_name, "content_block_stop") == 0) {
        int index = -1;
        tok = json_find_key(event->data, tokens, ntokens, 0, "index");
        if (tok >= 0)
            json_token_int(event->data, &tokens[tok], &index);
        debug_printf("[SSE] event: content_block_stop\n");

    } else if (strcmp(event->event_name, "message_delta") == 0) {
        int delta_tok = json_find_key(event->data, tokens, ntokens, 0, "delta");
        if (delta_tok >= 0) {
            int sr_tok = json_find_key(event->data, tokens, ntokens, delta_tok, "stop_reason");
            if (sr_tok >= 0) {
                if (json_token_eq(event->data, &tokens[sr_tok], "end_turn"))
                    state->stop_reason = CLAUDE_STOP_END_TURN;
                else if (json_token_eq(event->data, &tokens[sr_tok], "tool_use"))
                    state->stop_reason = CLAUDE_STOP_TOOL_USE;
                else if (json_token_eq(event->data, &tokens[sr_tok], "max_tokens"))
                    state->stop_reason = CLAUDE_STOP_MAX_TOKENS;
            }
        }
        /* output_tokens from usage */
        int usage_tok = json_find_key(event->data, tokens, ntokens, 0, "usage");
        if (usage_tok >= 0) {
            tok = json_find_key(event->data, tokens, ntokens, usage_tok, "output_tokens");
            if (tok >= 0)
                json_token_int(event->data, &tokens[tok], &state->output_tokens);
        }
        debug_printf("[SSE] event: message_delta (stop_reason=%d)\n", state->stop_reason);

    } else if (strcmp(event->event_name, "message_stop") == 0) {
        debug_printf("[SSE] event: message_stop\n");
        return 1;  /* Stream complete */

    } else if (strcmp(event->event_name, "ping") == 0) {
        debug_printf("[SSE] event: ping\n");
        /* Ignore */

    } else if (strcmp(event->event_name, "error") == 0) {
        state->stop_reason = CLAUDE_STOP_ERROR;
        debug_printf("[SSE] event: error\n");
        return CLAUDE_ERR_API;

    } else {
        debug_printf("[SSE] unknown event: %s\n", event->event_name);
    }

    return CLAUDE_OK;
}

/* ---- Tool call check ---- */

int claude_needs_tool_call(const struct claude_response *resp)
{
    return (resp && resp->stop_reason == CLAUDE_STOP_TOOL_USE) ? 1 : 0;
}

/* ---- Tool result builder ---- */

int claude_build_tool_result(
    struct json_writer *jw,
    const char *tool_use_id,
    const char *result_json, int result_json_len,
    int is_error)
{
    if (!jw || !tool_use_id)
        return CLAUDE_ERR_BUF_OVERFLOW;

    jw_object_start(jw);
    jw_key(jw, "type");
    jw_string(jw, "tool_result");
    jw_key(jw, "tool_use_id");
    jw_string(jw, tool_use_id);

    if (result_json && result_json_len > 0) {
        jw_key(jw, "content");
        jw_raw(jw, result_json, result_json_len);
    }

    if (is_error) {
        jw_key(jw, "is_error");
        jw_bool(jw, 1);
    }

    jw_object_end(jw);

    return (jw_finish(jw) < 0) ? CLAUDE_ERR_BUF_OVERFLOW : CLAUDE_OK;
}

/* ---- LLM Provider registration ---- */

const struct llm_provider provider_claude = {
    .name = "claude",
    .endpoint = &claude_endpoint,
    .headers = claude_headers,
    .header_count = 3,
    .build_request = claude_build_request,
    .parse_sse_event = claude_parse_sse_event,
    .parse_response = claude_parse_response,
};
