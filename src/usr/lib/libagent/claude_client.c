/*
 * claude_client.c - High-level Claude API client
 *
 * Integrates HTTP client, TLS, SSE parser, and Claude adapter
 * into a single send_message call with retry/backoff.
 */

#include <agent/claude_client.h>
#include <agent/claude_adapter.h>
#include <agent/conversation.h>
#include <agent/llm_provider.h>
#include <agent/tool_dispatch.h>
#include <http_client.h>
#include <tls_client.h>
#include <sse_parser.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* ---- Retry with simple backoff ---- */

static claude_stream_text_fn s_stream_text_callback = (claude_stream_text_fn)0;
static void *s_stream_text_userdata = (void *)0;

static int parse_chunk_size_line(const char *buf, int len, int *chunk_size)
{
    int i = 0;
    int val = 0;
    char c;

    if (!buf || len <= 0 || !chunk_size)
        return 0;

    while (i < len) {
        c = buf[i];
        if (c >= '0' && c <= '9')
            val = val * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            val = val * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            val = val * 16 + (c - 'A' + 10);
        else
            break;
        i++;
    }
    if (i == 0)
        return 0;

    while (i < len && buf[i] != '\r')
        i++;
    if (i + 1 >= len)
        return 0;
    if (buf[i] != '\r' || buf[i + 1] != '\n')
        return 0;

    *chunk_size = val;
    return i + 2;
}

static int build_error_body_preview(const char *body, int body_len,
                                    int is_chunked,
                                    char *out, int out_cap)
{
    int pos = 0;
    int out_len = 0;

    if (!out || out_cap <= 0)
        return 0;
    out[0] = '\0';
    if (!body || body_len <= 0)
        return 0;

    if (!is_chunked) {
        out_len = body_len;
        if (out_len >= out_cap)
            out_len = out_cap - 1;
        memcpy(out, body, (size_t)out_len);
        out[out_len] = '\0';
        return out_len;
    }

    while (pos < body_len && out_len < out_cap - 1) {
        int chunk_size = 0;
        int header_len = parse_chunk_size_line(body + pos,
                                               body_len - pos,
                                               &chunk_size);
        int copy_len;

        if (header_len <= 0)
            break;
        pos += header_len;
        if (chunk_size == 0)
            break;
        if (pos + chunk_size > body_len)
            chunk_size = body_len - pos;
        if (chunk_size <= 0)
            break;

        copy_len = chunk_size;
        if (copy_len > (out_cap - 1 - out_len))
            copy_len = out_cap - 1 - out_len;
        memcpy(out + out_len, body + pos, (size_t)copy_len);
        out_len += copy_len;
        pos += chunk_size;
        if (pos + 1 < body_len && body[pos] == '\r' && body[pos + 1] == '\n')
            pos += 2;
    }

    out[out_len] = '\0';
    return out_len;
}

static void emit_stream_text_delta(int prev_text_len[CLAUDE_MAX_BLOCKS],
                                   const struct claude_response *resp)
{
    int i;

    if (!resp || !s_stream_text_callback)
        return;

    for (i = 0; i < resp->block_count && i < CLAUDE_MAX_BLOCKS; i++) {
        if (resp->blocks[i].type != CLAUDE_CONTENT_TEXT)
            continue;
        if (resp->blocks[i].text.text_len > prev_text_len[i]) {
            int delta_len = resp->blocks[i].text.text_len - prev_text_len[i];

            s_stream_text_callback(resp->blocks[i].text.text + prev_text_len[i],
                                   delta_len,
                                   s_stream_text_userdata);
        }
    }
}

static void wait_ms(int ms)
{
    /* Use a busy loop based on an approximate iteration count.
     * On i486 at ~100MHz, ~100k iterations ≈ 1ms. Rough approximation. */
    volatile int i;
    int iters = ms * 100;
    for (i = 0; i < iters; i++)
        ;
}

/* ---- SSE streaming receive loop ---- */

static int recv_sse_stream(
    int use_tls,
    int sockfd,
    struct claude_response *resp)
{
    static struct sse_parser sp;
    static struct sse_event ev;
    static char recv_chunk[2048];
    int ret, sse_ret, claude_ret;

    sse_parser_init(&sp);
    claude_response_init(resp);

    /* Read TLS/TCP data in chunks and feed to SSE parser */
    for (;;) {
        if (use_tls)
            ret = tls_recv(recv_chunk, sizeof(recv_chunk) - 1);
        else {
            /* For plain TCP SSE, we'd need the socket fd.
             * This path is for future non-TLS SSE support. */
            ret = 0;
        }

        if (ret <= 0) {
            /* Connection closed or error */
            if (resp->stop_reason != CLAUDE_STOP_NONE)
                return CLAUDE_OK;  /* Already got message_stop */
            debug_printf("[CLAUDE] recv returned %d\n", ret);
            return CLAUDE_ERR_TIMEOUT;
        }

        recv_chunk[ret] = '\0';

        /* Feed chunk to SSE parser, process all complete events */
        sp.consumed = 0;
        while ((sse_ret = sse_feed(&sp, recv_chunk, ret, &ev)) == SSE_EVENT_DATA) {
            int prev_text_len[CLAUDE_MAX_BLOCKS] = {0};
            int i;

            for (i = 0; i < CLAUDE_MAX_BLOCKS; i++) {
                if (resp->blocks[i].type == CLAUDE_CONTENT_TEXT)
                    prev_text_len[i] = resp->blocks[i].text.text_len;
            }
            claude_ret = claude_parse_sse_event(&ev, resp);
            emit_stream_text_delta(prev_text_len, resp);
            if (claude_ret == 1) {
                /* message_stop received */
                debug_printf("[CLAUDE] response complete: %d block(s), stop=%d, tokens=%d/%d\n",
                            resp->block_count, resp->stop_reason,
                            resp->input_tokens, resp->output_tokens);
                return CLAUDE_OK;
            }
            if (claude_ret < 0) {
                debug_printf("[CLAUDE] SSE parse error: %d\n", claude_ret);
                return claude_ret;
            }
        }

        if (sse_ret == SSE_EVENT_ERROR) {
            debug_printf("[CLAUDE] SSE parser error\n");
            return CLAUDE_ERR_API;
        }
    }
}

/* ---- Core send + receive ---- */

static int claude_do_request(
    const struct llm_provider *provider,
    const char *request_body, int request_body_len,
    const char *api_key,
    struct claude_response *out)
{
    static struct http_header headers[8];
    static struct http_request req;
    static struct http_response resp;
    static char recv_buf[4096];  /* For headers only; body via SSE stream */
    int header_count = 0;
    int ret;
    int i;

    /* Build headers */
    for (i = 0; provider->headers[i].name != (const char *)0; i++) {
        headers[header_count].name = provider->headers[i].name;
        if (strcmp(provider->headers[i].name, "x-api-key") == 0 && api_key) {
            headers[header_count].value = api_key;
        } else {
            headers[header_count].value = provider->headers[i].value;
        }
        header_count++;
    }
    headers[header_count].name = (const char *)0;
    headers[header_count].value = (const char *)0;

    /* Build HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.host = provider->endpoint->host;
    req.path = provider->endpoint->path;
    req.port = provider->endpoint->port;
    req.headers = headers;
    req.body = request_body;
    req.body_len = request_body_len;
    /* Use TLS for port 443 (production) and 4443 (test mock) */
    req.use_tls = (provider->endpoint->port == 443 ||
                   provider->endpoint->port == 4443) ? 1 : 0;

    /* For SSE, we need to manage the connection manually since
     * http_do_request waits for complete body.
     * Use TLS directly for SSE streaming. */

    if (req.use_tls) {
        static char send_buf[4096];
        int send_len;
        int total_recv = 0;
        int header_len = 0;

        /* Build HTTP headers */
        {
            int pos = 0;
            int hdr_i;

            pos = snprintf(send_buf, sizeof(send_buf),
                          "%s %s HTTP/1.1\r\n", req.method, req.path);
            pos += snprintf(send_buf + pos, sizeof(send_buf) - pos,
                           "Host: %s\r\n", req.host);
            for (hdr_i = 0; headers[hdr_i].name != (const char *)0; hdr_i++) {
                pos += snprintf(send_buf + pos, sizeof(send_buf) - pos,
                               "%s: %s\r\n",
                               headers[hdr_i].name, headers[hdr_i].value);
            }
            if (request_body_len > 0) {
                pos += snprintf(send_buf + pos, sizeof(send_buf) - pos,
                               "Content-Length: %d\r\n", request_body_len);
            }
            pos += snprintf(send_buf + pos, sizeof(send_buf) - pos,
                           "Connection: close\r\n");
            pos += snprintf(send_buf + pos, sizeof(send_buf) - pos, "\r\n");

            if (pos >= (int)sizeof(send_buf)) {
                debug_printf("[CLAUDE] HTTP header too large\n");
                return CLAUDE_ERR_BUF_OVERFLOW;
            }
            send_len = pos;
        }

        /* TLS connect */
        debug_printf("[CLAUDE] connecting to %s:%d ...\n",
                    provider->endpoint->host, provider->endpoint->port);
        ret = tls_connect(provider->endpoint->host, provider->endpoint->port);
        if (ret != TLS_OK) {
            debug_printf("[CLAUDE] TLS connect failed: %d\n", ret);
            return CLAUDE_ERR_CONNECT;
        }

        /* Send headers first, then body */
        ret = tls_send(send_buf, send_len);
        debug_printf("[CLAUDE] tls_send headers: ret=%d (len=%d)\n", ret, send_len);
        if (ret < 0) {
            debug_printf("[CLAUDE] TLS send headers failed: %d\n", ret);
            tls_close();
            return CLAUDE_ERR_HTTP;
        }
        if (request_body_len > 0) {
            ret = tls_send(request_body, request_body_len);
            debug_printf("[CLAUDE] tls_send body: ret=%d (len=%d)\n", ret, request_body_len);
            if (ret < 0) {
                debug_printf("[CLAUDE] TLS send body failed: %d\n", ret);
                tls_close();
                return CLAUDE_ERR_HTTP;
            }
        }
        debug_printf("[CLAUDE] POST %s (body=%d bytes)\n",
                    provider->endpoint->path, request_body_len);

        /* Receive headers first */
        while (total_recv < (int)sizeof(recv_buf) - 1) {
            ret = tls_recv(recv_buf + total_recv, sizeof(recv_buf) - 1 - total_recv);
            debug_printf("[CLAUDE] tls_recv returned %d (total=%d)\n", ret, total_recv + (ret > 0 ? ret : 0));
            if (ret <= 0) break;
            total_recv += ret;
            recv_buf[total_recv] = '\0';

            header_len = http_parse_response_headers(recv_buf, total_recv, &resp);
            if (header_len > 0)
                break;
        }

        if (header_len <= 0) {
            debug_printf("[CLAUDE] no valid response headers\n");
            tls_close();
            return CLAUDE_ERR_HTTP;
        }

        debug_printf("[CLAUDE] response: %d %s, Content-Type: %s\n",
                    resp.status_code, resp.status_text, resp.content_type);

        /* Check HTTP status */
        if (resp.status_code != 200) {
            int body_start = header_len;
            int body_received = total_recv - header_len;
            static char error_preview[601];

            while (total_recv < (int)sizeof(recv_buf) - 1) {
                if (resp.content_length >= 0 &&
                    body_received >= resp.content_length) {
                    break;
                }
                ret = tls_recv(recv_buf + total_recv,
                               sizeof(recv_buf) - 1 - total_recv);
                if (ret <= 0)
                    break;
                total_recv += ret;
                body_received = total_recv - header_len;
            }
            recv_buf[total_recv] = '\0';
            if (build_error_body_preview(recv_buf + body_start,
                                         body_received,
                                         resp.is_chunked,
                                         error_preview,
                                         sizeof(error_preview)) > 0) {
                debug_printf("[CLAUDE] error body: %.600s\n",
                             error_preview);
            }
            tls_close();
            if (resp.status_code == 429)
                return CLAUDE_ERR_TIMEOUT;  /* Signal retry */
            return CLAUDE_ERR_API;
        }

        /* Check if SSE */
        if (!resp.is_sse) {
            /* Non-streaming response - read rest of body */
            int body_start = header_len;
            int body_received = total_recv - header_len;

            /* Read remaining body if needed */
            while (resp.content_length >= 0 && body_received < resp.content_length) {
                int remain = sizeof(recv_buf) - 1 - total_recv;
                if (remain <= 0) break;
                ret = tls_recv(recv_buf + total_recv, remain);
                if (ret <= 0) break;
                total_recv += ret;
                body_received = total_recv - header_len;
            }
            recv_buf[total_recv] = '\0';

            tls_close();
            return provider->parse_response(recv_buf + body_start,
                                            body_received, out);
        }

        /* SSE streaming: any remaining data after headers is the first SSE chunk.
         * Push it back into the SSE parser, then continue receiving. */
        {
            int extra = total_recv - header_len;
            static struct sse_parser sp;
            static struct sse_event ev;
            int sse_ret, claude_ret;

            sse_parser_init(&sp);
            claude_response_init(out);

            /* Feed initial data that came with headers */
            if (extra > 0) {
                sp.consumed = 0;
                while ((sse_ret = sse_feed(&sp, recv_buf + header_len, extra, &ev)) == SSE_EVENT_DATA) {
                    int prev_text_len[CLAUDE_MAX_BLOCKS] = {0};
                    int i;

                    for (i = 0; i < CLAUDE_MAX_BLOCKS; i++) {
                        if (out->blocks[i].type == CLAUDE_CONTENT_TEXT)
                            prev_text_len[i] = out->blocks[i].text.text_len;
                    }
                    claude_ret = claude_parse_sse_event(&ev, out);
                    emit_stream_text_delta(prev_text_len, out);
                    if (claude_ret == 1) {
                        tls_close();
                        debug_printf("[CLAUDE] response complete: %d block(s), stop=%d\n",
                                    out->block_count, out->stop_reason);
                        return CLAUDE_OK;
                    }
                    if (claude_ret < 0) {
                        tls_close();
                        return claude_ret;
                    }
                }
            }

            /* Continue receiving SSE chunks */
            for (;;) {
                static char chunk[2048];
                ret = tls_recv(chunk, sizeof(chunk) - 1);
                if (ret <= 0) {
                    if (out->stop_reason != CLAUDE_STOP_NONE) {
                        tls_close();
                        return CLAUDE_OK;
                    }
                    debug_printf("[CLAUDE] recv ended: %d\n", ret);
                    tls_close();
                    return CLAUDE_ERR_TIMEOUT;
                }
                chunk[ret] = '\0';

                sp.consumed = 0;
                while ((sse_ret = sse_feed(&sp, chunk, ret, &ev)) == SSE_EVENT_DATA) {
                    int prev_text_len[CLAUDE_MAX_BLOCKS] = {0};
                    int i;

                    for (i = 0; i < CLAUDE_MAX_BLOCKS; i++) {
                        if (out->blocks[i].type == CLAUDE_CONTENT_TEXT)
                            prev_text_len[i] = out->blocks[i].text.text_len;
                    }
                    claude_ret = claude_parse_sse_event(&ev, out);
                    emit_stream_text_delta(prev_text_len, out);
                    if (claude_ret == 1) {
                        tls_close();
                        debug_printf("[CLAUDE] response complete: %d block(s), stop=%d, tokens=%d/%d\n",
                                    out->block_count, out->stop_reason,
                                    out->input_tokens, out->output_tokens);
                        return CLAUDE_OK;
                    }
                    if (claude_ret < 0) {
                        tls_close();
                        return claude_ret;
                    }
                }

                if (sse_ret == SSE_EVENT_ERROR) {
                    debug_printf("[CLAUDE] SSE parser error\n");
                    tls_close();
                    return CLAUDE_ERR_API;
                }
            }
        }
    }

    /* Non-TLS path (for mock server testing on port 8080/4443) */
    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);
    if (ret != HTTP_OK)
        return CLAUDE_ERR_HTTP;

    if (resp.status_code != 200) {
        if (resp.status_code == 429)
            return CLAUDE_ERR_TIMEOUT;
        return CLAUDE_ERR_API;
    }

    return provider->parse_response(resp.body, resp.body_len, out);
}

/* ---- Public API ---- */

int claude_send_message_with_key(
    const struct llm_provider *provider,
    const char *user_message,
    const char *api_key,
    struct claude_response *out)
{
    static char request_buf[4096];
    struct json_writer jw;
    struct claude_message msgs[1];
    int ret;
    int attempt;
    int wait_ms_val = CLAUDE_INITIAL_WAIT_MS;

    if (!provider || !user_message || !out)
        return CLAUDE_ERR_BUF_OVERFLOW;

    /* Build request JSON */
    msgs[0].role = "user";
    msgs[0].content = user_message;

    jw_init(&jw, request_buf, sizeof(request_buf));
    ret = provider->build_request(&jw, "claude-sonnet-4-20250514",
                                  msgs, 1, (const char *)0, 1024, 1);
    if (ret != CLAUDE_OK) {
        debug_printf("[CLAUDE] request build failed: %d\n", ret);
        return ret;
    }

    /* Retry loop */
    for (attempt = 0; attempt <= CLAUDE_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            debug_printf("[CLAUDE] retry %d/%d (wait %dms)\n",
                        attempt, CLAUDE_MAX_RETRIES, wait_ms_val);
            wait_ms(wait_ms_val);
            wait_ms_val *= 2;
            if (wait_ms_val > CLAUDE_MAX_WAIT_MS)
                wait_ms_val = CLAUDE_MAX_WAIT_MS;
        }

        ret = claude_do_request(provider, request_buf, strlen(request_buf),
                               api_key, out);
        if (ret == CLAUDE_OK)
            return CLAUDE_OK;

        /* Only retry on timeout/429 */
        if (ret != CLAUDE_ERR_TIMEOUT)
            return ret;
    }

    debug_printf("[CLAUDE] all retries exhausted\n");
    return ret;
}

int claude_send_message(
    const struct llm_provider *provider,
    const char *user_message,
    struct claude_response *out)
{
    return claude_send_message_with_key(provider, user_message,
                                        (const char *)0, out);
}

/* ---- Multi-turn conversation API ---- */

int claude_send_conversation_with_key(
    const struct llm_provider *provider,
    const struct conversation *conv,
    int tools_enabled,
    const char *api_key,
    struct claude_response *out)
{
    static char request_buf[16384];  /* static: too large for stack */
    struct json_writer jw;
    int ret;
    int attempt;
    int wait_ms_val = CLAUDE_INITIAL_WAIT_MS;

    if (!provider || !conv || !out)
        return CLAUDE_ERR_BUF_OVERFLOW;

    /* Build request JSON */
    jw_init(&jw, request_buf, sizeof(request_buf));
    jw_object_start(&jw);

    jw_key(&jw, "model");
    jw_string(&jw, "claude-sonnet-4-20250514");

    jw_key(&jw, "max_tokens");
    jw_int(&jw, 4096);

    if (conv->system_prompt_len > 0) {
        jw_key(&jw, "system");
        jw_string_n(&jw, conv->system_prompt, conv->system_prompt_len);
    }

    jw_key(&jw, "stream");
    jw_bool(&jw, 1);

    if (tools_enabled) {
        jw_key(&jw, "tools");
        tool_build_definitions(&jw);
    }

    jw_key(&jw, "messages");
    ret = conv_build_messages_json(conv, &jw);
    if (ret < 0) {
        debug_printf("[CLAUDE] conv_build_messages_json failed: %d\n", ret);
        return CLAUDE_ERR_BUF_OVERFLOW;
    }

    jw_object_end(&jw);
    ret = jw_finish(&jw);
    if (ret < 0) {
        debug_printf("[CLAUDE] JSON writer overflow (need > %d bytes)\n",
                    (int)sizeof(request_buf));
        return CLAUDE_ERR_BUF_OVERFLOW;
    }

    debug_printf("[CLAUDE] conversation request: %d turns, %d bytes JSON\n",
                conv->turn_count, ret);

    /* Retry loop */
    for (attempt = 0; attempt <= CLAUDE_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            debug_printf("[CLAUDE] retry %d/%d (wait %dms)\n",
                        attempt, CLAUDE_MAX_RETRIES, wait_ms_val);
            wait_ms(wait_ms_val);
            wait_ms_val *= 2;
            if (wait_ms_val > CLAUDE_MAX_WAIT_MS)
                wait_ms_val = CLAUDE_MAX_WAIT_MS;
        }

        ret = claude_do_request(provider, request_buf, strlen(request_buf),
                               api_key, out);
        if (ret == CLAUDE_OK)
            return CLAUDE_OK;

        /* Only retry on timeout/429 */
        if (ret != CLAUDE_ERR_TIMEOUT)
            return ret;
    }

    debug_printf("[CLAUDE] all retries exhausted (conversation)\n");
    return ret;
}

int claude_send_conversation(
    const struct llm_provider *provider,
    const struct conversation *conv,
    int tools_enabled,
    struct claude_response *out)
{
    return claude_send_conversation_with_key(provider, conv, tools_enabled,
                                             (const char *)0, out);
}

/* ---- Raw request API (for agent loop) ---- */

int claude_send_raw_request(
    const struct llm_provider *provider,
    const char *request_json, int request_json_len,
    const char *api_key,
    struct claude_response *out)
{
    int ret;
    int attempt;
    int wait_ms_val = CLAUDE_INITIAL_WAIT_MS;

    if (!provider || !request_json || !out)
        return CLAUDE_ERR_BUF_OVERFLOW;

    /* Retry loop */
    for (attempt = 0; attempt <= CLAUDE_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            debug_printf("[CLAUDE] retry %d/%d (wait %dms)\n",
                        attempt, CLAUDE_MAX_RETRIES, wait_ms_val);
            wait_ms(wait_ms_val);
            wait_ms_val *= 2;
            if (wait_ms_val > CLAUDE_MAX_WAIT_MS)
                wait_ms_val = CLAUDE_MAX_WAIT_MS;
        }

        ret = claude_do_request(provider, request_json, request_json_len,
                               api_key, out);
        if (ret == CLAUDE_OK)
            return CLAUDE_OK;

        if (ret != CLAUDE_ERR_TIMEOUT)
            return ret;
    }

    debug_printf("[CLAUDE] all retries exhausted (raw request)\n");
    return ret;
}

void claude_client_set_text_stream_callback(claude_stream_text_fn callback,
                                            void *userdata)
{
    s_stream_text_callback = callback;
    s_stream_text_userdata = userdata;
}
