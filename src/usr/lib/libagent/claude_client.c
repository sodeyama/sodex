/*
 * claude_client.c - High-level Claude API client
 *
 * Integrates HTTP client, TLS, SSE parser, and Claude adapter
 * into a single send_message call with retry/backoff.
 */

#include <agent/claude_client.h>
#include <agent/claude_adapter.h>
#include <agent/llm_provider.h>
#include <http_client.h>
#include <tls_client.h>
#include <sse_parser.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* ---- Retry with simple backoff ---- */

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
    struct sse_parser sp;
    struct sse_event ev;
    char recv_chunk[2048];
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
            claude_ret = claude_parse_sse_event(&ev, resp);
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
    struct http_header headers[8];
    struct http_request req;
    struct http_response resp;
    char recv_buf[4096];  /* For headers only; body via SSE stream */
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
        char send_buf[4096];
        int send_len;
        int total_recv = 0;
        int header_len = 0;

        /* Build HTTP request string */
        send_len = http_build_request(send_buf, sizeof(send_buf), &req);
        if (send_len < 0) {
            debug_printf("[CLAUDE] request build failed: %d\n", send_len);
            return CLAUDE_ERR_BUF_OVERFLOW;
        }

        /* TLS connect */
        debug_printf("[CLAUDE] connecting to %s:%d ...\n",
                    provider->endpoint->host, provider->endpoint->port);
        ret = tls_connect(provider->endpoint->host, provider->endpoint->port);
        if (ret != TLS_OK) {
            debug_printf("[CLAUDE] TLS connect failed: %d\n", ret);
            return CLAUDE_ERR_CONNECT;
        }

        /* Send request */
        ret = tls_send(send_buf, send_len);
        if (ret < 0) {
            debug_printf("[CLAUDE] TLS send failed: %d\n", ret);
            tls_close();
            return CLAUDE_ERR_HTTP;
        }
        debug_printf("[CLAUDE] POST %s (body=%d bytes)\n",
                    provider->endpoint->path, request_body_len);

        /* Receive headers first */
        while (total_recv < (int)sizeof(recv_buf) - 1) {
            ret = tls_recv(recv_buf + total_recv, sizeof(recv_buf) - 1 - total_recv);
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
            struct sse_parser sp;
            struct sse_event ev;
            int sse_ret, claude_ret;

            sse_parser_init(&sp);
            claude_response_init(out);

            /* Feed initial data that came with headers */
            if (extra > 0) {
                sp.consumed = 0;
                while ((sse_ret = sse_feed(&sp, recv_buf + header_len, extra, &ev)) == SSE_EVENT_DATA) {
                    claude_ret = claude_parse_sse_event(&ev, out);
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
                char chunk[2048];
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
                    claude_ret = claude_parse_sse_event(&ev, out);
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
    char request_buf[4096];
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
