#include "test_framework.h"
#include <sx_lexer.h>
#include <string.h>

TEST(lex_function_and_control_flow) {
    const char *text =
        "fn pick(flag) -> bool {\n"
        "  if (flag) { return true; } else { return false; }\n"
        "  let code = -7;\n"
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
    ASSERT_EQ(tokens[8].kind, SX_TOKEN_KEYWORD_IF);
    ASSERT_EQ(tokens[12].kind, SX_TOKEN_LBRACE);
    ASSERT_EQ(tokens[13].kind, SX_TOKEN_KEYWORD_RETURN);
    ASSERT_EQ(tokens[14].kind, SX_TOKEN_KEYWORD_TRUE);
    ASSERT_EQ(tokens[17].kind, SX_TOKEN_KEYWORD_ELSE);
    ASSERT_EQ(tokens[19].kind, SX_TOKEN_KEYWORD_RETURN);
    ASSERT_EQ(tokens[20].kind, SX_TOKEN_KEYWORD_FALSE);
    ASSERT_EQ(tokens[23].kind, SX_TOKEN_KEYWORD_LET);
    ASSERT_STR_EQ(tokens[24].text, "code");
    ASSERT_EQ(tokens[26].kind, SX_TOKEN_INT);
    ASSERT_STR_EQ(tokens[26].text, "-7");
    ASSERT_EQ(tokens[29].kind, SX_TOKEN_EOF);
}

TEST(lex_reports_unterminated_string) {
    const char *text = "io.println(\"oops);\n";
    struct sx_token tokens[SX_MAX_TOKENS];
    struct sx_diagnostic diag;

    ASSERT_EQ(sx_lex(text, (int)strlen(text), tokens, SX_MAX_TOKENS, &diag), -1);
    ASSERT_EQ(diag.span.line, 1);
    ASSERT_STR_EQ(diag.message, "unterminated string literal");
}

int main(void)
{
    printf("=== sx lexer tests ===\n");

    RUN_TEST(lex_function_and_control_flow);
    RUN_TEST(lex_reports_unterminated_string);

    TEST_REPORT();
}
