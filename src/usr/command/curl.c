/*
 * curl.c - Simple HTTP/HTTPS client command
 *
 * Usage: curl [options] <url>
 *   -X <method>       HTTP method (GET, POST, PUT, DELETE) [default: GET]
 *   -d <data>         Request body (implies POST if -X not set)
 *   -H <header>       Add header (format: "Name: Value"), repeatable
 *   -v                Verbose: show response status and headers
 *   -o <file>         Write response body to file instead of stdout
 *
 * Examples:
 *   curl http://10.0.2.2:8080/healthz
 *   curl https://api.anthropic.com/v1/messages
 *   curl -X POST -H "Content-Type: application/json" -d '{"key":"val"}' http://10.0.2.2:8080/echo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <http_client.h>
#include <dns.h>
#include <entropy.h>
#include <tls_client.h>

#define MAX_HEADERS     8
#define RECV_BUF_SIZE   8192

/* ---- Chunked transfer encoding decoder ---- */
struct chunked_state {
    int in_chunk;       /* 1 = reading chunk data, 0 = reading chunk size */
    int chunk_remain;   /* bytes remaining in current chunk */
    int done;           /* 1 = received final 0-length chunk */
};

/*
 * Parse a hex chunk-size from buf. Returns the number of bytes consumed
 * (including trailing CRLF), or 0 if the line is incomplete.
 * Sets *chunk_size to the parsed value.
 */
PRIVATE int parse_chunk_size(const char *buf, int len, int *chunk_size)
{
    int i = 0;
    int val = 0;
    char c;

    /* Parse hex digits */
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
        return 0;   /* no hex digits */

    /* Skip optional chunk-extension (anything until CRLF) */
    while (i < len && buf[i] != '\r')
        i++;

    /* Need CRLF */
    if (i + 1 >= len)
        return 0;   /* incomplete line */
    if (buf[i] == '\r' && buf[i + 1] == '\n') {
        *chunk_size = val;
        return i + 2;
    }
    return 0;
}

/*
 * Feed raw body data through the chunked decoder.
 * Writes decoded output to out_fd (or stdout if out_fd < 0).
 * Returns bytes of decoded body written, or -1 on error.
 */
PRIVATE int chunked_decode(struct chunked_state *st, const char *buf, int len,
                           int out_fd)
{
    int pos = 0;
    int written = 0;

    while (pos < len && !st->done) {
        if (st->in_chunk) {
            /* Consume chunk data */
            int avail = len - pos;
            int take = (avail < st->chunk_remain) ? avail : st->chunk_remain;
            if (take > 0) {
                if (out_fd >= 0)
                    write(out_fd, buf + pos, take);
                else
                    write(1, buf + pos, take);
                written += take;
                pos += take;
                st->chunk_remain -= take;
            }
            if (st->chunk_remain == 0) {
                /* Expect trailing CRLF after chunk data */
                if (pos + 1 < len && buf[pos] == '\r' && buf[pos + 1] == '\n')
                    pos += 2;
                else if (pos < len && buf[pos] == '\r')
                    pos += 1;  /* partial CRLF, next recv will have \n */
                st->in_chunk = 0;
            }
        } else {
            /* Parse chunk size line */
            int chunk_size = 0;
            int consumed = parse_chunk_size(buf + pos, len - pos, &chunk_size);
            if (consumed == 0)
                break;  /* incomplete line, need more data */
            pos += consumed;
            if (chunk_size == 0) {
                st->done = 1;
                break;
            }
            st->chunk_remain = chunk_size;
            st->in_chunk = 1;
        }
    }
    return written;
}

/* ---- URL parser ---- */
struct parsed_url {
    char host[128];
    char path[256];
    u_int16_t port;
    int  use_tls;
};

/*
 * Parse URL into components.
 * Supports: http://host[:port][/path] and https://host[:port][/path]
 * Returns 0 on success, -1 on error.
 */
PRIVATE int parse_url(const char *url, struct parsed_url *out)
{
    const char *p;
    const char *host_start;
    const char *host_end;
    const char *port_start;
    int host_len;

    memset(out, 0, sizeof(*out));

    /* Determine scheme */
    if (strncmp(url, "https://", 8) == 0) {
        out->use_tls = 1;
        out->port = 443;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        out->use_tls = 0;
        out->port = 80;
        p = url + 7;
    } else {
        /* No scheme: assume https */
        out->use_tls = 1;
        out->port = 443;
        p = url;
    }

    host_start = p;

    /* Find end of host: '/', ':', or end of string */
    host_end = p;
    while (*host_end && *host_end != '/' && *host_end != ':')
        host_end++;

    host_len = host_end - host_start;
    if (host_len <= 0 || host_len >= (int)sizeof(out->host))
        return -1;

    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    /* Parse optional port */
    if (*host_end == ':') {
        port_start = host_end + 1;
        out->port = 0;
        while (*port_start >= '0' && *port_start <= '9') {
            out->port = out->port * 10 + (*port_start - '0');
            port_start++;
        }
        host_end = port_start;
    }

    /* Parse path (default to "/") */
    if (*host_end == '/') {
        strncpy(out->path, host_end, sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = '\0';
    } else {
        strcpy(out->path, "/");
    }

    return 0;
}

/* ---- Resolve hostname if needed ---- */
PRIVATE int resolve_host(struct parsed_url *url, char *resolved_ip, int ip_cap)
{
    struct dns_result dns;
    int ret;

    /* Check if host is already an IP address (starts with digit) */
    if (url->host[0] >= '0' && url->host[0] <= '9') {
        strncpy(resolved_ip, url->host, ip_cap - 1);
        resolved_ip[ip_cap - 1] = '\0';
        return 0;
    }

    /* DNS lookup */
    ret = dns_resolve(url->host, &dns);
    if (ret != DNS_OK) {
        printf("curl: could not resolve host '%s'", url->host);
        if (ret == DNS_ERR_NXDOMAIN)
            printf(" (NXDOMAIN)\n");
        else if (ret == DNS_ERR_TIMEOUT)
            printf(" (timeout)\n");
        else
            printf(" (error %d)\n", ret);
        return -1;
    }

    snprintf(resolved_ip, ip_cap, "%d.%d.%d.%d",
             dns.addr[0], dns.addr[1], dns.addr[2], dns.addr[3]);
    return 0;
}

/* ---- Parse "Name: Value" into http_header ---- */
PRIVATE int parse_header_arg(const char *arg, struct http_header *hdr,
                             char *name_buf, int name_cap,
                             char *value_buf, int value_cap)
{
    const char *colon = strchr(arg, ':');
    int name_len;
    const char *val;

    if (!colon)
        return -1;

    name_len = colon - arg;
    if (name_len <= 0 || name_len >= name_cap)
        return -1;

    memcpy(name_buf, arg, name_len);
    name_buf[name_len] = '\0';

    /* Skip ": " */
    val = colon + 1;
    while (*val == ' ')
        val++;

    strncpy(value_buf, val, value_cap - 1);
    value_buf[value_cap - 1] = '\0';

    hdr->name = name_buf;
    hdr->value = value_buf;
    return 0;
}

int main(int argc, char *argv[])
{
    const char *method = (const char *)0;
    const char *data = (const char *)0;
    const char *output_file = (const char *)0;
    int verbose = 0;

    struct http_header headers[MAX_HEADERS + 1];
    char hdr_names[MAX_HEADERS][64];
    char hdr_values[MAX_HEADERS][128];
    int num_headers = 0;

    const char *url_str = (const char *)0;
    struct parsed_url url;
    char resolved_ip[20];

    struct http_request req;
    struct http_response resp;
    char recv_buf[RECV_BUF_SIZE];
    int ret;
    int i;
    int fd;
    int tls_inited = 0;

    /* Parse arguments */
    i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-X") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "-H") == 0 && i + 1 < argc) {
            i++;
            if (num_headers < MAX_HEADERS) {
                if (parse_header_arg(argv[i],
                                     &headers[num_headers],
                                     hdr_names[num_headers], 64,
                                     hdr_values[num_headers], 128) == 0) {
                    num_headers++;
                } else {
                    printf("curl: bad header format: %s\n", argv[i]);
                    exit(1);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] == '-') {
            printf("curl: unknown option: %s\n", argv[i]);
            exit(1);
            return 1;
        } else {
            url_str = argv[i];
        }
        i++;
    }

    if (!url_str) {
        printf("usage: curl [options] <url>\n");
        printf("  -X <method>   HTTP method (default: GET)\n");
        printf("  -d <data>     Request body\n");
        printf("  -H <header>   Add header \"Name: Value\"\n");
        printf("  -v            Verbose output\n");
        printf("  -o <file>     Write body to file\n");
        exit(1);
        return 1;
    }

    /* Default method: POST if data provided, GET otherwise */
    if (!method)
        method = data ? "POST" : "GET";

    /* Parse URL */
    if (parse_url(url_str, &url) < 0) {
        printf("curl: invalid URL: %s\n", url_str);
        exit(1);
        return 1;
    }

    if (verbose) {
        printf("> %s %s%s%s\n", method,
               url.use_tls ? "https://" : "http://",
               url.host, url.path);
    }

    /* TLS init for HTTPS */
    if (url.use_tls) {
        entropy_init();
        entropy_collect_jitter(512);
        ret = tls_init();
        if (ret != 0) {
            printf("curl: TLS init failed (%d)\n", ret);
            exit(1);
            return 1;
        }
        tls_inited = 1;
    }

    /* Resolve hostname for non-TLS requests.
     * TLS uses tls_connect() which handles DNS internally. */
    if (!url.use_tls) {
        if (resolve_host(&url, resolved_ip, sizeof(resolved_ip)) < 0) {
            exit(1);
            return 1;
        }
    }

    /* Terminate header list */
    headers[num_headers].name = (const char *)0;
    headers[num_headers].value = (const char *)0;

    /* Build request */
    memset(&req, 0, sizeof(req));
    req.method = method;
    req.host = url.use_tls ? url.host : resolved_ip;
    req.path = url.path;
    req.port = url.port;
    req.headers = num_headers > 0 ? headers : (const struct http_header *)0;
    req.body = data;
    req.body_len = data ? strlen(data) : 0;
    req.use_tls = url.use_tls;

    /* For HTTPS: streaming receive (TLS handles connection) */
    if (url.use_tls) {
        int send_len2;
        static char send_buf2[2048];
        static char hdr_buf[4096];
        int hdr_len = 0;
        int total_body = 0;
        int header_done = 0;
        int is_chunked = 0;
        struct chunked_state chunk_st;
        memset(&chunk_st, 0, sizeof(chunk_st));

        /* TLS connect */
        ret = tls_connect(url.host, url.port);
        if (ret != 0) {
            printf("curl: TLS connect failed (%d)\n", ret);
            exit(1);
            return 1;
        }

        /* Build and send HTTP request */
        {
            int pos = 0;
            pos = snprintf(send_buf2, sizeof(send_buf2),
                          "%s %s HTTP/1.1\r\nHost: %s\r\n",
                          method, url.path, url.host);
            for (i = 0; i < num_headers; i++) {
                pos += snprintf(send_buf2 + pos, sizeof(send_buf2) - pos,
                               "%s: %s\r\n", headers[i].name, headers[i].value);
            }
            if (req.body_len > 0)
                pos += snprintf(send_buf2 + pos, sizeof(send_buf2) - pos,
                               "Content-Length: %d\r\n", req.body_len);
            pos += snprintf(send_buf2 + pos, sizeof(send_buf2) - pos,
                           "User-Agent: sodex-curl/1.0\r\n"
                           "Accept: */*\r\n"
                           "Accept-Encoding: identity\r\n"
                           "Connection: close\r\n\r\n");
            send_len2 = pos;
        }

        tls_send(send_buf2, send_len2);
        if (req.body_len > 0)
            tls_send(req.body, req.body_len);

        /* Streaming receive: print body as it arrives */
        fd = -1;
        if (output_file) {
            fd = creat(output_file, 0644);
            if (fd < 0) {
                printf("curl: cannot open '%s'\n", output_file);
                tls_close();
                exit(1);
                return 1;
            }
        }

        {
        int zero_count = 0;
        for (;;) {
            ret = tls_recv(recv_buf, sizeof(recv_buf) - 1);
            if (ret <= 0) {
                /* TLS recv returned error or 0. TCP data may still be
                 * in flight from the server. Sleep briefly to let the
                 * network stack poll and receive more TCP segments. */
                if (++zero_count > 50)
                    break;
                sleep_ticks(2);  /* ~20ms per tick, give TCP time */
                continue;
            }
            zero_count = 0;

            if (!header_done) {
                /* Accumulate header data until we see \r\n\r\n */
                int copy = ret;
                char *sep;

                if (hdr_len + copy > (int)sizeof(hdr_buf) - 1)
                    copy = (int)sizeof(hdr_buf) - 1 - hdr_len;
                if (copy > 0) {
                    memcpy(hdr_buf + hdr_len, recv_buf, copy);
                    hdr_len += copy;
                    hdr_buf[hdr_len] = '\0';
                }

                sep = strstr(hdr_buf, "\r\n\r\n");
                if (sep) {
                    char *body_start = sep + 4;
                    int body_offset = body_start - hdr_buf;

                    header_done = 1;

                    /* Detect Transfer-Encoding: chunked */
                    {
                        char *te = strstr(hdr_buf, "Transfer-Encoding:");
                        if (!te)
                            te = strstr(hdr_buf, "transfer-encoding:");
                        if (te && te < body_start) {
                            if (strstr(te, "chunked"))
                                is_chunked = 1;
                        }
                    }

                    if (verbose) {
                        char *p = hdr_buf;
                        while (p < body_start) {
                            char *nl = strstr(p, "\r\n");
                            if (!nl || nl >= body_start) break;
                            *nl = '\0';
                            printf("< %s\n", p);
                            p = nl + 2;
                        }
                        printf("<\n");
                    }

                    /* Output body portion that was in the header buffer */
                    {
                        int body_len = hdr_len - body_offset;
                        if (body_len > 0) {
                            if (is_chunked) {
                                total_body += chunked_decode(&chunk_st,
                                    body_start, body_len, fd);
                            } else {
                                if (fd >= 0)
                                    write(fd, body_start, body_len);
                                else
                                    write(1, body_start, body_len);
                                total_body += body_len;
                            }
                        }
                    }

                    /* Also output any remaining data from recv_buf
                     * that didn't fit in hdr_buf */
                    if (copy < ret) {
                        int extra = ret - copy;
                        if (is_chunked) {
                            total_body += chunked_decode(&chunk_st,
                                recv_buf + copy, extra, fd);
                        } else {
                            if (fd >= 0)
                                write(fd, recv_buf + copy, extra);
                            else
                                write(1, recv_buf + copy, extra);
                            total_body += extra;
                        }
                    }
                }
            } else {
                /* Pure body data */
                if (is_chunked) {
                    total_body += chunked_decode(&chunk_st, recv_buf, ret, fd);
                    if (chunk_st.done)
                        break;
                } else {
                    if (fd >= 0)
                        write(fd, recv_buf, ret);
                    else
                        write(1, recv_buf, ret);
                    total_body += ret;
                }
            }
        }
        }

        tls_close();
        if (fd >= 0) {
            close(fd);
            if (verbose)
                printf("\nSaved %d bytes to %s\n", total_body, output_file);
        } else if (total_body > 0) {
            /* trailing newline */
            if (recv_buf[0] && recv_buf[ret > 0 ? ret - 1 : 0] != '\n')
                putc('\n');
        }

        exit(0);
        return 0;
    }

    /* Non-TLS: use http_do_request as before */
    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);

    if (ret != HTTP_OK) {
        printf("curl: request failed");
        if (ret == HTTP_ERR_CONNECT)
            printf(" (connection error)\n");
        else if (ret == HTTP_ERR_SEND)
            printf(" (send error)\n");
        else if (ret == HTTP_ERR_RECV)
            printf(" (receive error)\n");
        else if (ret == HTTP_ERR_TIMEOUT)
            printf(" (timeout)\n");
        else
            printf(" (error %d)\n", ret);
        exit(1);
        return 1;
    }

    if (verbose) {
        printf("< HTTP %d %s\n", resp.status_code, resp.status_text);
        if (resp.content_type[0])
            printf("< Content-Type: %s\n", resp.content_type);
        if (resp.content_length >= 0)
            printf("< Content-Length: %d\n", resp.content_length);
        printf("<\n");
    }

    if (resp.body && resp.body_len > 0) {
        if (output_file) {
            fd = creat(output_file, 0644);
            if (fd < 0) {
                printf("curl: cannot open '%s'\n", output_file);
                exit(1);
                return 1;
            }
            write(fd, resp.body, resp.body_len);
            close(fd);
        } else {
            write(1, resp.body, resp.body_len);
            if (resp.body[resp.body_len - 1] != '\n')
                putc('\n');
        }
    }

    exit(resp.status_code >= 200 && resp.status_code < 400 ? 0 : 1);
    return 0;
}
