#ifndef _JSON_H
#define _JSON_H

#include <sys/types.h>

/* ---- Error codes ---- */
#define JSON_OK                  0
#define JSON_ERR_NOMEM          (-1)  /* Token array full */
#define JSON_ERR_INVALID        (-2)  /* Invalid JSON */
#define JSON_ERR_PARTIAL        (-3)  /* Input truncated */
#define JSON_ERR_KEY_NOT_FOUND  (-4)  /* Key not found */
#define JSON_ERR_TYPE_MISMATCH  (-5)  /* Unexpected type */
#define JSON_ERR_BUF_OVERFLOW   (-6)  /* Writer buffer overflow */

/* ---- Token types ---- */
enum json_type {
    JSON_NONE = 0,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_NULL
};

/* ---- Parsed token (references [start, end) in source) ---- */
struct json_token {
    enum json_type type;
    int start;          /* Offset in source string */
    int end;            /* Offset in source string (exclusive) */
    int size;           /* Children count (obj: key-value pairs, array: elements) */
    int parent;         /* Parent token index, -1 for root */
};

/* ---- Parser state ---- */
struct json_parser {
    int pos;            /* Current position in input */
    int toknext;        /* Next free token slot */
    int toksuper;       /* Current super/parent token */
    int error;          /* Error code */
};

#define JSON_MAX_TOKENS  256

/* ---- Parser API ---- */
void json_init(struct json_parser *parser);

/* Parse JSON string. Returns number of tokens, or negative error code. */
int json_parse(struct json_parser *parser,
               const char *js, int len,
               struct json_token *tokens, int num_tokens);

/* ---- Accessor API ---- */

/* Find value token for key within an object. Returns token index or -1. */
int json_find_key(const char *js,
                  const struct json_token *tokens, int token_count,
                  int obj_token, const char *key);

/* Get i-th element of an array. Returns token index or -1. */
int json_array_get(const struct json_token *tokens, int token_count,
                   int array_token, int index);

/* Copy token's string value to NUL-terminated buffer. Returns length or -1. */
int json_token_str(const char *js, const struct json_token *tok,
                   char *out, int out_cap);

/* Convert token to int. Returns 0 on success, -1 on error. */
int json_token_int(const char *js, const struct json_token *tok, int *out);

/* Convert token to bool. Returns 0 on success, -1 on error. */
int json_token_bool(const char *js, const struct json_token *tok, int *out);

/* Compare token string with a C string. Returns 1 if equal, 0 otherwise. */
int json_token_eq(const char *js, const struct json_token *tok, const char *s);

/* ---- JSON Writer ---- */
struct json_writer {
    char *buf;
    int   cap;
    int   len;
    int   error;        /* Set to 1 on buffer overflow */
    int   need_comma;   /* Insert comma before next value */
    int   depth;        /* Nesting depth */
};

void jw_init(struct json_writer *jw, char *buf, int cap);
void jw_object_start(struct json_writer *jw);
void jw_object_end(struct json_writer *jw);
void jw_array_start(struct json_writer *jw);
void jw_array_end(struct json_writer *jw);
void jw_key(struct json_writer *jw, const char *key);
void jw_string(struct json_writer *jw, const char *value);
void jw_string_n(struct json_writer *jw, const char *value, int len);
void jw_int(struct json_writer *jw, int value);
void jw_bool(struct json_writer *jw, int value);
void jw_null(struct json_writer *jw);
void jw_raw(struct json_writer *jw, const char *raw, int len);
int  jw_finish(struct json_writer *jw);  /* NUL-terminate. Returns length or -1 on error */

#endif /* _JSON_H */
