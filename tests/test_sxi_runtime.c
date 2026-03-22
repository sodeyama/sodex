#include "test_framework.h"
#include <sx_parser.h>
#include <sx_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct output_buffer {
    char text[256];
    int len;
};

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
        return -1;
    if (fwrite(text, 1, strlen(text), fp) != strlen(text)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int read_text_file(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t read_len;

    if (fp == NULL || buf == NULL || cap == 0)
        return -1;
    read_len = fread(buf, 1, cap - 1, fp);
    buf[read_len] = '\0';
    fclose(fp);
    return 0;
}

static int append_output(void *ctx, const char *text, int len)
{
    struct output_buffer *out = (struct output_buffer *)ctx;
    int i;

    if (out == NULL)
        return -1;
    for (i = 0; i < len && out->len < (int)sizeof(out->text) - 1; i++)
        out->text[out->len++] = text[i];
    out->text[out->len] = '\0';
    return i == len ? 0 : -1;
}

static int discard_output(void *ctx, const char *text, int len)
{
    (void)ctx;
    (void)text;
    (void)len;
    return 0;
}

TEST(runtime_executes_function_if_block_and_return) {
    const char *text =
        "fn choose(flag) -> str {\n"
        "  if (flag) {\n"
        "    let inner = \"YES\";\n"
        "    return inner;\n"
        "  } else {\n"
        "    return \"NO\";\n"
        "  }\n"
        "}\n"
        "{\n"
        "  let shadow = \"INNER\";\n"
        "  io.println(shadow);\n"
        "}\n"
        "let flag = true;\n"
        "let name = choose(flag);\n"
        "io.println(name);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "INNER\nYES\n");
}

TEST(runtime_executes_while_and_return) {
    const char *text =
        "fn loop_once(flag) -> str {\n"
        "  while (flag) {\n"
        "    return \"LOOP\";\n"
        "  }\n"
        "  return \"STOP\";\n"
        "}\n"
        "let ok = true;\n"
        "let text = loop_once(ok);\n"
        "io.println(text);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "LOOP\n");
}

TEST(runtime_rejects_undefined_name_on_check) {
    const char *text = "io.println(name);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    sx_runtime_init(&runtime);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "undefined name");
}

TEST(runtime_rejects_duplicate_binding_in_same_scope) {
    const char *text =
        "let name = \"A\";\n"
        "let name = \"B\";\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    sx_runtime_init(&runtime);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "duplicate binding");
}

TEST(runtime_rejects_duplicate_function) {
    const char *text =
        "fn dup() -> str { return \"A\"; }\n"
        "fn dup() -> str { return \"B\"; }\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    sx_runtime_init(&runtime);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "duplicate function");
}

TEST(runtime_rejects_return_outside_function) {
    const char *text = "return \"no\";\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    sx_runtime_init(&runtime);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "return outside function");
}

TEST(runtime_fs_builtins_copy_and_append) {
    const char *src_path = "sxi_runtime_src.txt";
    const char *dst_path = "sxi_runtime_dst.txt";
    char script[512];
    char out[256];
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    ASSERT_EQ(write_text_file(src_path, "HELLO"), 0);
    remove(dst_path);
    snprintf(script, sizeof(script),
             "let src = \"%s\";\n"
             "let dst = \"%s\";\n"
             "let body = fs.read_text(src);\n"
             "fs.write_text(dst, body);\n"
             "fs.append_text(dst, \"!\");\n"
             "let ok = fs.exists(dst);\n"
             "io.println(ok);\n",
             src_path, dst_path);
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, discard_output, NULL);
    ASSERT_EQ(sx_parse_program(script, (int)strlen(script), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, discard_output, NULL);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_EQ(read_text_file(dst_path, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "HELLO!");
    remove(src_path);
    remove(dst_path);
}

TEST(runtime_executes_json_text_and_i32_builtins) {
    const char *text =
        "let raw = \"{\\\"name\\\":\\\"sx\\\",\\\"ok\\\":true,\\\"code\\\":7}\";\n"
        "let name = json.get_str(raw, \"name\");\n"
        "let ok = json.get_bool(raw, \"ok\");\n"
        "let code = json.get_i32(raw, \"code\");\n"
        "let joined = text.concat(name, text.trim(\"  hi  \"));\n"
        "let has_sx = text.contains(text.concat(name, \"hi\"), \"sx\");\n"
        "let raw_ok = json.valid(raw);\n"
        "io.println(text.trim(text.concat(joined, \"  \")));\n"
        "io.println(ok);\n"
        "io.println(code);\n"
        "io.println(has_sx);\n"
        "io.println(raw_ok);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "sxhi\ntrue\n7\ntrue\ntrue\n");
}

TEST(runtime_executes_proc_and_list_dir_builtins) {
    char dir_template[] = "/tmp/sxi_runtime_dirXXXXXX";
    char file_a[256];
    char file_b[256];
    char script[768];
    char *dir_path;
    const char *script_template =
        "let has_a = text.contains(fs.list_dir(\"%s\"), \"a.txt\");\n"
        "let captured = proc.capture(\"/bin/sh\", \"-c\", \"printf capture\");\n"
        "let ok = proc.status_ok(proc.run(\"/bin/sh\", \"-c\", \"exit 7\"));\n"
        "let status = proc.run(\"/bin/sh\", \"-c\", \"exit 7\");\n"
        "io.println(has_a);\n"
        "io.println(captured);\n"
        "io.println(ok);\n"
        "io.println(status);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    dir_path = mkdtemp(dir_template);
    ASSERT_EQ(dir_path != NULL, 1);
    snprintf(file_a, sizeof(file_a), "%s/a.txt", dir_path);
    snprintf(file_b, sizeof(file_b), "%s/b.txt", dir_path);
    ASSERT_EQ(write_text_file(file_a, "A"), 0);
    ASSERT_EQ(write_text_file(file_b, "B"), 0);
    snprintf(script, sizeof(script), script_template, dir_path);

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(script, (int)strlen(script), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "true\ncapture\nfalse\n7\n");

    remove(file_a);
    remove(file_b);
    rmdir(dir_path);
}

int main(void)
{
    printf("=== sxi runtime tests ===\n");

    RUN_TEST(runtime_executes_function_if_block_and_return);
    RUN_TEST(runtime_executes_while_and_return);
    RUN_TEST(runtime_rejects_undefined_name_on_check);
    RUN_TEST(runtime_rejects_duplicate_binding_in_same_scope);
    RUN_TEST(runtime_rejects_duplicate_function);
    RUN_TEST(runtime_rejects_return_outside_function);
    RUN_TEST(runtime_fs_builtins_copy_and_append);
    RUN_TEST(runtime_executes_json_text_and_i32_builtins);
    RUN_TEST(runtime_executes_proc_and_list_dir_builtins);

    TEST_REPORT();
}
