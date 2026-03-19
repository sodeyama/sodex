/*
 * http_client.c - Minimal HTTP/1.1 client for freestanding environment
 *
 * Supports GET and POST with Content-Length body reception.
 * No chunked encoding, no redirects, no keep-alive.
 */

#include <http_client.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <tls_client.h>
#endif

/* ================================================================
 * Request Builder
 * ================================================================ */

int http_build_request(char *buf, int cap, const struct http_request *req)
{
    int pos = 0;
    int i;

    if (!buf || cap <= 0 || !req || !req->method || !req->host || !req->path)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Request line: METHOD path HTTP/1.1\r\n */
    pos = snprintf(buf, cap, "%s %s HTTP/1.1\r\n", req->method, req->path);
    if (pos >= cap)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Host header (mandatory) */
    pos += snprintf(buf + pos, cap - pos, "Host: %s\r\n", req->host);
    if (pos >= cap)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Custom headers */
    if (req->headers) {
        for (i = 0; req->headers[i].name != (const char *)0; i++) {
            pos += snprintf(buf + pos, cap - pos, "%s: %s\r\n",
                           req->headers[i].name, req->headers[i].value);
            if (pos >= cap)
                return HTTP_ERR_BUF_OVERFLOW;
        }
    }

    /* User-Agent (required by many CDN/WAF like CloudFront) */
    pos += snprintf(buf + pos, cap - pos, "User-Agent: sodex-curl/1.0\r\n");
    if (pos >= cap)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Accept */
    pos += snprintf(buf + pos, cap - pos, "Accept: */*\r\n");
    if (pos >= cap)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Content-Length for POST with body */
    if (req->body && req->body_len > 0) {
        pos += snprintf(buf + pos, cap - pos, "Content-Length: %d\r\n",
                       req->body_len);
        if (pos >= cap)
            return HTTP_ERR_BUF_OVERFLOW;
    }

    /* Connection: close */
    pos += snprintf(buf + pos, cap - pos, "Connection: close\r\n");
    if (pos >= cap)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Header terminator */
    pos += snprintf(buf + pos, cap - pos, "\r\n");
    if (pos >= cap)
        return HTTP_ERR_BUF_OVERFLOW;

    /* Body */
    if (req->body && req->body_len > 0) {
        if (pos + req->body_len >= cap)
            return HTTP_ERR_BUF_OVERFLOW;
        memcpy(buf + pos, req->body, req->body_len);
        pos += req->body_len;
        buf[pos] = '\0';
    }

    return pos;
}

/* ================================================================
 * Response Parser
 * ================================================================ */

/* Case-insensitive header name comparison helper */
static int header_name_eq(const char *buf, int len, const char *name)
{
    return (strncasecmp(buf, name, len) == 0 && name[len] == '\0');
}

/* Skip leading whitespace */
static const char *skip_ows(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

int http_parse_response_headers(const char *buf, int len,
                                struct http_response *resp)
{
    const char *p = buf;
    const char *end = buf + len;
    const char *line_end;
    const char *header_end;
    int i;

    if (!buf || !resp)
        return HTTP_ERR_PARSE_STATUS;

    /* Initialize response */
    resp->status_code = 0;
    resp->status_text[0] = '\0';
    resp->content_type[0] = '\0';
    resp->content_length = -1;
    resp->is_chunked = 0;
    resp->is_sse = 0;
    resp->retry_after = -1;
    resp->body = (const char *)0;
    resp->body_len = 0;

    /* Find end of headers */
    header_end = strstr(buf, "\r\n\r\n");
    if (!header_end)
        return HTTP_ERR_PARSE_HEADER;  /* Headers incomplete */

    /* Parse status line: HTTP/1.x SP status-code SP reason-phrase CRLF */
    if (len < 12)
        return HTTP_ERR_PARSE_STATUS;
    if (memcmp(p, "HTTP/1.", 7) != 0)
        return HTTP_ERR_PARSE_STATUS;
    p += 7;
    if (*p != '0' && *p != '1')
        return HTTP_ERR_PARSE_STATUS;
    p++;
    if (*p != ' ')
        return HTTP_ERR_PARSE_STATUS;
    p++;

    /* Parse 3-digit status code */
    if (p + 3 > end)
        return HTTP_ERR_PARSE_STATUS;
    resp->status_code = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
    p += 3;

    /* Skip SP and copy reason phrase */
    if (*p == ' ')
        p++;
    line_end = strstr(p, "\r\n");
    if (!line_end)
        return HTTP_ERR_PARSE_STATUS;
    {
        int rlen = line_end - p;
        if (rlen > (int)sizeof(resp->status_text) - 1)
            rlen = sizeof(resp->status_text) - 1;
        memcpy(resp->status_text, p, rlen);
        resp->status_text[rlen] = '\0';
    }

    /* Parse headers */
    p = line_end + 2;  /* Skip past first \r\n */

    while (p < header_end) {
        const char *colon;
        const char *value_start;
        int name_len, value_len;

        line_end = strstr(p, "\r\n");
        if (!line_end)
            break;

        colon = (const char *)0;
        for (i = 0; p + i < line_end; i++) {
            if (p[i] == ':') {
                colon = p + i;
                break;
            }
        }
        if (!colon) {
            p = line_end + 2;
            continue;
        }

        name_len = colon - p;
        value_start = skip_ows(colon + 1);
        value_len = line_end - value_start;

        /* Recognize important headers */
        if (header_name_eq(p, name_len, "Content-Length")) {
            resp->content_length = 0;
            {
                const char *v = value_start;
                while (v < line_end && *v >= '0' && *v <= '9') {
                    resp->content_length = resp->content_length * 10 + (*v - '0');
                    v++;
                }
            }
        } else if (header_name_eq(p, name_len, "Content-Type")) {
            int ct_len = value_len;
            if (ct_len > (int)sizeof(resp->content_type) - 1)
                ct_len = sizeof(resp->content_type) - 1;
            memcpy(resp->content_type, value_start, ct_len);
            resp->content_type[ct_len] = '\0';
            /* Check for SSE */
            if (strstr(resp->content_type, "text/event-stream"))
                resp->is_sse = 1;
        } else if (header_name_eq(p, name_len, "Transfer-Encoding")) {
            if (strncasecmp(value_start, "chunked", 7) == 0)
                resp->is_chunked = 1;
        } else if (header_name_eq(p, name_len, "Retry-After")) {
            resp->retry_after = 0;
            {
                const char *v = value_start;
                while (v < line_end && *v >= '0' && *v <= '9') {
                    resp->retry_after = resp->retry_after * 10 + (*v - '0');
                    v++;
                }
            }
        }

        p = line_end + 2;
    }

    /* Return offset to body start */
    return (header_end + 4) - buf;
}

int http_body_complete(const struct http_response *resp, int received_body_len)
{
    if (!resp)
        return 0;
    if (resp->content_length >= 0)
        return (received_body_len >= resp->content_length) ? 1 : 0;
    /* No Content-Length: can't determine completion */
    return 0;
}

/* ================================================================
 * High-level request function (not available in TEST_BUILD)
 * ================================================================ */

#ifndef TEST_BUILD

/* ---- TLS version of http_do_request ---- */
static int http_do_request_tls(const struct http_request *req,
                               char *recv_buf, int recv_cap,
                               struct http_response *resp)
{
    char send_buf[2048];
    int send_len;
    int ret;
    int total_recv = 0;
    int header_len = 0;
    int body_received = 0;

    /* Build request */
    send_len = http_build_request(send_buf, sizeof(send_buf), req);
    if (send_len < 0)
        return send_len;

    /* TLS connect (includes DNS + TCP + handshake) */
    debug_printf("[HTTPS] connecting to %s:%d ...\n", req->host, req->port);
    ret = tls_connect(req->host, req->port);
    if (ret != TLS_OK) {
        debug_printf("[HTTPS] connect failed: %d\n", ret);
        return HTTP_ERR_CONNECT;
    }

    /* Send HTTP request over TLS */
    ret = tls_send(send_buf, send_len);
    if (ret < 0) {
        debug_printf("[HTTPS] send failed: %d\n", ret);
        tls_close();
        return HTTP_ERR_SEND;
    }
    debug_printf("[HTTPS] sent %d bytes\n", ret);

    /* Receive response over TLS */
    total_recv = 0;
    while (total_recv < recv_cap - 1) {
        ret = tls_recv(recv_buf + total_recv, recv_cap - 1 - total_recv);
        if (ret <= 0)
            break;
        total_recv += ret;
        recv_buf[total_recv] = '\0';

        if (header_len == 0) {
            header_len = http_parse_response_headers(recv_buf, total_recv, resp);
            if (header_len > 0) {
                resp->body = recv_buf + header_len;
                body_received = total_recv - header_len;
                resp->body_len = body_received;
                if (http_body_complete(resp, body_received))
                    break;
            }
        } else {
            body_received = total_recv - header_len;
            resp->body_len = body_received;
            if (http_body_complete(resp, body_received))
                break;
        }
    }

    tls_close();

    if (header_len <= 0) {
        debug_printf("[HTTPS] no valid response (received %d bytes)\n", total_recv);
        return HTTP_ERR_RECV;
    }

    debug_printf("[HTTPS] %d %s, body=%d bytes\n",
                resp->status_code, resp->status_text, resp->body_len);
    return HTTP_OK;
}

/* ---- Plaintext version of http_do_request ---- */
static int http_do_request_plain(const struct http_request *req,
                                 char *recv_buf, int recv_cap,
                                 struct http_response *resp)
{
    char send_buf[2048];
    int send_len;
    int sockfd;
    struct sockaddr_in addr;
    int ret;
    int total_recv = 0;
    int header_len = 0;
    int body_received = 0;
    u_int32_t timeout;

    /* Build request */
    send_len = http_build_request(send_buf, sizeof(send_buf), req);
    if (send_len < 0)
        return send_len;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        debug_printf("[HTTP] socket() failed: %d\n", sockfd);
        return HTTP_ERR_CONNECT;
    }

    timeout = 5000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(req->port);
    inet_aton(req->host, &addr.sin_addr);

    debug_printf("[HTTP] connect %s:%d ...\n", req->host, req->port);
    ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        debug_printf("[HTTP] connect failed: %d\n", ret);
        closesocket(sockfd);
        return HTTP_ERR_CONNECT;
    }
    debug_printf("[HTTP] connected\n");

    ret = send_msg(sockfd, send_buf, send_len, 0);
    if (ret < 0) {
        debug_printf("[HTTP] send failed: %d\n", ret);
        closesocket(sockfd);
        return HTTP_ERR_SEND;
    }
    debug_printf("[HTTP] sent %d bytes\n", ret);

    total_recv = 0;
    while (total_recv < recv_cap - 1) {
        ret = recv_msg(sockfd, recv_buf + total_recv, recv_cap - 1 - total_recv, 0);
        if (ret <= 0)
            break;
        total_recv += ret;
        recv_buf[total_recv] = '\0';

        if (header_len == 0) {
            header_len = http_parse_response_headers(recv_buf, total_recv, resp);
            if (header_len > 0) {
                resp->body = recv_buf + header_len;
                body_received = total_recv - header_len;
                resp->body_len = body_received;
                if (http_body_complete(resp, body_received))
                    break;
            }
        } else {
            body_received = total_recv - header_len;
            resp->body_len = body_received;
            if (http_body_complete(resp, body_received))
                break;
        }
    }

    closesocket(sockfd);

    if (header_len <= 0) {
        debug_printf("[HTTP] no valid response (received %d bytes)\n", total_recv);
        return HTTP_ERR_RECV;
    }

    debug_printf("[HTTP] %d %s, body=%d bytes\n",
                resp->status_code, resp->status_text, resp->body_len);
    return HTTP_OK;
}

int http_do_request(const struct http_request *req,
                    char *recv_buf, int recv_cap,
                    struct http_response *resp)
{
    if (!req || !recv_buf || !resp)
        return HTTP_ERR_BUF_OVERFLOW;

    if (req->use_tls)
        return http_do_request_tls(req, recv_buf, recv_cap, resp);
    else
        return http_do_request_plain(req, recv_buf, recv_cap, resp);
}

#endif /* !TEST_BUILD */
