/*
 * websearch.c - Web search command using host-side websearch proxy
 *
 * Usage: websearch <query>
 *   -n <count>       Number of results to display [default: 5]
 *   -e <engine>      Search engine (google, bing, duckduckgo, all) [default: all]
 *   -j               Raw JSON output (no formatting)
 *   -h <host:port>   Proxy host [default: 10.0.2.2:8080]
 *   -p <path>        Proxy path [default: /search]
 *
 * Examples:
 *   websearch tcp ip stack
 *   websearch -n 3 linux kernel
 *   websearch -e google rust programming
 *   websearch -h 10.0.2.2:8080 -p /search hello world
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <http_client.h>
#include <json.h>

#define DEFAULT_HOST     "10.0.2.2"
#define DEFAULT_PORT     8080
#define DEFAULT_PATH     "/search"
#define DEFAULT_ENGINE   "all"
#define MAX_RESULTS      10
#define DEFAULT_RESULTS  5
#define RECV_BUF_SIZE    49152   /* 検索 proxy の応答は大きくなりやすい */
#define PATH_BUF_SIZE    512
#define QUERY_BUF_SIZE   256
#define JSON_TOKENS_MAX  4096    /* 入れ子の深い検索結果に対応する */

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

PRIVATE int json_find_any_key(const char *js,
                             const struct json_token *tokens, int token_count,
                             int obj_token,
                             const char *const *keys, int key_count)
{
    int i;
    int idx;

    for (i = 0; i < key_count; i++) {
        idx = json_find_key(js, tokens, token_count, obj_token, keys[i]);
        if (idx >= 0)
            return idx;
    }
    return -1;
}

PRIVATE int find_results_array(const char *js,
                               const struct json_token *tokens, int token_count)
{
    static const char *const root_keys[] = {
        "results",
        "items",
        "organic_results"
    };
    static const char *const nested_keys[] = {
        "results",
        "items"
    };
    static const char *const containers[] = {
        "data",
        "web"
    };
    int idx;
    int i;

    if (token_count <= 0)
        return -1;

    if (tokens[0].type == JSON_ARRAY)
        return 0;

    idx = json_find_any_key(js, tokens, token_count, 0,
                            root_keys, sizeof(root_keys) / sizeof(root_keys[0]));
    if (idx >= 0 && tokens[idx].type == JSON_ARRAY)
        return idx;

    /* host 側 proxy でよくある入れ子構造も順に試す */
    for (i = 0; i < (int)(sizeof(containers) / sizeof(containers[0])); i++) {
        idx = json_find_key(js, tokens, token_count, 0, containers[i]);
        if (idx >= 0 && tokens[idx].type == JSON_OBJECT) {
            int nested_idx = json_find_any_key(js, tokens, token_count, idx,
                                               nested_keys,
                                               sizeof(nested_keys) / sizeof(nested_keys[0]));
            if (nested_idx >= 0 && tokens[nested_idx].type == JSON_ARRAY)
                return nested_idx;
        }
    }

    return -1;
}

PRIVATE int find_result_count(const char *js,
                              const struct json_token *tokens, int token_count)
{
    static const char *const count_keys[] = {
        "number_of_results",
        "total",
        "total_results"
    };
    int idx;
    int value;

    if (token_count <= 0 || tokens[0].type != JSON_OBJECT)
        return -1;

    idx = json_find_any_key(js, tokens, token_count, 0,
                            count_keys,
                            sizeof(count_keys) / sizeof(count_keys[0]));
    if (idx >= 0 && json_token_int(js, &tokens[idx], &value) == 0)
        return value;

    return -1;
}

PRIVATE void print_result(const char *js,
                          const struct json_token *tokens, int token_count,
                          int result_idx, int rank)
{
    static const char *const title_keys[] = {
        "title",
        "name",
        "headline"
    };
    static const char *const url_keys[] = {
        "url",
        "link",
        "href"
    };
    static const char *const content_keys[] = {
        "content",
        "snippet",
        "description",
        "body",
        "text"
    };
    char title[256];
    char url[256];
    char content[512];
    int ti, ui, ci;

    title[0] = '\0';
    url[0] = '\0';
    content[0] = '\0';

    ti = json_find_any_key(js, tokens, token_count, result_idx,
                           title_keys,
                           sizeof(title_keys) / sizeof(title_keys[0]));
    if (ti >= 0)
        json_token_str(js, &tokens[ti], title, sizeof(title));

    ui = json_find_any_key(js, tokens, token_count, result_idx,
                           url_keys,
                           sizeof(url_keys) / sizeof(url_keys[0]));
    if (ui >= 0)
        json_token_str(js, &tokens[ui], url, sizeof(url));

    ci = json_find_any_key(js, tokens, token_count, result_idx,
                           content_keys,
                           sizeof(content_keys) / sizeof(content_keys[0]));
    if (ci >= 0)
        json_token_str(js, &tokens[ci], content, sizeof(content));

    if (title[0] == '\0' && url[0] != '\0') {
        strncpy(title, url, sizeof(title) - 1);
        title[sizeof(title) - 1] = '\0';
    }

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
    const char *path_base = DEFAULT_PATH;
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
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            path_base = argv[++i];
        } else if (argv[i][0] == '-') {
            printf("websearch: unknown option: %s\n", argv[i]);
            printf("usage: websearch [-n count] [-e engine] [-j] [-h host:port] [-p path] <query...>\n");
            exit(1);
            return 1;
        } else {
            if (num_words < 32)
                query_words[num_words++] = argv[i];
        }
        i++;
    }

    if (num_words == 0) {
        printf("usage: websearch [-n count] [-e engine] [-j] [-h host:port] [-p path] <query...>\n");
        printf("\n");
        printf("Options:\n");
        printf("  -n <count>      Number of results [default: 5, max: 10]\n");
        printf("  -e <engine>     Search engine (brave, bing, duckduckgo, all) [default: all]\n");
        printf("  -j              Raw JSON output\n");
        printf("  -h <host:port>  Proxy host [default: 10.0.2.2:8080]\n");
        printf("  -p <path>       Proxy path [default: /search]\n");
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

    /* 既定の query string を proxy path に付与する */
    if (engine && strcmp(engine, "all") != 0) {
        snprintf(path, sizeof(path),
                 "%s%sq=%s&format=json&engines=%s",
                 path_base,
                 strchr(path_base, '?') ? "&" : "?",
                 query_enc, engine);
    } else {
        snprintf(path, sizeof(path),
                 "%s%sq=%s&format=json",
                 path_base,
                 strchr(path_base, '?') ? "&" : "?",
                 query_enc);
    }

    if (strlen(path) >= sizeof(path) - 1) {
        printf("websearch: request path too long\n");
        exit(1);
        return 1;
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
        debug_printf("[WEBSEARCH] request_failed ret=%d host=%s port=%d\n",
                     ret, host, port);
        printf("websearch: request failed");
        if (ret == HTTP_ERR_CONNECT)
            printf(" (cannot connect to %s:%d)\n", host, port);
        else if (ret == HTTP_ERR_TIMEOUT)
            printf(" (timeout)\n");
        else
            printf(" (error %d)\n", ret);
        printf("\nhost 側 websearch proxy の起動と接続先を確認してください\n");
        exit(1);
        return 1;
    }

    if (resp.status_code != 200) {
        debug_printf("[WEBSEARCH] bad_status code=%d text=%s\n",
                     resp.status_code, resp.status_text);
        printf("websearch: HTTP %d %s\n", resp.status_code, resp.status_text);
        exit(1);
        return 1;
    }

    if (!resp.body || resp.body_len <= 0) {
        debug_printf("[WEBSEARCH] empty_body\n");
        printf("websearch: empty response\n");
        exit(1);
        return 1;
    }

    /* Raw JSON mode: just dump body */
    if (raw_json) {
        debug_printf("[WEBSEARCH] raw_json bytes=%d\n", resp.body_len);
        write(1, resp.body, resp.body_len);
        if (resp.body[resp.body_len - 1] != '\n')
            putc('\n');
        exit(0);
        return 0;
    }

    /* Parse JSON response */
    {
        struct json_parser parser;
        static struct json_token tokens[JSON_TOKENS_MAX];
        int token_count;
        int results_idx;
        int num_total;
        int total_hint;
        int show;

        json_init(&parser);
        token_count = json_parse(&parser, resp.body, resp.body_len,
                                 tokens, JSON_TOKENS_MAX);

        if (token_count < 0) {
            debug_printf("[WEBSEARCH] json_parse_error code=%d\n", token_count);
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

        results_idx = find_results_array(resp.body, tokens, token_count);
        if (results_idx < 0 || tokens[results_idx].type != JSON_ARRAY) {
            debug_printf("[WEBSEARCH] results_not_found\n");
            printf("websearch: no results found in response\n");
            exit(1);
            return 1;
        }

        num_total = tokens[results_idx].size;
        total_hint = find_result_count(resp.body, tokens, token_count);

        /* Display header */
        if (total_hint >= 0) {
            printf("Search: \"%s\" (%d results)\n\n", query_raw, total_hint);
        } else {
            printf("Search: \"%s\"\n\n", query_raw);
        }

        /* Display results */
        show = (num_total < max_results) ? num_total : max_results;
        for (i = 0; i < show; i++) {
            int ri = json_array_get(tokens, token_count, results_idx, i);
            if (ri < 0)
                break;
            print_result(resp.body, tokens, token_count, ri, i + 1);
        }

        debug_printf("[WEBSEARCH] rendered=%d total=%d body=%d\n",
                     show, num_total, resp.body_len);

        if (num_total > show)
            printf("... and %d more results\n", num_total - show);
    }

    exit(0);
    return 0;
}
