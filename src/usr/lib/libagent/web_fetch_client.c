/*
 * web_fetch_client.c - host 側 Web fetch gateway 用 client
 */

#include <web_fetch_client.h>
#include <http_client.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

#define WEB_FETCH_JSON_TOKENS  512
#define WEB_FETCH_BODY_BUF    1024

static int copy_json_string_field_from(const char *js,
                                       const struct json_token *tokens, int token_count,
                                       int obj_token,
                                       const char *key,
                                       char *dst, int dst_cap);

static int copy_json_string_field(const char *js,
                                  const struct json_token *tokens, int token_count,
                                  const char *key,
                                  char *dst, int dst_cap)
{
    return copy_json_string_field_from(js, tokens, token_count, 0,
                                       key, dst, dst_cap);
}

static int copy_json_string_field_from(const char *js,
                                       const struct json_token *tokens, int token_count,
                                       int obj_token,
                                       const char *key,
                                       char *dst, int dst_cap)
{
    int tok;

    if (!dst || dst_cap <= 0)
        return -1;

    dst[0] = '\0';
    tok = json_find_key(js, tokens, token_count, obj_token, key);
    if (tok < 0)
        return 0;
    if (json_token_str(js, &tokens[tok], dst, dst_cap) < 0)
        return -1;
    return 0;
}

static int copy_json_int_field(const char *js,
                               const struct json_token *tokens, int token_count,
                               const char *key,
                               int *value)
{
    int tok;

    tok = json_find_key(js, tokens, token_count, 0, key);
    if (tok < 0)
        return 0;
    return json_token_int(js, &tokens[tok], value);
}

static int copy_json_bool_field(const char *js,
                                const struct json_token *tokens, int token_count,
                                const char *key,
                                int *value)
{
    int tok;

    tok = json_find_key(js, tokens, token_count, 0, key);
    if (tok < 0)
        return 0;
    return json_token_bool(js, &tokens[tok], value);
}

static int build_request_body(const struct web_fetch_request *req,
                              char *body, int body_cap)
{
    struct json_writer jw;

    if (!req || !body || body_cap <= 0)
        return -1;

    jw_init(&jw, body, body_cap);
    jw_object_start(&jw);

    jw_key(&jw, "url");
    jw_string(&jw, req->url);

    jw_key(&jw, "method");
    jw_string(&jw, req->method);

    jw_key(&jw, "render_js");
    jw_bool(&jw, req->render_js ? 1 : 0);

    jw_key(&jw, "max_chars");
    jw_int(&jw, req->max_chars);

    jw_key(&jw, "max_bytes");
    jw_int(&jw, req->max_bytes);

    jw_object_end(&jw);
    return jw_finish(&jw);
}

static void normalize_source_error(struct web_fetch_result *result)
{
    if (!result)
        return;

    if (result->source_status >= 400 && result->error[0] == '\0') {
        snprintf(result->error, sizeof(result->error),
                 "source returned HTTP %d", result->source_status);
        strncpy(result->code, "source_http_error", sizeof(result->code) - 1);
        result->code[sizeof(result->code) - 1] = '\0';
    }
}

static int parse_fetch_result(const char *js, int js_len,
                              int gateway_status,
                              struct web_fetch_result *result)
{
    struct json_parser jp;
    struct json_token tokens[WEB_FETCH_JSON_TOKENS];
    int token_count;
    int i;
    int links_tok;

    if (!js || !result)
        return -1;

    memset(result, 0, sizeof(*result));
    result->gateway_status = gateway_status;

    json_init(&jp);
    token_count = json_parse(&jp, js, js_len, tokens, WEB_FETCH_JSON_TOKENS);
    if (token_count < 0)
        return -1;

    copy_json_string_field(js, tokens, token_count, "url",
                           result->url, sizeof(result->url));
    copy_json_string_field(js, tokens, token_count, "final_url",
                           result->final_url, sizeof(result->final_url));
    copy_json_string_field(js, tokens, token_count, "method",
                           result->method, sizeof(result->method));
    copy_json_string_field(js, tokens, token_count, "content_type",
                           result->content_type, sizeof(result->content_type));
    copy_json_string_field(js, tokens, token_count, "title",
                           result->title, sizeof(result->title));
    copy_json_string_field(js, tokens, token_count, "excerpt",
                           result->excerpt, sizeof(result->excerpt));
    copy_json_string_field(js, tokens, token_count, "main_text",
                           result->main_text, sizeof(result->main_text));
    copy_json_string_field(js, tokens, token_count, "fetched_at",
                           result->fetched_at, sizeof(result->fetched_at));
    copy_json_string_field(js, tokens, token_count, "source_hash",
                           result->source_hash, sizeof(result->source_hash));
    copy_json_string_field(js, tokens, token_count, "error",
                           result->error, sizeof(result->error));
    copy_json_string_field(js, tokens, token_count, "code",
                           result->code, sizeof(result->code));
    copy_json_int_field(js, tokens, token_count, "status", &result->source_status);
    copy_json_bool_field(js, tokens, token_count, "truncated", &result->truncated);
    copy_json_bool_field(js, tokens, token_count, "render_js", &result->render_js);

    links_tok = json_find_key(js, tokens, token_count, 0, "links");
    if (links_tok >= 0 && tokens[links_tok].type == JSON_ARRAY) {
        for (i = 0; i < tokens[links_tok].size && i < WEB_FETCH_MAX_LINKS; i++) {
            int item_tok = json_array_get(tokens, token_count, links_tok, i);

            if (item_tok < 0 || tokens[item_tok].type != JSON_OBJECT)
                continue;
            copy_json_string_field_from(js, tokens, token_count, item_tok, "href",
                                        result->links[result->link_count].href,
                                        sizeof(result->links[result->link_count].href));
            copy_json_string_field_from(js, tokens, token_count, item_tok, "text",
                                        result->links[result->link_count].text,
                                        sizeof(result->links[result->link_count].text));
            if (result->links[result->link_count].href[0] != '\0')
                result->link_count++;
        }
    }

    normalize_source_error(result);
    return 0;
}

void web_fetch_request_init(struct web_fetch_request *req)
{
    if (!req)
        return;

    memset(req, 0, sizeof(*req));
    strncpy(req->host, WEB_FETCH_DEFAULT_HOST, sizeof(req->host) - 1);
    strncpy(req->path, WEB_FETCH_DEFAULT_PATH, sizeof(req->path) - 1);
    strncpy(req->method, WEB_FETCH_DEFAULT_METHOD, sizeof(req->method) - 1);
    req->port = WEB_FETCH_DEFAULT_PORT;
    req->max_chars = WEB_FETCH_DEFAULT_MAX_CHARS;
    req->max_bytes = WEB_FETCH_DEFAULT_MAX_BYTES;
}

int web_fetch_parse_host_port(const char *text,
                              char *host, int host_cap,
                              int *port_out)
{
    const char *colon;
    int host_len;
    int port = 0;

    if (!text || !host || host_cap <= 0 || !port_out)
        return -1;

    colon = strchr(text, ':');
    if (!colon)
        return -1;

    host_len = (int)(colon - text);
    if (host_len <= 0 || host_len >= host_cap)
        return -1;

    memcpy(host, text, (size_t)host_len);
    host[host_len] = '\0';

    port = atoi(colon + 1);
    if (port <= 0)
        return -1;

    *port_out = port;
    return 0;
}

int web_fetch_execute(const struct web_fetch_request *req,
                      struct web_fetch_result *result)
{
    static char recv_buf[WEB_FETCH_RECV_BUF_SIZE];
    char body[WEB_FETCH_BODY_BUF];
    struct http_header headers[3];
    struct http_request http_req;
    struct http_response http_resp;
    int body_len;
    int ret;

    if (!req || !result)
        return -1;

    memset(result, 0, sizeof(*result));

    body_len = build_request_body(req, body, sizeof(body));
    if (body_len < 0) {
        snprintf(result->error, sizeof(result->error),
                 "request body too large");
        strncpy(result->code, "request_too_large", sizeof(result->code) - 1);
        return -1;
    }

    headers[0].name = "Content-Type";
    headers[0].value = "application/json";
    headers[1].name = "Accept";
    headers[1].value = "application/json";
    headers[2].name = (const char *)0;
    headers[2].value = (const char *)0;

    memset(&http_req, 0, sizeof(http_req));
    memset(&http_resp, 0, sizeof(http_resp));

    http_req.method = "POST";
    http_req.host = req->host;
    http_req.path = req->path;
    http_req.port = req->port;
    http_req.headers = headers;
    http_req.body = body;
    http_req.body_len = body_len;
    http_req.use_tls = 0;

    ret = http_do_request(&http_req, recv_buf, sizeof(recv_buf), &http_resp);
    if (ret < 0) {
        snprintf(result->error, sizeof(result->error),
                 "webfetch request failed (%d)", ret);
        strncpy(result->code, "transport_error", sizeof(result->code) - 1);
        return -1;
    }

    debug_printf("[WEBFETCH] gateway_http=%d body=%d source_url=%s\n",
                 http_resp.status_code, http_resp.body_len, req->url);

    if (!http_resp.body || http_resp.body_len <= 0) {
        result->gateway_status = http_resp.status_code;
        snprintf(result->error, sizeof(result->error),
                 "empty gateway response");
        strncpy(result->code, "empty_response", sizeof(result->code) - 1);
        return -1;
    }

    if (parse_fetch_result(http_resp.body, http_resp.body_len,
                           http_resp.status_code, result) < 0) {
        result->gateway_status = http_resp.status_code;
        snprintf(result->error, sizeof(result->error),
                 "invalid gateway JSON");
        strncpy(result->code, "invalid_gateway_json", sizeof(result->code) - 1);
        return -1;
    }

    if (result->url[0] == '\0') {
        strncpy(result->url, req->url, sizeof(result->url) - 1);
        result->url[sizeof(result->url) - 1] = '\0';
    }

    debug_printf("[WEBFETCH] result gateway=%d status=%d truncated=%d code=%s title=%s url=%s\n",
                 result->gateway_status,
                 result->source_status,
                 result->truncated,
                 result->code[0] ? result->code : "-",
                 result->title[0] ? result->title : "-",
                 result->url);
    return 0;
}
