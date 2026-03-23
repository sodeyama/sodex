#include "test_framework.h"
#include <arpa/inet.h>
#include <sx_parser.h>
#include <sx_runtime.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static int write_raw_file(const char *path, const unsigned char *data, size_t len)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
        return -1;
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
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

static int count_active_pipes(const struct sx_runtime *runtime)
{
    int count = 0;
    int i;

    if (runtime == NULL)
        return 0;
    for (i = 0; i < SX_MAX_PIPE_HANDLES; i++) {
        if (runtime->pipes[i].active != 0)
            count++;
    }
    return count;
}

static int count_active_sockets(const struct sx_runtime *runtime)
{
    int count = 0;
    int i;

    if (runtime == NULL)
        return 0;
    for (i = 0; i < SX_MAX_SOCKET_HANDLES; i++) {
        if (runtime->sockets[i].active != 0)
            count++;
    }
    return count;
}

static int create_tcp_listener(int *out_port)
{
    struct sockaddr_in addr;
    socklen_t addr_len = (socklen_t)sizeof(addr);
    int reuse = 1;
    int fd;

    if (out_port == NULL)
        return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 4) < 0 ||
        getsockname(fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        close(fd);
        return -1;
    }
    *out_port = (int)ntohs(addr.sin_port);
    return fd;
}

static int reserve_tcp_port(void)
{
    int fd;
    int port = -1;

    fd = create_tcp_listener(&port);
    if (fd < 0)
        return -1;
    close(fd);
    return port;
}

static int connect_with_retry(int port, int attempts, int delay_us)
{
    int attempt;

    for (attempt = 0; attempt < attempts; attempt++) {
        struct sockaddr_in addr;
        int fd = socket(AF_INET, SOCK_STREAM, 0);

        if (fd < 0)
            return -1;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_int16_t)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0)
            return fd;
        close(fd);
        usleep((useconds_t)delay_us);
    }
    return -1;
}

static int read_socket_text(int fd, char *buf, size_t cap)
{
    ssize_t len;

    if (fd < 0 || buf == NULL || cap == 0)
        return -1;
    len = recv(fd, buf, cap - 1, 0);
    if (len < 0)
        return -1;
    buf[len] = '\0';
    return (int)len;
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

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "true\ncapture\nfalse\n7\n");

    remove(file_a);
    remove(file_b);
    rmdir(dir_path);
}

TEST(runtime_executes_argv_fs_and_time_builtins) {
    char dir_template[] = "/tmp/sxi_runtime_interopXXXXXX";
    char *dir_path;
    char script[1024];
    char *argv_values[] = { "script.sx", "alpha", "beta", NULL };
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    dir_path = mkdtemp(dir_template);
    ASSERT_EQ(dir_path != NULL, 1);
    snprintf(script, sizeof(script),
             "let root = \"%s\";\n"
             "let sub = text.concat(root, \"/sub\");\n"
             "let base = fs.cwd();\n"
             "fs.mkdir(sub);\n"
             "let before = time.now_ticks();\n"
             "time.sleep_ticks(1);\n"
             "fs.chdir(sub);\n"
             "let cwd = fs.cwd();\n"
             "let dir_ok = fs.is_dir(cwd);\n"
             "fs.write_text(\"note.txt\", proc.argv(1));\n"
             "fs.rename(\"note.txt\", \"renamed.txt\");\n"
             "let content = fs.read_text(\"renamed.txt\");\n"
             "fs.remove(\"renamed.txt\");\n"
             "fs.chdir(base);\n"
             "fs.remove(sub);\n"
             "let after = time.now_ticks();\n"
             "io.println(proc.argv_count());\n"
             "io.println(proc.argv(0));\n"
             "io.println(proc.argv(2));\n"
             "io.println(text.contains(cwd, \"/sub\"));\n"
             "io.println(dir_ok);\n"
             "io.println(content);\n"
             "io.println(after >= before);\n",
             dir_path);

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_set_argv(&runtime, 3, argv_values), 0);
    ASSERT_EQ(sx_parse_program(script, (int)strlen(script), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_set_argv(&runtime, 3, argv_values), 0);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "3\nscript.sx\nbeta\ntrue\ntrue\nalpha\ntrue\n");
    sx_runtime_dispose(&runtime);
    rmdir(dir_path);
}

TEST(runtime_executes_io_pipe_spawn_and_wait) {
    const char *stdin_path = "/tmp/sxi_runtime_stdin.txt";
    const char *text =
        "let line = io.read_line();\n"
        "let rest = io.read_all();\n"
        "let input = proc.pipe();\n"
        "let output = proc.pipe();\n"
        "let in_r = proc.pipe_read_fd(input);\n"
        "let in_w = proc.pipe_write_fd(input);\n"
        "let out_r = proc.pipe_read_fd(output);\n"
        "let out_w = proc.pipe_write_fd(output);\n"
        "let pid = proc.spawn_io(\"/bin/sh\", in_r, out_w, -1, \"-c\", \"cat\");\n"
        "io.close(in_r);\n"
        "io.close(out_w);\n"
        "io.write_fd(in_w, text.concat(line, rest));\n"
        "io.close(in_w);\n"
        "let body = io.read_fd(out_r);\n"
        "io.close(out_r);\n"
        "proc.pipe_close(input);\n"
        "proc.pipe_close(output);\n"
        "let status = proc.wait(pid);\n"
        "io.println(body);\n"
        "io.println(proc.status_ok(status));\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;
    int saved_stdin = -1;
    int stdin_fd = -1;

    ASSERT_EQ(write_text_file(stdin_path, "HELLO\n_PIPE"), 0);
    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    saved_stdin = dup(STDIN_FILENO);
    ASSERT_EQ(saved_stdin >= 0, 1);
    stdin_fd = open(stdin_path, O_RDONLY, 0);
    ASSERT_EQ(stdin_fd >= 0, 1);
    ASSERT_EQ(dup2(stdin_fd, STDIN_FILENO) >= 0, 1);
    close(stdin_fd);
    stdin_fd = -1;
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_EQ(dup2(saved_stdin, STDIN_FILENO) >= 0, 1);
    close(saved_stdin);
    saved_stdin = -1;
    ASSERT_STR_EQ(out.text, "HELLO_PIPE\ntrue\n");
    sx_runtime_dispose(&runtime);
    remove(stdin_path);
}

TEST(runtime_executes_fork_and_exit) {
    const char *fork_path = "/tmp/sxi_runtime_fork.txt";
    char script[512];
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    remove(fork_path);
    snprintf(script, sizeof(script),
             "let path = \"%s\";\n"
             "let pid = proc.fork();\n"
             "if (pid == 0) {\n"
             "  fs.write_text(path, \"child\");\n"
             "  proc.exit(5);\n"
             "}\n"
             "let status = proc.wait(pid);\n"
             "io.println(proc.status_ok(status));\n"
             "io.println(status);\n"
             "io.println(fs.read_text(path));\n"
             "fs.remove(path);\n",
             fork_path);

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(script, (int)strlen(script), &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "false\n5\nchild\n");
    sx_runtime_dispose(&runtime);
}

TEST(runtime_executes_env_bytes_and_result_builtins) {
    const char *src_path = "/tmp/sxi_runtime_bytes.bin";
    const char *dst_path = "/tmp/sxi_runtime_bytes_out.bin";
    const unsigned char raw_bytes[] = { 'A', 0, 'B' };
    char script[1024];
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    ASSERT_EQ(write_raw_file(src_path, raw_bytes, sizeof(raw_bytes)), 0);
    remove(dst_path);
    ASSERT_EQ(setenv("SX_TEST_ENV", "env-ok", 1), 0);
    snprintf(script, sizeof(script),
             "let env_ok = proc.has_env(\"SX_TEST_ENV\");\n"
             "let env_value = proc.env(\"SX_TEST_ENV\");\n"
             "let missing = fs.try_read_text(\"/tmp/does-not-exist.sx\");\n"
             "let raw = fs.read_bytes(\"%s\");\n"
             "let pipe = proc.pipe();\n"
             "let read_fd = proc.pipe_read_fd(pipe);\n"
             "let write_fd = proc.pipe_write_fd(pipe);\n"
             "io.write_fd_bytes(write_fd, raw);\n"
             "io.close(write_fd);\n"
             "let round = io.read_fd_bytes(read_fd);\n"
             "io.close(read_fd);\n"
             "proc.pipe_close(pipe);\n"
             "fs.write_bytes(\"%s\", round);\n"
             "let ok_capture = proc.try_capture(\"/bin/sh\", \"-c\", \"printf cap\");\n"
             "test.assert_eq(env_ok, true);\n"
             "test.assert_eq(env_value, \"env-ok\");\n"
             "test.assert_eq(bytes.len(raw), 3);\n"
             "test.assert_eq(raw, round);\n"
             "test.assert_eq(result.is_ok(missing), false);\n"
             "test.assert_eq(result.error(missing), \"fs.read_text failed\");\n"
             "test.assert_eq(result.is_ok(ok_capture), true);\n"
             "test.assert_eq(result.value(ok_capture), \"cap\");\n"
             "io.println(env_value);\n"
             "io.println(bytes.len(round));\n"
             "io.println(result.error(missing));\n"
             "io.println(result.value(ok_capture));\n",
             src_path, dst_path);

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(script, (int)strlen(script), &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "env-ok\n3\nfs.read_text failed\ncap\n");
    sx_runtime_dispose(&runtime);
    remove(src_path);
    remove(dst_path);
}

TEST(runtime_executes_list_and_map_builtins) {
    const char *text =
        "let items = list.new();\n"
        "list.push(items, 7);\n"
        "list.push(items, 9);\n"
        "test.assert_eq(list.len(items), 2);\n"
        "test.assert_eq(list.get(items, 1), 9);\n"
        "list.set(items, 0, 11);\n"
        "let meta = map.new();\n"
        "map.set(meta, \"name\", \"sx\");\n"
        "map.set(meta, \"count\", list.len(items));\n"
        "test.assert_eq(map.has(meta, \"name\"), true);\n"
        "test.assert_eq(map.get(meta, \"count\"), 2);\n"
        "map.remove(meta, \"name\");\n"
        "test.assert_eq(map.has(meta, \"name\"), false);\n"
        "let ok = result.ok(list.get(items, 0));\n"
        "test.assert_eq(result.is_ok(ok), true);\n"
        "test.assert_eq(result.value(ok), 11);\n"
        "io.println(list.get(items, 0));\n"
        "io.println(map.len(meta));\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);

    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "11\n1\n");
}

TEST(runtime_executes_literals_and_else_if) {
    const char *text =
        "let items = [7, 9, 11];\n"
        "let meta = {\"name\": \"sx\", \"count\": list.len(items)};\n"
        "let branch = \"none\";\n"
        "if (false) {\n"
        "  branch = \"a\";\n"
        "} else if (map.get(meta, \"count\") == 3) {\n"
        "  branch = map.get(meta, \"name\");\n"
        "} else {\n"
        "  branch = \"c\";\n"
        "}\n"
        "io.println(list.get(items, 1));\n"
        "io.println(branch);\n";
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
    ASSERT_STR_EQ(out.text, "9\nsx\n");
    sx_runtime_dispose(&runtime);
}

TEST(runtime_executes_net_client_builtins) {
    char script[512];
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct output_buffer out;
    int listener = -1;
    int port = -1;
    pid_t pid = -1;
    int status = 0;

    listener = create_tcp_listener(&port);
    ASSERT_EQ(listener >= 0, 1);
    pid = fork();
    ASSERT_EQ(pid >= 0, 1);
    if (pid == 0) {
        char recv_buf[32];
        int client_fd = accept(listener, NULL, NULL);

        if (client_fd < 0)
            _exit(2);
        if (read_socket_text(client_fd, recv_buf, sizeof(recv_buf)) < 0)
            _exit(3);
        if (strcmp(recv_buf, "PING") != 0)
            _exit(4);
        if (send(client_fd, "PONG", 4, 0) != 4)
            _exit(5);
        close(client_fd);
        close(listener);
        _exit(0);
    }
    close(listener);
    listener = -1;

    snprintf(script, sizeof(script),
             "let sock = net.connect(\"127.0.0.1\", %d);\n"
             "net.write(sock, \"PING\");\n"
             "let ready = net.poll_read(sock, 1000);\n"
             "let body = net.read(sock);\n"
             "net.close(sock);\n"
             "io.println(ready);\n"
             "io.println(body);\n",
             port);

    memset(&out, 0, sizeof(out));
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_parse_program(script, (int)strlen(script), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), 0);
    ASSERT_STR_EQ(out.text, "true\nPONG\n");
    sx_runtime_dispose(&runtime);

    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_EQ(WIFEXITED(status), 1);
    ASSERT_EQ(WEXITSTATUS(status), 0);
}

TEST(runtime_executes_net_server_builtins) {
    char script[512];
    int stdout_pipe[2];
    int port = -1;
    pid_t pid;
    int client_fd = -1;
    char reply[32];
    char child_out[128];
    int status = 0;

    ASSERT_EQ(pipe(stdout_pipe), 0);
    port = reserve_tcp_port();
    ASSERT_EQ(port > 0, 1);

    pid = fork();
    ASSERT_EQ(pid >= 0, 1);
    if (pid == 0) {
        struct sx_program program;
        struct sx_diagnostic diag;
        struct sx_runtime runtime;
        struct output_buffer out;

        close(stdout_pipe[0]);
        snprintf(script, sizeof(script),
                 "let listener = net.listen(%d);\n"
                 "let sock = net.accept(listener);\n"
                 "let ready = net.poll_read(sock, 1000);\n"
                 "let body = net.read(sock);\n"
                 "net.write(sock, \"WORLD\");\n"
                 "net.close(sock);\n"
                 "net.close(listener);\n"
                 "io.println(ready);\n"
                 "io.println(body);\n",
                 port);
        memset(&out, 0, sizeof(out));
        sx_runtime_init(&runtime);
        sx_runtime_set_output(&runtime, append_output, &out);
        if (sx_parse_program(script, (int)strlen(script), &program, &diag) < 0)
            _exit(10);
        if (sx_runtime_execute_program(&runtime, &program, &diag) < 0)
            _exit(11);
        write(stdout_pipe[1], out.text, (size_t)out.len);
        close(stdout_pipe[1]);
        sx_runtime_dispose(&runtime);
        _exit(0);
    }

    close(stdout_pipe[1]);
    client_fd = connect_with_retry(port, 40, 50000);
    ASSERT_EQ(client_fd >= 0, 1);
    ASSERT_EQ(send(client_fd, "HELLO", 5, 0), 5);
    ASSERT_EQ(read_socket_text(client_fd, reply, sizeof(reply)) >= 0, 1);
    ASSERT_STR_EQ(reply, "WORLD");
    close(client_fd);
    client_fd = -1;

    memset(child_out, 0, sizeof(child_out));
    ASSERT_EQ(read(stdout_pipe[0], child_out, sizeof(child_out) - 1) >= 0, 1);
    close(stdout_pipe[0]);
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_EQ(WIFEXITED(status), 1);
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_STR_EQ(child_out, "true\nHELLO\n");
}

TEST(runtime_executes_operators_assignment_and_loop_control) {
    const char *text =
        "let sum = 0;\n"
        "let i = 0;\n"
        "while (i < 4) {\n"
        "  sum = sum + i;\n"
        "  i = i + 1;\n"
        "}\n"
        "for (let j = 0; j < 6; j = j + 1) {\n"
        "  if (j == 1) {\n"
        "    continue;\n"
        "  }\n"
        "  if (j == 5) {\n"
        "    break;\n"
        "  }\n"
        "  sum = sum + j;\n"
        "}\n"
        "let ok = sum == 15 && !(false || false);\n"
        "io.println(sum);\n"
        "io.println(ok);\n";
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
    ASSERT_STR_EQ(out.text, "15\ntrue\n");
}

TEST(runtime_executes_recursive_function) {
    const char *text =
        "fn sum_to(n) -> i32 {\n"
        "  if (n == 0) {\n"
        "    return 0;\n"
        "  }\n"
        "  return n + sum_to(n - 1);\n"
        "}\n"
        "let value = sum_to(6);\n"
        "io.println(value);\n";
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
    ASSERT_STR_EQ(out.text, "21\n");
}

TEST(runtime_rejects_break_outside_loop) {
    const char *text = "break;\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    sx_runtime_init(&runtime);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "break outside loop");
}

TEST(runtime_rejects_continue_outside_loop) {
    const char *text = "continue;\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;

    sx_runtime_init(&runtime);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "continue outside loop");
}

TEST(runtime_reset_session_preserves_configuration_and_closes_pipes) {
    char setup_text[128];
    const char *after_reset_text = "io.println(proc.argv(1));\n";
    char *argv_values[] = { "script.sx", "kept-arg", NULL };
    struct sx_program setup_program;
    struct sx_program after_reset_program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct sx_runtime_limits limits;
    struct output_buffer out;
    int port;

    memset(&out, 0, sizeof(out));
    port = reserve_tcp_port();
    ASSERT_EQ(port > 0, 1);
    snprintf(setup_text, sizeof(setup_text),
             "let handle = proc.pipe();\n"
             "let listener = net.listen(%d);\n",
             port);
    sx_runtime_init(&runtime);
    sx_runtime_set_output(&runtime, append_output, &out);
    ASSERT_EQ(sx_runtime_set_argv(&runtime, 2, argv_values), 0);
    sx_runtime_default_limits(&limits);
    limits.max_loop_iterations = 3;
    ASSERT_EQ(sx_runtime_set_limits(&runtime, &limits), 0);
    ASSERT_EQ(sx_parse_program(setup_text, (int)strlen(setup_text), &setup_program, &diag), 0);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &setup_program, &diag), 0);
    ASSERT_EQ(count_active_pipes(&runtime) > 0, 1);
    ASSERT_EQ(count_active_sockets(&runtime) > 0, 1);

    sx_runtime_reset_session(&runtime);
    ASSERT_EQ(runtime.binding_count, 0);
    ASSERT_EQ(runtime.scope_depth, 0);
    ASSERT_EQ(runtime.call_depth, 0);
    ASSERT_EQ(runtime.error_call_depth, 0);
    ASSERT_EQ(runtime.argc, 2);
    ASSERT_EQ(runtime.limits.max_loop_iterations, 3);
    ASSERT_EQ(count_active_pipes(&runtime), 0);
    ASSERT_EQ(count_active_sockets(&runtime), 0);

    ASSERT_EQ(sx_parse_program(after_reset_text, (int)strlen(after_reset_text), &after_reset_program, &diag), 0);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &after_reset_program, &diag), 0);
    ASSERT_STR_EQ(out.text, "kept-arg\n");
    sx_runtime_dispose(&runtime);
}

TEST(runtime_enforces_custom_binding_limit) {
    const char *text =
        "let a = 1;\n"
        "let b = 2;\n"
        "let c = 3;\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct sx_runtime_limits limits;

    sx_runtime_init(&runtime);
    sx_runtime_default_limits(&limits);
    limits.max_bindings = 2;
    ASSERT_EQ(sx_runtime_set_limits(&runtime, &limits), 0);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_check_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "binding table is full");
}

TEST(runtime_enforces_custom_loop_limit) {
    const char *text =
        "let i = 0;\n"
        "while (i < 4) {\n"
        "  i = i + 1;\n"
        "}\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct sx_runtime_limits limits;

    sx_runtime_init(&runtime);
    sx_runtime_default_limits(&limits);
    limits.max_loop_iterations = 2;
    ASSERT_EQ(sx_runtime_set_limits(&runtime, &limits), 0);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "while iteration limit exceeded");
    sx_runtime_dispose(&runtime);
}

TEST(runtime_enforces_custom_call_limit) {
    const char *text =
        "fn descend(n) -> i32 {\n"
        "  if (n == 0) {\n"
        "    return 0;\n"
        "  }\n"
        "  return descend(n - 1);\n"
        "}\n"
        "let value = descend(4);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    struct sx_runtime runtime;
    struct sx_runtime_limits limits;

    sx_runtime_init(&runtime);
    sx_runtime_default_limits(&limits);
    limits.max_call_depth = 2;
    ASSERT_EQ(sx_runtime_set_limits(&runtime, &limits), 0);
    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(sx_runtime_execute_program(&runtime, &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "call depth limit exceeded");
    sx_runtime_dispose(&runtime);
}

TEST(runtime_rejects_invalid_limit_configuration) {
    struct sx_runtime runtime;
    struct sx_runtime_limits limits;

    sx_runtime_init(&runtime);
    sx_runtime_default_limits(&limits);
    limits.max_bindings = SX_MAX_BINDINGS + 1;
    ASSERT_EQ(sx_runtime_set_limits(&runtime, &limits), -1);
    limits.max_bindings = SX_MAX_BINDINGS;
    limits.max_call_depth = 0;
    ASSERT_EQ(sx_runtime_set_limits(&runtime, &limits), -1);
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
    RUN_TEST(runtime_executes_argv_fs_and_time_builtins);
    RUN_TEST(runtime_executes_io_pipe_spawn_and_wait);
    RUN_TEST(runtime_executes_fork_and_exit);
    RUN_TEST(runtime_executes_env_bytes_and_result_builtins);
    RUN_TEST(runtime_executes_list_and_map_builtins);
    RUN_TEST(runtime_executes_literals_and_else_if);
    RUN_TEST(runtime_executes_net_client_builtins);
    RUN_TEST(runtime_executes_net_server_builtins);
    RUN_TEST(runtime_executes_operators_assignment_and_loop_control);
    RUN_TEST(runtime_executes_recursive_function);
    RUN_TEST(runtime_rejects_break_outside_loop);
    RUN_TEST(runtime_rejects_continue_outside_loop);
    RUN_TEST(runtime_reset_session_preserves_configuration_and_closes_pipes);
    RUN_TEST(runtime_enforces_custom_binding_limit);
    RUN_TEST(runtime_enforces_custom_loop_limit);
    RUN_TEST(runtime_enforces_custom_call_limit);
    RUN_TEST(runtime_rejects_invalid_limit_configuration);

    TEST_REPORT();
}
