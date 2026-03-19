/*
 * webfetch.c - host 側の構造化 Web fetch gateway を叩くコマンド
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <web_fetch_client.h>

static void print_usage(void)
{
    printf("usage: webfetch [-I] [-r] [-m max_chars] [-h host:port] [-p path] <url>\n");
}

int main(int argc, char *argv[])
{
    static struct web_fetch_request req;
    static struct web_fetch_result result;
    const char *url = (const char *)0;
    int i;
    int ret;
    int failed = 0;

    web_fetch_request_init(&req);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-I") == 0) {
            strncpy(req.method, "HEAD", sizeof(req.method) - 1);
        } else if (strcmp(argv[i], "-r") == 0) {
            req.render_js = 1;
        } else if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                print_usage();
                return 1;
            }
            req.max_chars = atoi(argv[++i]);
            if (req.max_chars <= 0 || req.max_chars >= WEB_FETCH_MAX_MAIN_TEXT)
                req.max_chars = WEB_FETCH_MAX_MAIN_TEXT - 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            if (i + 1 >= argc ||
                web_fetch_parse_host_port(argv[++i],
                                          req.host, sizeof(req.host),
                                          &req.port) < 0) {
                printf("webfetch: invalid host:port\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc) {
                print_usage();
                return 1;
            }
            strncpy(req.path, argv[++i], sizeof(req.path) - 1);
            req.path[sizeof(req.path) - 1] = '\0';
        } else if (argv[i][0] == '-') {
            printf("webfetch: unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        } else {
            url = argv[i];
        }
    }

    if (!url) {
        print_usage();
        return 1;
    }

    if (strlen(url) >= sizeof(req.url)) {
        printf("webfetch: url too long\n");
        return 1;
    }
    strncpy(req.url, url, sizeof(req.url) - 1);
    req.url[sizeof(req.url) - 1] = '\0';

    ret = web_fetch_execute(&req, &result);
    if (ret < 0) {
        printf("webfetch: %s\n",
               result.error[0] ? result.error : "request failed");
        return 1;
    }

    if (result.gateway_status != 200 || result.error[0] != '\0')
        failed = 1;

    printf("Gateway Status: %d\n", result.gateway_status);
    if (result.source_status > 0)
        printf("Source Status: %d\n", result.source_status);
    printf("URL: %s\n", result.url);
    if (result.final_url[0] != '\0' && strcmp(result.final_url, result.url) != 0)
        printf("Final URL: %s\n", result.final_url);
    if (result.title[0] != '\0')
        printf("Title: %s\n", result.title);
    if (result.excerpt[0] != '\0')
        printf("Excerpt: %s\n", result.excerpt);
    if (result.content_type[0] != '\0')
        printf("Content-Type: %s\n", result.content_type);
    if (result.fetched_at[0] != '\0')
        printf("Fetched At: %s\n", result.fetched_at);
    if (result.source_hash[0] != '\0')
        printf("Source Hash: %s\n", result.source_hash);
    printf("Truncated: %s\n", result.truncated ? "yes" : "no");

    if (result.error[0] != '\0')
        printf("Error: %s\n", result.error);
    if (result.code[0] != '\0')
        printf("Code: %s\n", result.code);

    if (result.main_text[0] != '\0') {
        printf("\n");
        printf("%s\n", result.main_text);
    }

    if (result.link_count > 0) {
        int link_index;

        printf("\nLinks:\n");
        for (link_index = 0; link_index < result.link_count; link_index++) {
            printf("  - %s (%s)\n",
                   result.links[link_index].text,
                   result.links[link_index].href);
        }
    }

    return failed ? 1 : 0;
}
