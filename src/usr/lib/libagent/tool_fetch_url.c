/*
 * tool_fetch_url.c - fetch_url tool 実装
 */

#include <agent/tool_handlers.h>
#include <agent/bounded_output.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <web_fetch_client.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_FETCH_URL[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"url\":{\"type\":\"string\",\"description\":\"URL to fetch and extract\"},"
    "\"method\":{\"type\":\"string\",\"description\":\"GET or HEAD\"},"
    "\"render_js\":{\"type\":\"boolean\",\"description\":\"Request JS rendering on the host gateway\"},"
    "\"max_chars\":{\"type\":\"integer\",\"description\":\"Maximum extracted characters to request\"}"
    "},"
    "\"required\":[\"url\"]}";

int tool_fetch_url(const char *input_json, int input_len,
                   char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[64];
    int token_count;
    int tok;
    static struct web_fetch_request req;
    static struct web_fetch_result result;
    static struct bounded_output bounded;
    struct json_writer jw;
    int render_js = 0;
    int max_chars = WEB_FETCH_DEFAULT_MAX_CHARS;
    int ret;

    if (!input_json || !result_buf)
        return -1;

    json_init(&jp);
    token_count = json_parse(&jp, input_json, input_len, tokens, 64);
    if (token_count < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    web_fetch_request_init(&req);

    tok = json_find_key(input_json, tokens, token_count, 0, "url");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: url\"}");
    }
    if (json_token_str(input_json, &tokens[tok], req.url, sizeof(req.url)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"url too long\"}");
    }

    tok = json_find_key(input_json, tokens, token_count, 0, "method");
    if (tok >= 0) {
        if (json_token_str(input_json, &tokens[tok],
                           req.method, sizeof(req.method)) < 0) {
            return snprintf(result_buf, result_cap,
                            "{\"error\":\"method too long\"}");
        }
    }

    tok = json_find_key(input_json, tokens, token_count, 0, "render_js");
    if (tok >= 0 && json_token_bool(input_json, &tokens[tok], &render_js) == 0)
        req.render_js = render_js;

    tok = json_find_key(input_json, tokens, token_count, 0, "max_chars");
    if (tok >= 0 && json_token_int(input_json, &tokens[tok], &max_chars) == 0) {
        if (max_chars <= 0)
            max_chars = WEB_FETCH_DEFAULT_MAX_CHARS;
        if (max_chars >= WEB_FETCH_MAX_MAIN_TEXT)
            max_chars = WEB_FETCH_MAX_MAIN_TEXT - 1;
        req.max_chars = max_chars;
    }

    debug_printf("[TOOL fetch_url] url=%s method=%s render_js=%d max_chars=%d\n",
                 req.url, req.method, req.render_js, req.max_chars);

    ret = web_fetch_execute(&req, &result);
    if (ret < 0) {
        jw_init(&jw, result_buf, result_cap);
        jw_object_start(&jw);
        jw_key(&jw, "error");
        jw_string(&jw, result.error[0] ? result.error : "fetch failed");
        jw_key(&jw, "code");
        jw_string(&jw, result.code[0] ? result.code : "transport_error");
        jw_key(&jw, "url");
        jw_string(&jw, req.url);
        jw_object_end(&jw);
        return jw_finish(&jw);
    }

    bounded_output_init(&bounded);
    bounded_output_begin_artifact(&bounded, "fetch", ".txt");
    if (result.main_text[0] != '\0')
        bounded_output_append(&bounded, result.main_text, strlen(result.main_text));
    bounded_output_finish(&bounded, bounded.total_bytes > AGENT_BOUNDED_INLINE);

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);

    jw_key(&jw, "url");
    jw_string(&jw, result.url);
    jw_key(&jw, "final_url");
    jw_string(&jw, result.final_url[0] ? result.final_url : result.url);
    jw_key(&jw, "gateway_status");
    jw_int(&jw, result.gateway_status);
    jw_key(&jw, "status");
    jw_int(&jw, result.source_status);
    jw_key(&jw, "method");
    jw_string(&jw, result.method[0] ? result.method : req.method);
    jw_key(&jw, "content_type");
    jw_string(&jw, result.content_type);
    jw_key(&jw, "title");
    jw_string(&jw, result.title);
    jw_key(&jw, "excerpt");
    jw_string(&jw, result.excerpt);
    bounded_output_write_json(&bounded, &jw,
                              "main_text",
                              "main_text_head",
                              "main_text_tail");
    jw_key(&jw, "fetched_at");
    jw_string(&jw, result.fetched_at);
    jw_key(&jw, "source_hash");
    jw_string(&jw, result.source_hash);
    jw_key(&jw, "source_truncated");
    jw_bool(&jw, result.truncated ? 1 : 0);

    if (result.error[0] != '\0') {
        jw_key(&jw, "error");
        jw_string(&jw, result.error);
    }
    if (result.code[0] != '\0') {
        jw_key(&jw, "code");
        jw_string(&jw, result.code);
    }

    jw_object_end(&jw);
    return jw_finish(&jw);
}
