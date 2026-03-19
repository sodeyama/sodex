/*
 * test_json.c - Host-side unit test for JSON parser/writer
 *
 * Compile: cc -I ../src/usr/include -o test_json test_json.c ../src/usr/lib/libagent/json.c test_stubs.c
 * Run:     ./test_json
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while(0)

/* ---- Parser tests ---- */

static void test_empty_object(void)
{
    const char *js = "{}";
    struct json_parser p;
    struct json_token tokens[8];
    int n;

    TEST_START("empty_object");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 8);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(tokens[0].type == JSON_OBJECT, "expected OBJECT");
    ASSERT(tokens[0].size == 0, "expected 0 children");
    TEST_PASS("empty_object");
}

static void test_empty_array(void)
{
    const char *js = "[]";
    struct json_parser p;
    struct json_token tokens[8];
    int n;

    TEST_START("empty_array");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 8);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(tokens[0].type == JSON_ARRAY, "expected ARRAY");
    ASSERT(tokens[0].size == 0, "expected 0 elements");
    TEST_PASS("empty_array");
}

static void test_simple_object(void)
{
    const char *js = "{\"name\":\"Jack\",\"age\":27}";
    struct json_parser p;
    struct json_token tokens[16];
    int n, tok;
    char val[32];
    int ival;

    TEST_START("simple_object");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 16);
    ASSERT(n == 5, "expected 5 tokens");

    tok = json_find_key(js, tokens, n, 0, "name");
    ASSERT(tok >= 0, "key 'name' not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, "Jack") == 0, "name mismatch");

    tok = json_find_key(js, tokens, n, 0, "age");
    ASSERT(tok >= 0, "key 'age' not found");
    json_token_int(js, &tokens[tok], &ival);
    ASSERT(ival == 27, "age mismatch");

    TEST_PASS("simple_object");
}

static void test_nested_object(void)
{
    const char *js = "{\"user\":{\"id\":1,\"name\":\"Alice\"},\"active\":true}";
    struct json_parser p;
    struct json_token tokens[32];
    int n, tok, user_tok;
    char val[32];
    int ival, bval;

    TEST_START("nested_object");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 32);
    ASSERT(n > 0, "parse failed");

    user_tok = json_find_key(js, tokens, n, 0, "user");
    ASSERT(user_tok >= 0, "key 'user' not found");
    ASSERT(tokens[user_tok].type == JSON_OBJECT, "user not object");

    tok = json_find_key(js, tokens, n, user_tok, "id");
    ASSERT(tok >= 0, "key 'id' not found");
    json_token_int(js, &tokens[tok], &ival);
    ASSERT(ival == 1, "id mismatch");

    tok = json_find_key(js, tokens, n, user_tok, "name");
    ASSERT(tok >= 0, "key 'name' not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, "Alice") == 0, "name mismatch");

    tok = json_find_key(js, tokens, n, 0, "active");
    ASSERT(tok >= 0, "key 'active' not found");
    json_token_bool(js, &tokens[tok], &bval);
    ASSERT(bval == 1, "active not true");

    TEST_PASS("nested_object");
}

static void test_array_access(void)
{
    const char *js = "[10,20,30]";
    struct json_parser p;
    struct json_token tokens[16];
    int n, tok, ival;

    TEST_START("array_access");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 16);
    ASSERT(n == 4, "expected 4 tokens");

    tok = json_array_get(tokens, n, 0, 0);
    ASSERT(tok >= 0, "elem 0 not found");
    json_token_int(js, &tokens[tok], &ival);
    ASSERT(ival == 10, "elem[0] mismatch");

    tok = json_array_get(tokens, n, 0, 2);
    ASSERT(tok >= 0, "elem 2 not found");
    json_token_int(js, &tokens[tok], &ival);
    ASSERT(ival == 30, "elem[2] mismatch");

    tok = json_array_get(tokens, n, 0, 3);
    ASSERT(tok < 0, "elem 3 should not exist");

    TEST_PASS("array_access");
}

static void test_string_escape(void)
{
    const char *js = "{\"msg\":\"hello\\nworld\\t\\\"quoted\\\"\"}";
    struct json_parser p;
    struct json_token tokens[16];
    int n, tok;
    char val[64];

    TEST_START("string_escape");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 16);
    ASSERT(n > 0, "parse failed");

    tok = json_find_key(js, tokens, n, 0, "msg");
    ASSERT(tok >= 0, "key not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, "hello\nworld\t\"quoted\"") == 0, "escape mismatch");

    TEST_PASS("string_escape");
}

static void test_unicode_escape(void)
{
    const char *js = "{\"msg\":\"\\u4eca\\u65e5 \\u6771\\u4eac\"}";
    const char *expected = "\xE4\xBB\x8A\xE6\x97\xA5 \xE6\x9D\xB1\xE4\xBA\xAC";
    struct json_parser p;
    struct json_token tokens[16];
    int n, tok;
    char val[64];

    TEST_START("unicode_escape");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 16);
    ASSERT(n > 0, "parse failed");

    tok = json_find_key(js, tokens, n, 0, "msg");
    ASSERT(tok >= 0, "key not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, expected) == 0, "unicode mismatch");

    TEST_PASS("unicode_escape");
}

static void test_unicode_surrogate_pair(void)
{
    const char *js = "{\"msg\":\"\\ud83d\\ude00\"}";
    const char *expected = "\xF0\x9F\x98\x80";
    struct json_parser p;
    struct json_token tokens[16];
    int n, tok;
    char val[16];

    TEST_START("unicode_surrogate_pair");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 16);
    ASSERT(n > 0, "parse failed");

    tok = json_find_key(js, tokens, n, 0, "msg");
    ASSERT(tok >= 0, "key not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, expected) == 0, "surrogate mismatch");

    TEST_PASS("unicode_surrogate_pair");
}

static void test_null_bool(void)
{
    const char *js = "{\"a\":null,\"b\":false,\"c\":true}";
    struct json_parser p;
    struct json_token tokens[16];
    int n, tok, bval;

    TEST_START("null_bool");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 16);
    ASSERT(n > 0, "parse failed");

    tok = json_find_key(js, tokens, n, 0, "a");
    ASSERT(tok >= 0, "key 'a' not found");
    ASSERT(tokens[tok].type == JSON_NULL, "a not null");

    tok = json_find_key(js, tokens, n, 0, "b");
    ASSERT(tok >= 0, "key 'b' not found");
    json_token_bool(js, &tokens[tok], &bval);
    ASSERT(bval == 0, "b not false");

    tok = json_find_key(js, tokens, n, 0, "c");
    ASSERT(tok >= 0, "key 'c' not found");
    json_token_bool(js, &tokens[tok], &bval);
    ASSERT(bval == 1, "c not true");

    TEST_PASS("null_bool");
}

static void test_negative_number(void)
{
    const char *js = "{\"val\":-42}";
    struct json_parser p;
    struct json_token tokens[8];
    int n, tok, ival;

    TEST_START("negative_number");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 8);
    ASSERT(n > 0, "parse failed");

    tok = json_find_key(js, tokens, n, 0, "val");
    ASSERT(tok >= 0, "key not found");
    json_token_int(js, &tokens[tok], &ival);
    ASSERT(ival == -42, "val mismatch");

    TEST_PASS("negative_number");
}

static void test_claude_response(void)
{
    const char *js =
        "{\"id\":\"msg_001\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"Hello!\"}],"
        "\"stop_reason\":\"end_turn\","
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";
    struct json_parser p;
    struct json_token tokens[64];
    int n, tok, elem, ival;
    char val[64];

    TEST_START("claude_response");
    json_init(&p);
    n = json_parse(&p, js, strlen(js), tokens, 64);
    ASSERT(n > 0, "parse failed");

    /* content[0].type == "text" */
    tok = json_find_key(js, tokens, n, 0, "content");
    ASSERT(tok >= 0, "content not found");
    elem = json_array_get(tokens, n, tok, 0);
    ASSERT(elem >= 0, "content[0] not found");

    tok = json_find_key(js, tokens, n, elem, "type");
    ASSERT(tok >= 0, "type not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, "text") == 0, "type mismatch");

    tok = json_find_key(js, tokens, n, elem, "text");
    ASSERT(tok >= 0, "text not found");
    json_token_str(js, &tokens[tok], val, sizeof(val));
    ASSERT(strcmp(val, "Hello!") == 0, "text mismatch");

    /* usage.input_tokens */
    tok = json_find_key(js, tokens, n, 0, "usage");
    ASSERT(tok >= 0, "usage not found");
    {
        int itok = json_find_key(js, tokens, n, tok, "input_tokens");
        ASSERT(itok >= 0, "input_tokens not found");
        json_token_int(js, &tokens[itok], &ival);
        ASSERT(ival == 10, "input_tokens mismatch");
    }

    TEST_PASS("claude_response");
}

/* ---- Writer tests ---- */

static void test_writer_empty(void)
{
    char buf[32];
    struct json_writer jw;

    TEST_START("writer_empty");
    jw_init(&jw, buf, sizeof(buf));
    jw_object_start(&jw);
    jw_object_end(&jw);
    jw_finish(&jw);
    ASSERT(strcmp(buf, "{}") == 0, "mismatch");
    TEST_PASS("writer_empty");
}

static void test_writer_nested(void)
{
    char buf[512];
    struct json_writer jw;
    const char *expected = "{\"model\":\"test\",\"max_tokens\":1024,\"messages\":[{\"role\":\"user\",\"content\":\"Hi\"}]}";

    TEST_START("writer_nested");
    jw_init(&jw, buf, sizeof(buf));
    jw_object_start(&jw);
    jw_key(&jw, "model");
    jw_string(&jw, "test");
    jw_key(&jw, "max_tokens");
    jw_int(&jw, 1024);
    jw_key(&jw, "messages");
    jw_array_start(&jw);
    jw_object_start(&jw);
    jw_key(&jw, "role");
    jw_string(&jw, "user");
    jw_key(&jw, "content");
    jw_string(&jw, "Hi");
    jw_object_end(&jw);
    jw_array_end(&jw);
    jw_object_end(&jw);
    ASSERT(jw_finish(&jw) >= 0, "jw_finish failed");
    ASSERT(strcmp(buf, expected) == 0, "output mismatch");

    TEST_PASS("writer_nested");
}

static void test_writer_escape(void)
{
    char buf[128];
    struct json_writer jw;

    TEST_START("writer_escape");
    jw_init(&jw, buf, sizeof(buf));
    jw_object_start(&jw);
    jw_key(&jw, "msg");
    jw_string(&jw, "line1\nline2\t\"quoted\"");
    jw_object_end(&jw);
    ASSERT(jw_finish(&jw) >= 0, "jw_finish failed");
    ASSERT(strcmp(buf, "{\"msg\":\"line1\\nline2\\t\\\"quoted\\\"\"}") == 0, "escape mismatch");

    TEST_PASS("writer_escape");
}

static void test_writer_overflow(void)
{
    char buf[8];
    struct json_writer jw;

    TEST_START("writer_overflow");
    jw_init(&jw, buf, sizeof(buf));
    jw_object_start(&jw);
    jw_key(&jw, "longkey");
    jw_string(&jw, "value");
    jw_object_end(&jw);
    ASSERT(jw_finish(&jw) < 0, "should overflow");

    TEST_PASS("writer_overflow");
}

static void test_writer_bool_null(void)
{
    char buf[128];
    struct json_writer jw;

    TEST_START("writer_bool_null");
    jw_init(&jw, buf, sizeof(buf));
    jw_object_start(&jw);
    jw_key(&jw, "a");
    jw_bool(&jw, 1);
    jw_key(&jw, "b");
    jw_bool(&jw, 0);
    jw_key(&jw, "c");
    jw_null(&jw);
    jw_object_end(&jw);
    ASSERT(jw_finish(&jw) >= 0, "jw_finish failed");
    ASSERT(strcmp(buf, "{\"a\":true,\"b\":false,\"c\":null}") == 0, "mismatch");

    TEST_PASS("writer_bool_null");
}

int main(void)
{
    printf("=== JSON Parser/Writer Unit Tests ===\n\n");

    /* Parser tests */
    test_empty_object();
    test_empty_array();
    test_simple_object();
    test_nested_object();
    test_array_access();
    test_string_escape();
    test_unicode_escape();
    test_unicode_surrogate_pair();
    test_null_bool();
    test_negative_number();
    test_claude_response();

    /* Writer tests */
    test_writer_empty();
    test_writer_nested();
    test_writer_escape();
    test_writer_overflow();
    test_writer_bool_null();

    printf("\n=== RESULT: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
