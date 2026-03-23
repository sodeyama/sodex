#include "test_framework.h"
#include <sx_lexer.h>
#include <string.h>

static int find_kind_after(struct sx_token *tokens, int count, int start,
                           enum sx_token_kind kind)
{
    int i;

    for (i = start; i < count; i++) {
        if (tokens[i].kind == kind)
            return i;
    }
    return -1;
}

TEST(lex_function_and_control_flow) {
    const char *text =
        "fn pick(flag) -> bool {\n"
        "  for (let i = 0; i < 3 && !false; i = i + 1) {\n"
        "    if (i == 1) { continue; }\n"
        "    break;\n"
        "  }\n"
        "  let code = -(7 % 3);\n"
        "  return flag || code != 0;\n"
        "}\n";
    struct sx_token tokens[SX_MAX_TOKENS];
    struct sx_diagnostic diag;
    int count = sx_lex(text, (int)strlen(text), tokens, SX_MAX_TOKENS, &diag);

    ASSERT_EQ(count > 0, 1);
    ASSERT_EQ(tokens[0].kind, SX_TOKEN_KEYWORD_FN);
    ASSERT_STR_EQ(tokens[1].text, "pick");
    ASSERT_EQ(tokens[2].kind, SX_TOKEN_LPAREN);
    ASSERT_STR_EQ(tokens[3].text, "flag");
    ASSERT_EQ(tokens[4].kind, SX_TOKEN_RPAREN);
    ASSERT_EQ(tokens[5].kind, SX_TOKEN_ARROW);
    ASSERT_STR_EQ(tokens[6].text, "bool");
    ASSERT_EQ(tokens[7].kind, SX_TOKEN_LBRACE);
    ASSERT_EQ(find_kind_after(tokens, count, 8, SX_TOKEN_KEYWORD_FOR) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 9, SX_TOKEN_KEYWORD_LET) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 14, SX_TOKEN_LESS) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 17, SX_TOKEN_AND_AND) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 18, SX_TOKEN_BANG) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 19, SX_TOKEN_KEYWORD_FALSE) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 21, SX_TOKEN_PLUS) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 26, SX_TOKEN_KEYWORD_IF) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 30, SX_TOKEN_EQUAL_EQUAL) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 33, SX_TOKEN_KEYWORD_CONTINUE) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 37, SX_TOKEN_KEYWORD_BREAK) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 40, SX_TOKEN_KEYWORD_LET) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 43, SX_TOKEN_MINUS) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 46, SX_TOKEN_PERCENT) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 49, SX_TOKEN_KEYWORD_RETURN) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 53, SX_TOKEN_OR_OR) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 55, SX_TOKEN_BANG_EQUAL) >= 0, 1);
    ASSERT_EQ(tokens[count - 1].kind, SX_TOKEN_EOF);
}

TEST(lex_reports_unterminated_string) {
    const char *text = "io.println(\"oops);\n";
    struct sx_token tokens[SX_MAX_TOKENS];
    struct sx_diagnostic diag;

    ASSERT_EQ(sx_lex(text, (int)strlen(text), tokens, SX_MAX_TOKENS, &diag), -1);
    ASSERT_EQ(diag.span.line, 1);
    ASSERT_STR_EQ(diag.message, "unterminated string literal");
}

TEST(lex_collection_literals_and_else_if) {
    const char *text =
        "let items = [1, 2, 3];\n"
        "let meta = {\"name\": \"sx\", \"ok\": true};\n"
        "if (false) { io.println(\"a\"); } else if (true) { io.println(\"b\"); }\n";
    struct sx_token tokens[SX_MAX_TOKENS];
    struct sx_diagnostic diag;
    int count = sx_lex(text, (int)strlen(text), tokens, SX_MAX_TOKENS, &diag);

    ASSERT_EQ(count > 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 0, SX_TOKEN_LBRACKET) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 0, SX_TOKEN_RBRACKET) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 0, SX_TOKEN_COLON) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 0, SX_TOKEN_KEYWORD_ELSE) >= 0, 1);
    ASSERT_EQ(find_kind_after(tokens, count, 0, SX_TOKEN_KEYWORD_IF) >= 0, 1);
    ASSERT_EQ(tokens[count - 1].kind, SX_TOKEN_EOF);
}

int main(void)
{
    printf("=== sx lexer tests ===\n");

    RUN_TEST(lex_function_and_control_flow);
    RUN_TEST(lex_reports_unterminated_string);
    RUN_TEST(lex_collection_literals_and_else_if);

    TEST_REPORT();
}
