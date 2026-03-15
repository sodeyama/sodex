#include "test_framework.h"
#include <eshell_parser.h>
#include <stdio.h>
#include <string.h>

TEST(parse_quotes_and_escape) {
    struct eshell_pipeline pipeline;
    char line[] = "mv \"two words.txt\" three\\ four";

    ASSERT_EQ(eshell_parse_line(line, strlen(line), &pipeline), 1);
    ASSERT_EQ(pipeline.command_count, 1);
    ASSERT_STR_EQ(pipeline.commands[0].argv[0], "mv");
    ASSERT_STR_EQ(pipeline.commands[0].argv[1], "two words.txt");
    ASSERT_STR_EQ(pipeline.commands[0].argv[2], "three four");
    ASSERT_EQ(pipeline.commands[0].argv[3], NULL);
}

TEST(parse_multi_stage_pipeline_and_append) {
    struct eshell_pipeline pipeline;
    char line[] = "ls | cat | cat >> out.txt";

    ASSERT_EQ(eshell_parse_line(line, strlen(line), &pipeline), 3);
    ASSERT_EQ(pipeline.command_count, 3);
    ASSERT_STR_EQ(pipeline.commands[0].argv[0], "ls");
    ASSERT_STR_EQ(pipeline.commands[1].argv[0], "cat");
    ASSERT_STR_EQ(pipeline.commands[2].argv[0], "cat");
    ASSERT_STR_EQ(pipeline.commands[2].output_path, "out.txt");
    ASSERT_EQ(pipeline.commands[2].append_output, 1);
}

TEST(parse_input_and_output_redirection) {
    struct eshell_pipeline pipeline;
    char line[] = "cat < input.txt > output.txt";

    ASSERT_EQ(eshell_parse_line(line, strlen(line), &pipeline), 1);
    ASSERT_EQ(pipeline.command_count, 1);
    ASSERT_STR_EQ(pipeline.commands[0].argv[0], "cat");
    ASSERT_STR_EQ(pipeline.commands[0].input_path, "input.txt");
    ASSERT_STR_EQ(pipeline.commands[0].output_path, "output.txt");
    ASSERT_EQ(pipeline.commands[0].append_output, 0);
}

TEST(parse_quoted_redirection_paths) {
    struct eshell_pipeline pipeline;
    char line[] = "cat < \"two words.txt\" >> \"three four.txt\"";

    ASSERT_EQ(eshell_parse_line(line, strlen(line), &pipeline), 1);
    ASSERT_STR_EQ(pipeline.commands[0].argv[0], "cat");
    ASSERT_STR_EQ(pipeline.commands[0].input_path, "two words.txt");
    ASSERT_STR_EQ(pipeline.commands[0].output_path, "three four.txt");
    ASSERT_EQ(pipeline.commands[0].append_output, 1);
}

TEST(parse_rejects_unclosed_quote) {
    struct eshell_pipeline pipeline;
    char line[] = "touch \"broken";

    ASSERT_EQ(eshell_parse_line(line, strlen(line), &pipeline), -1);
}

TEST(parse_rejects_too_many_arguments) {
    struct eshell_pipeline pipeline;
    char line[] = "touch a b c";

    ASSERT_EQ(eshell_parse_line(line, strlen(line), &pipeline), -1);
}

int main(void)
{
    printf("=== eshell parser tests ===\n");

    RUN_TEST(parse_quotes_and_escape);
    RUN_TEST(parse_multi_stage_pipeline_and_append);
    RUN_TEST(parse_input_and_output_redirection);
    RUN_TEST(parse_quoted_redirection_paths);
    RUN_TEST(parse_rejects_unclosed_quote);
    RUN_TEST(parse_rejects_too_many_arguments);

    TEST_REPORT();
}
