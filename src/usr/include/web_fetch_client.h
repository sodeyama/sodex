#ifndef _WEB_FETCH_CLIENT_H
#define _WEB_FETCH_CLIENT_H

#include <sys/types.h>

#define WEB_FETCH_DEFAULT_HOST         "10.0.2.2"
#define WEB_FETCH_DEFAULT_PORT         8081
#define WEB_FETCH_DEFAULT_PATH         "/fetch"
#define WEB_FETCH_DEFAULT_METHOD       "GET"
#define WEB_FETCH_DEFAULT_MAX_CHARS    4000
#define WEB_FETCH_DEFAULT_MAX_BYTES    262144

#define WEB_FETCH_MAX_URL             256
#define WEB_FETCH_MAX_HOST             64
#define WEB_FETCH_MAX_PATH             64
#define WEB_FETCH_MAX_METHOD            8
#define WEB_FETCH_MAX_CONTENT_TYPE     64
#define WEB_FETCH_MAX_TITLE           256
#define WEB_FETCH_MAX_EXCERPT         384
#define WEB_FETCH_MAX_MAIN_TEXT      4096
#define WEB_FETCH_MAX_FETCHED_AT       32
#define WEB_FETCH_MAX_SOURCE_HASH      80
#define WEB_FETCH_MAX_ERROR           160
#define WEB_FETCH_MAX_CODE             64
#define WEB_FETCH_MAX_LINKS             8
#define WEB_FETCH_MAX_LINK_HREF       160
#define WEB_FETCH_MAX_LINK_TEXT        96
#define WEB_FETCH_RECV_BUF_SIZE     49152

struct web_fetch_request {
    char url[WEB_FETCH_MAX_URL];
    char host[WEB_FETCH_MAX_HOST];
    char path[WEB_FETCH_MAX_PATH];
    char method[WEB_FETCH_MAX_METHOD];
    int  port;
    int  render_js;
    int  max_chars;
    int  max_bytes;
};

struct web_fetch_link {
    char href[WEB_FETCH_MAX_LINK_HREF];
    char text[WEB_FETCH_MAX_LINK_TEXT];
};

struct web_fetch_result {
    int gateway_status;
    int source_status;
    int truncated;
    int render_js;
    char url[WEB_FETCH_MAX_URL];
    char final_url[WEB_FETCH_MAX_URL];
    char method[WEB_FETCH_MAX_METHOD];
    char content_type[WEB_FETCH_MAX_CONTENT_TYPE];
    char title[WEB_FETCH_MAX_TITLE];
    char excerpt[WEB_FETCH_MAX_EXCERPT];
    char main_text[WEB_FETCH_MAX_MAIN_TEXT];
    char fetched_at[WEB_FETCH_MAX_FETCHED_AT];
    char source_hash[WEB_FETCH_MAX_SOURCE_HASH];
    char error[WEB_FETCH_MAX_ERROR];
    char code[WEB_FETCH_MAX_CODE];
    int  link_count;
    struct web_fetch_link links[WEB_FETCH_MAX_LINKS];
};

void web_fetch_request_init(struct web_fetch_request *req);
int web_fetch_parse_host_port(const char *text,
                              char *host, int host_cap,
                              int *port_out);
int web_fetch_execute(const struct web_fetch_request *req,
                      struct web_fetch_result *result);

#endif /* _WEB_FETCH_CLIENT_H */
