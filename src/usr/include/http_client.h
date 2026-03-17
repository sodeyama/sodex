#ifndef _HTTP_CLIENT_H
#define _HTTP_CLIENT_H

#include <sys/types.h>

/* ---- Error codes ---- */
#define HTTP_OK                    0
#define HTTP_ERR_BUF_OVERFLOW     (-1)
#define HTTP_ERR_PARSE_STATUS     (-2)
#define HTTP_ERR_PARSE_HEADER     (-3)
#define HTTP_ERR_NO_CONTENT_LENGTH (-4)
#define HTTP_ERR_BODY_TOO_LARGE   (-5)
#define HTTP_ERR_CONNECT          (-6)
#define HTTP_ERR_SEND             (-7)
#define HTTP_ERR_RECV             (-8)
#define HTTP_ERR_TIMEOUT          (-9)

/* ---- Data structures ---- */
#define HTTP_MAX_HEADERS    16
#define HTTP_MAX_HEADER_LEN 256
#define HTTP_MAX_URL_LEN    256

struct http_header {
    const char *name;
    const char *value;
};

struct http_request {
    const char *method;               /* "GET", "POST" */
    const char *host;                 /* "api.anthropic.com" */
    const char *path;                 /* "/v1/messages" */
    u_int16_t   port;                 /* 443 or 8080 */
    const struct http_header *headers;/* NULL-terminated array */
    const char *body;                 /* NULL = no body */
    int         body_len;
};

struct http_response {
    int  status_code;                 /* 200, 429, 500 etc */
    char status_text[64];             /* "OK", "Too Many Requests" */
    char content_type[128];           /* "application/json" */
    int  content_length;              /* -1 if not specified */
    int  is_chunked;                  /* Transfer-Encoding: chunked */
    int  is_sse;                      /* Content-Type: text/event-stream */
    int  retry_after;                 /* Retry-After header value, -1 if absent */
    const char *body;                 /* Pointer into recv buffer */
    int  body_len;                    /* Received body length */
};

/* ---- API ---- */

/*
 * Build HTTP/1.1 request into buf.
 * Returns bytes written (not counting NUL), or negative error.
 */
int http_build_request(char *buf, int cap, const struct http_request *req);

/*
 * Parse response status line and headers from buf.
 * Returns offset to body start (after \r\n\r\n), or negative error.
 */
int http_parse_response_headers(const char *buf, int len,
                                struct http_response *resp);

/*
 * Check if body reception is complete.
 * Returns 1 if complete, 0 if more data needed.
 */
int http_body_complete(const struct http_response *resp, int received_body_len);

/*
 * High-level: perform a full HTTP request over TCP.
 * recv_buf/recv_cap: buffer for response.
 * Returns 0 on success, negative on error.
 */
int http_do_request(const struct http_request *req,
                    char *recv_buf, int recv_cap,
                    struct http_response *resp);

#endif /* _HTTP_CLIENT_H */
