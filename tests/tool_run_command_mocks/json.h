#ifndef _JSON_H
#define _JSON_H

enum json_type {
    JSON_NONE = 0,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_NULL
};

struct json_token {
    enum json_type type;
    int start;
    int end;
    int size;
    int parent;
};

struct json_parser {
    int pos;
    int toknext;
    int toksuper;
    int error;
};

struct json_writer {
    char *buf;
    int cap;
    int len;
    int error;
};

void json_init(struct json_parser *parser);
int json_parse(struct json_parser *parser,
               const char *js, int len,
               struct json_token *tokens, int num_tokens);
int json_find_key(const char *js,
                  const struct json_token *tokens, int token_count,
                  int obj_token, const char *key);
int json_token_str(const char *js, const struct json_token *tok,
                   char *out, int out_cap);
void jw_init(struct json_writer *jw, char *buf, int cap);
void jw_object_start(struct json_writer *jw);
void jw_object_end(struct json_writer *jw);
void jw_key(struct json_writer *jw, const char *key);
void jw_string(struct json_writer *jw, const char *value);
void jw_int(struct json_writer *jw, int value);
void jw_bool(struct json_writer *jw, int value);
int jw_finish(struct json_writer *jw);

#endif
