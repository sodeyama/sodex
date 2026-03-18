/*
 * websearch.c - Web search command using SearXNG
 *
 * Usage: websearch <query>
 *   -n <count>       Number of results to display [default: 5]
 *   -e <engine>      Search engine (google, bing, duckduckgo) [default: all]
 *   -j               Raw JSON output (no formatting)
 *   -h <host:port>   SearXNG host [default: 10.0.2.2:8080]
 *
 * Requires SearXNG running on the host:
 *   docker run -d -p 8080:8080 searxng/searxng
 *
 * Examples:
 *   websearch tcp ip stack
 *   websearch -n 3 linux kernel
 *   websearch -e google rust programming
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <http_client.h>
#include <json.h>

#define DEFAULT_HOST     "10.0.2.2"
#define DEFAULT_PORT     8080
#define DEFAULT_ENGINE   "brave"  /* Single engine keeps response small */
#define MAX_RESULTS      10
#define DEFAULT_RESULTS  5
#define RECV_BUF_SIZE    49152   /* SearXNG responses can be ~30KB+ */
#define PATH_BUF_SIZE    512
#define QUERY_BUF_SIZE   256
#define JSON_TOKENS_MAX  4096    /* SearXNG JSON has many nested objects */

/* ---- URL encoding ---- */

/*
 * Check if a character is "unreserved" per RFC 3986 and safe in a query
 * component.  Everything else must be percent-encoded.
 */
PRIVATE int is_url_safe(char c)
{
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '-' || c == '_' || c == '.' || c == '~') return 1;
    return 0;
}

/*
 * URL-encode src into dst.  Returns bytes written (not counting NUL),
 * or -1 if dst is too small.
 */
PRIVATE int url_encode(const char *src, char *dst, int dst_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    int si = 0;
    int di = 0;

    while (src[si]) {
        char c = src[si];

        if (is_url_safe(c)) {
            if (di + 1 >= dst_cap) return -1;
            dst[di++] = c;
        } else if (c == ' ') {
            /* Space -> '+' in query string (application/x-www-form-urlencoded) */
            if (di + 1 >= dst_cap) return -1;
            dst[di++] = '+';
        } else {
            if (di + 3 >= dst_cap) return -1;
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0x0F];
            dst[di++] = hex[c & 0x0F];
        }
        si++;
    }
    if (di >= dst_cap) return -1;
    dst[di] = '\0';
    return di;
}

/* ---- Result display ---- */

PRIVATE void print_result(const char *js,
                          const struct json_token *tokens, int token_count,
                          int result_idx, int rank)
{
    char title[256];
    char url[256];
    char content[512];
    int ti, ui, ci;

    title[0] = '\0';
    url[0] = '\0';
    content[0] = '\0';

    ti = json_find_key(js, tokens, token_count, result_idx, "title");
    if (ti >= 0)
        json_token_str(js, &tokens[ti], title, sizeof(title));

    ui = json_find_key(js, tokens, token_count, result_idx, "url");
    if (ui >= 0)
        json_token_str(js, &tokens[ui], url, sizeof(url));

    ci = json_find_key(js, tokens, token_count, result_idx, "content");
    if (ci >= 0)
        json_token_str(js, &tokens[ci], content, sizeof(content));

    printf("[%d] %s\n", rank, title);
    printf("    %s\n", url);
    if (content[0])
        printf("    %s\n", content);
    printf("\n");
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    char host[64];
    int port = DEFAULT_PORT;
    int max_results = DEFAULT_RESULTS;
    int raw_json = 0;
    const char *engine = DEFAULT_ENGINE;

    /* Collect query words */
    char *query_words[32];
    int num_words = 0;

    char query_raw[QUERY_BUF_SIZE];
    char query_enc[QUERY_BUF_SIZE];
    char path[PATH_BUF_SIZE];
    char resolved_ip[20];

    struct http_request req;
    struct http_response resp;
    static char recv_buf[RECV_BUF_SIZE];
    int ret;
    int i;

    strncpy(host, DEFAULT_HOST, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';

    /* Parse arguments */
    i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_results = 0;
            {
                const char *p = argv[++i];
                while (*p >= '0' && *p <= '9') {
                    max_results = max_results * 10 + (*p - '0');
                    p++;
                }
            }
            if (max_results > MAX_RESULTS) max_results = MAX_RESULTS;
            if (max_results < 1) max_results = 1;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            engine = argv[++i];
        } else if (strcmp(argv[i], "-j") == 0) {
            raw_json = 1;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            i++;
            /* Parse host:port */
            {
                const char *colon = strchr(argv[i], ':');
                if (colon) {
                    int hlen = colon - argv[i];
                    if (hlen >= (int)sizeof(host)) hlen = sizeof(host) - 1;
                    memcpy(host, argv[i], hlen);
                    host[hlen] = '\0';
                    port = 0;
                    {
                        const char *pp = colon + 1;
                        while (*pp >= '0' && *pp <= '9') {
                            port = port * 10 + (*pp - '0');
                            pp++;
                        }
                    }
                } else {
                    strncpy(host, argv[i], sizeof(host) - 1);
                    host[sizeof(host) - 1] = '\0';
                }
            }
        } else if (argv[i][0] == '-') {
            printf("websearch: unknown option: %s\n", argv[i]);
            printf("usage: websearch [-n count] [-e engine] [-j] [-h host:port] <query...>\n");
            exit(1);
            return 1;
        } else {
            if (num_words < 32)
                query_words[num_words++] = argv[i];
        }
        i++;
    }

    if (num_words == 0) {
        printf("usage: websearch [-n count] [-e engine] [-j] [-h host:port] <query...>\n");
        printf("\n");
        printf("Options:\n");
        printf("  -n <count>      Number of results [default: 5, max: 10]\n");
        printf("  -e <engine>     Search engine (brave, bing, duckduckgo, all) [default: brave]\n");
        printf("  -j              Raw JSON output\n");
        printf("  -h <host:port>  SearXNG host [default: 10.0.2.2:8080]\n");
        exit(1);
        return 1;
    }

    /* Build raw query string from words */
    {
        int pos = 0;
        for (i = 0; i < num_words; i++) {
            if (i > 0 && pos < (int)sizeof(query_raw) - 1)
                query_raw[pos++] = ' ';
            {
                const char *w = query_words[i];
                while (*w && pos < (int)sizeof(query_raw) - 1)
                    query_raw[pos++] = *w++;
            }
        }
        query_raw[pos] = '\0';
    }

    /* URL-encode query */
    if (url_encode(query_raw, query_enc, sizeof(query_enc)) < 0) {
        printf("websearch: query too long\n");
        exit(1);
        return 1;
    }

    /* Build request path.  -e all disables engine filter. */
    if (engine && strcmp(engine, "all") != 0) {
        snprintf(path, sizeof(path),
                 "/search?q=%s&format=json&engines=%s", query_enc, engine);
    } else {
        snprintf(path, sizeof(path),
                 "/search?q=%s&format=json", query_enc);
    }

    /* Resolve host (should be an IP, but handle hostname too) */
    strncpy(resolved_ip, host, sizeof(resolved_ip) - 1);
    resolved_ip[sizeof(resolved_ip) - 1] = '\0';

    /* Build HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.host = resolved_ip;
    req.path = path;
    req.port = port;
    req.headers = (const struct http_header *)0;
    req.body = (const char *)0;
    req.body_len = 0;
    req.use_tls = 0;

    /* Perform request */
    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);

    if (ret != HTTP_OK) {
        printf("websearch: request failed");
        if (ret == HTTP_ERR_CONNECT)
            printf(" (cannot connect to %s:%d)\n", host, port);
        else if (ret == HTTP_ERR_TIMEOUT)
            printf(" (timeout)\n");
        else
            printf(" (error %d)\n", ret);
        printf("\nMake sure SearXNG is running:\n");
        printf("  docker-compose -f searxng/docker-compose.yml up -d\n");
        exit(1);
        return 1;
    }

    if (resp.status_code != 200) {
        printf("websearch: HTTP %d %s\n", resp.status_code, resp.status_text);
        exit(1);
        return 1;
    }

    if (!resp.body || resp.body_len <= 0) {
        printf("websearch: empty response\n");
        exit(1);
        return 1;
    }

    /* Raw JSON mode: just dump body */
    if (raw_json) {
        write(1, resp.body, resp.body_len);
        if (resp.body[resp.body_len - 1] != '\n')
            putc('\n');
        exit(0);
        return 0;
    }

    /* Parse JSON response */
    {
        struct json_parser parser;
        /* SearXNG responses can be large; use a bigger token pool */
        static struct json_token tokens[JSON_TOKENS_MAX];
        int token_count;
        int results_idx;
        int num_total;
        int show;

        json_init(&parser);
        token_count = json_parse(&parser, resp.body, resp.body_len,
                                 tokens, JSON_TOKENS_MAX);

        if (token_count < 0) {
            printf("websearch: JSON parse error (%d)\n", token_count);
            /* Show a snippet for debugging */
            {
                int show_len = resp.body_len < 200 ? resp.body_len : 200;
                write(1, resp.body, show_len);
                putc('\n');
            }
            exit(1);
            return 1;
        }

        /* Find "results" array */
        results_idx = json_find_key(resp.body, tokens, token_count,
                                    0, "results");
        if (results_idx < 0 || tokens[results_idx].type != JSON_ARRAY) {
            printf("websearch: no results found in response\n");
            exit(1);
            return 1;
        }

        num_total = tokens[results_idx].size;

        /* Display header */
        {
            int nr_idx = json_find_key(resp.body, tokens, token_count,
                                       0, "number_of_results");
            if (nr_idx >= 0) {
                int nr = 0;
                json_token_int(resp.body, &tokens[nr_idx], &nr);
                printf("Search: \"%s\" (%d results)\n\n", query_raw, nr);
            } else {
                printf("Search: \"%s\"\n\n", query_raw);
            }
        }

        /* Display results */
        show = (num_total < max_results) ? num_total : max_results;
        for (i = 0; i < show; i++) {
            int ri = json_array_get(tokens, token_count, results_idx, i);
            if (ri < 0)
                break;
            print_result(resp.body, tokens, token_count, ri, i + 1);
        }

        if (num_total > show)
            printf("... and %d more results\n", num_total - show);
    }

    exit(0);
    return 0;
}
