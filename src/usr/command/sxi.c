#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>
#include <sx_parser.h>
#include <sx_runtime.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#define SXI_DEBUG_PRINTF(...) ((void)0)
#define SXI_DEBUG_WRITE(buf, len) ((void)0)
#else
#include <debug.h>
#define SXI_DEBUG_PRINTF(...) debug_printf(__VA_ARGS__)
#define SXI_DEBUG_WRITE(buf, len) debug_write((buf), (len))
#endif

#define SXI_REPL_LINE_MAX 512
#define SXI_REPL_BUFFER_MAX 4096
#define SXI_IMPORT_MAX 32
#define SXI_PATH_SEGMENT_MAX 256
#define SXI_GUEST_STDLIB_DIR "/usr/lib/sx"

struct sxi_string_builder {
  char *data;
  int len;
  int cap;
};

struct sxi_loader_state {
  char stack[SXI_IMPORT_MAX][PATHNAME_MAX];
  int stack_count;
  char loaded[SXI_IMPORT_MAX][PATHNAME_MAX];
  int loaded_count;
};

static char *sxi_read_fd_all(int fd, int *out_len);
static char *sxi_read_file_all(const char *path, int *out_len);
static void sxi_write_text(int fd, const char *text);
static void sxi_print_usage(void);
static void sxi_print_help(void);
static void sxi_print_version(void);
static void sxi_print_diagnostic(const char *name,
                                 const struct sx_diagnostic *diag,
                                 const struct sx_runtime *runtime);
static int sxi_run_text(struct sx_runtime *runtime,
                        const char *name,
                        const char *text,
                        int check_only);
static int sxi_run_file(struct sx_runtime *runtime,
                        const char *path,
                        int check_only);
static int sxi_run_file_with_loader(struct sx_runtime *runtime,
                                    const char *path,
                                    int check_only,
                                    struct sxi_loader_state *loader);
static int sxi_trim_line(char *line);
static int sxi_read_line(char *buf, int cap);
static int sxi_append_line(char *buf, int cap, int *len, const char *line);
static int sxi_repl(void);
static int sxi_builder_init(struct sxi_string_builder *builder);
static void sxi_builder_reset(struct sxi_string_builder *builder);
static int sxi_builder_append(struct sxi_string_builder *builder,
                              const char *text, int len);
static int sxi_parse_import_line(const char *line, int len,
                                 char *import_path, int import_cap);
static int sxi_resolve_import_path(const char *current_path,
                                   const char *import_path,
                                   char *resolved, int resolved_cap);
static int sxi_path_exists(const char *path);
static int sxi_has_sx_suffix(const char *path);
static int sxi_try_resolve_candidate(const char *base_dir,
                                     const char *import_path,
                                     char *resolved, int resolved_cap);
static int sxi_resolve_stdlib_path(const char *import_path,
                                   char *resolved, int resolved_cap);
static int sxi_collect_source_tree(const char *path,
                                   struct sxi_loader_state *loader,
                                   struct sxi_string_builder *builder,
                                   struct sx_diagnostic *diag,
                                   int *error_kind);

int sxi_source_needs_more_input(const char *text)
{
  int brace_depth = 0;
  int paren_depth = 0;
  int in_string = 0;
  int in_comment = 0;
  int escaped = 0;
  int i;

  if (text == 0)
    return 0;
  for (i = 0; text[i] != '\0'; i++) {
    char ch = text[i];
    char next = text[i + 1];

    if (in_comment != 0) {
      if (ch == '\n')
        in_comment = 0;
      continue;
    }
    if (in_string != 0) {
      if (escaped != 0) {
        escaped = 0;
        continue;
      }
      if (ch == '\\') {
        escaped = 1;
        continue;
      }
      if (ch == '"')
        in_string = 0;
      continue;
    }
    if (ch == '/' && next == '/') {
      in_comment = 1;
      i++;
      continue;
    }
    if (ch == '"') {
      in_string = 1;
      continue;
    }
    if (ch == '{')
      brace_depth++;
    else if (ch == '}') {
      if (brace_depth > 0)
        brace_depth--;
    } else if (ch == '(')
      paren_depth++;
    else if (ch == ')') {
      if (paren_depth > 0)
        paren_depth--;
    }
  }
  return in_string != 0 || brace_depth > 0 || paren_depth > 0;
}

int sxi_command_main(int argc, char **argv)
{
  struct sx_runtime *runtime;
  int status = 1;

  SXI_DEBUG_PRINTF("AUDIT sxi_main_enter argc=%d argv0=%s\n",
                   argc, (argc > 0 && argv != 0 && argv[0] != 0) ? argv[0] : "(null)");
  runtime = (struct sx_runtime *)malloc(sizeof(*runtime));
  if (runtime == 0) {
    sxi_write_text(STDERR_FILENO, "sxi: out of memory\n");
    SXI_DEBUG_WRITE("AUDIT sxi_malloc_runtime_fail\n", 30);
    return 1;
  }
  sx_runtime_init(runtime);
  if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
    sxi_print_version();
    status = 0;
    goto out;
  }
  if (argc >= 2 && strcmp(argv[1], "--check") == 0) {
    if (argc < 3) {
      sxi_print_usage();
      goto out;
    }
    if (sx_runtime_set_argv(runtime, argc - 2, &argv[2]) < 0) {
      sxi_write_text(STDERR_FILENO, "sxi: script arguments are too large\n");
      goto out;
    }
    status = sxi_run_file(runtime, argv[2], 1);
    goto out;
  }
  if (argc >= 2 && strcmp(argv[1], "-e") == 0) {
    char *expr_argv[SX_MAX_RUNTIME_ARGS];
    int expr_argc = 0;
    int i;

    if (argc < 3) {
      sxi_print_usage();
      goto out;
    }
    expr_argv[expr_argc++] = "<expr>";
    for (i = 3; i < argc && expr_argc < SX_MAX_RUNTIME_ARGS; i++)
      expr_argv[expr_argc++] = argv[i];
    if (i < argc || sx_runtime_set_argv(runtime, expr_argc, expr_argv) < 0) {
      sxi_write_text(STDERR_FILENO, "sxi: script arguments are too large\n");
      goto out;
    }
    status = sxi_run_text(runtime, "<expr>", argv[2], 0);
    goto out;
  }
  if (argc >= 2) {
    if (sx_runtime_set_argv(runtime, argc - 1, &argv[1]) < 0) {
      sxi_write_text(STDERR_FILENO, "sxi: script arguments are too large\n");
      goto out;
    }
    status = sxi_run_file(runtime, argv[1], 0);
  } else
    status = sxi_repl();

out:
  sx_runtime_dispose(runtime);
  free(runtime);
  return status;
}

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return sxi_command_main(argc, argv);
}
#endif

static void sxi_write_text(int fd, const char *text)
{
  if (text == 0)
    return;
  write(fd, text, strlen(text));
}

static void sxi_print_usage(void)
{
  sxi_write_text(STDERR_FILENO,
                 "usage: sxi [--check <file.sx> [args...] | -e <code> [args...] | <file.sx> [args...]]\n");
}

static void sxi_print_help(void)
{
  sxi_write_text(STDOUT_FILENO,
                 ":help  show commands\n"
                 ":load <file.sx>  run file in current session\n"
                 ":reset  clear current session\n"
                 ":quit  exit repl\n");
}

static void sxi_print_version(void)
{
  char buf[128];

  snprintf(buf, sizeof(buf),
           "sxi %s (sx %s, frontend abi %d, runtime abi %d)\n",
           SX_LANGUAGE_VERSION,
           SX_LANGUAGE_VERSION,
           SX_FRONTEND_ABI_VERSION,
           SX_RUNTIME_ABI_VERSION);
  sxi_write_text(STDOUT_FILENO, buf);
}

static void sxi_print_diagnostic(const char *name,
                                 const struct sx_diagnostic *diag,
                                 const struct sx_runtime *runtime)
{
  char buf[256];
  char trace[256];
  int line = 0;
  int column = 0;

  if (diag != 0) {
    line = diag->span.line;
    column = diag->span.column;
  }
  snprintf(buf, sizeof(buf), "%s:%d:%d: %s\n",
           name != 0 ? name : "<input>",
           line, column,
           (diag != 0 && diag->message[0] != '\0') ?
               diag->message : "unknown error");
  SXI_DEBUG_PRINTF("AUDIT sxi_diag name=%s line=%d col=%d msg=%s\n",
                   name != 0 ? name : "<input>",
                   line, column,
                   (diag != 0 && diag->message[0] != '\0') ?
                       diag->message : "unknown error");
  sxi_write_text(STDERR_FILENO, buf);
  if (sx_runtime_format_stack_trace(runtime, trace, sizeof(trace)) > 0)
    sxi_write_text(STDERR_FILENO, trace);
}

static int sxi_builder_init(struct sxi_string_builder *builder)
{
  if (builder == 0)
    return -1;
  memset(builder, 0, sizeof(*builder));
  builder->cap = 512;
  builder->data = (char *)malloc((size_t)builder->cap);
  if (builder->data == 0)
    return -1;
  builder->data[0] = '\0';
  return 0;
}

static void sxi_builder_reset(struct sxi_string_builder *builder)
{
  if (builder == 0)
    return;
  if (builder->data != 0)
    free(builder->data);
  builder->data = 0;
  builder->len = 0;
  builder->cap = 0;
}

static int sxi_builder_append(struct sxi_string_builder *builder,
                              const char *text, int len)
{
  char *next;
  int next_cap;

  if (builder == 0 || text == 0 || len < 0)
    return -1;
  if (builder->data == 0 && sxi_builder_init(builder) < 0)
    return -1;

  while (builder->len + len + 1 > builder->cap) {
    next_cap = builder->cap * 2;
    if (next_cap < builder->len + len + 1)
      next_cap = builder->len + len + 1;
    next = (char *)malloc((size_t)next_cap);
    if (next == 0)
      return -1;
    memset(next, 0, (size_t)next_cap);
    if (builder->len > 0)
      memcpy(next, builder->data, (size_t)builder->len);
    free(builder->data);
    builder->data = next;
    builder->cap = next_cap;
  }

  if (len > 0)
    memcpy(builder->data + builder->len, text, (size_t)len);
  builder->len += len;
  builder->data[builder->len] = '\0';
  return 0;
}

static int sxi_path_dirname(const char *path, char *dir, int dir_cap)
{
  int last_slash = -1;
  int i;

  if (path == 0 || dir == 0 || dir_cap <= 0)
    return -1;
  for (i = 0; path[i] != '\0'; i++) {
    if (path[i] == '/')
      last_slash = i;
  }
  if (last_slash < 0)
    return sx_copy_text(dir, dir_cap, ".");
  if (last_slash == 0)
    return sx_copy_text(dir, dir_cap, "/");
  return sx_copy_slice(dir, dir_cap, path, last_slash);
}

static int sxi_normalize_path(const char *path, char *out, int out_cap)
{
  char segments[SXI_IMPORT_MAX][SXI_PATH_SEGMENT_MAX];
  int segment_count = 0;
  int absolute = 0;
  int pos = 0;
  int len = 0;
  int i;

  if (path == 0 || out == 0 || out_cap <= 0)
    return -1;
  if (path[0] == '/') {
    absolute = 1;
    pos = 1;
  }

  while (path[pos] != '\0') {
    char segment[PATHNAME_MAX];
    int start = pos;
    int seg_len;

    while (path[pos] != '\0' && path[pos] != '/')
      pos++;
    seg_len = pos - start;
    if (seg_len > 0) {
      if (seg_len >= (int)sizeof(segment))
        return -1;
      sx_copy_slice(segment, sizeof(segment), path + start, seg_len);
      if (strcmp(segment, ".") == 0) {
      } else if (strcmp(segment, "..") == 0) {
        if (segment_count > 0 &&
            strcmp(segments[segment_count - 1], "..") != 0)
          segment_count--;
        else if (absolute == 0) {
          if (segment_count >= SXI_IMPORT_MAX)
            return -1;
          sx_copy_text(segments[segment_count++],
                       sizeof(segments[0]), segment);
        }
      } else {
        if (segment_count >= SXI_IMPORT_MAX)
          return -1;
        sx_copy_text(segments[segment_count++],
                     sizeof(segments[0]), segment);
      }
    }
    while (path[pos] == '/')
      pos++;
  }

  out[0] = '\0';
  if (absolute != 0) {
    if (sx_copy_text(out, out_cap, "/") < 0)
      return -1;
    len = 1;
  }
  for (i = 0; i < segment_count; i++) {
    int seg_len = (int)strlen(segments[i]);

    if (len > 0 && out[len - 1] != '/') {
      if (len >= out_cap - 1)
        return -1;
      out[len++] = '/';
      out[len] = '\0';
    }
    if (len + seg_len >= out_cap)
      return -1;
    memcpy(out + len, segments[i], (size_t)seg_len);
    len += seg_len;
    out[len] = '\0';
  }
  if (len == 0)
    return sx_copy_text(out, out_cap, absolute != 0 ? "/" : ".");
  return 0;
}

static int sxi_resolve_import_path(const char *current_path,
                                   const char *import_path,
                                   char *resolved, int resolved_cap)
{
  char dir[PATHNAME_MAX];
  char combined[PATHNAME_MAX];

  if (import_path == 0 || resolved == 0 || resolved_cap <= 0)
    return -1;
  if (import_path[0] == '/')
    return sxi_normalize_path(import_path, resolved, resolved_cap);
  if (current_path == 0)
    return sxi_normalize_path(import_path, resolved, resolved_cap);
  if (import_path[0] != '.') {
    if (sxi_path_dirname(current_path, dir, sizeof(dir)) < 0)
      return -1;
    if (sxi_try_resolve_candidate(dir, import_path,
                                  resolved, resolved_cap) == 0)
      return 0;
    return sxi_resolve_stdlib_path(import_path, resolved, resolved_cap);
  }
  if (sxi_path_dirname(current_path, dir, sizeof(dir)) < 0)
    return -1;
  if (strcmp(dir, ".") == 0)
    return sxi_normalize_path(import_path, resolved, resolved_cap);
  if (strcmp(dir, "/") == 0)
    snprintf(combined, sizeof(combined), "/%s", import_path);
  else
    snprintf(combined, sizeof(combined), "%s/%s", dir, import_path);
  return sxi_normalize_path(combined, resolved, resolved_cap);
}

static int sxi_path_exists(const char *path)
{
  int fd;

  if (path == 0)
    return 0;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static int sxi_has_sx_suffix(const char *path)
{
  int len;

  if (path == 0)
    return 0;
  len = (int)strlen(path);
  return len >= 3 && strcmp(path + len - 3, ".sx") == 0;
}

static int sxi_try_resolve_candidate(const char *base_dir,
                                     const char *import_path,
                                     char *resolved, int resolved_cap)
{
  char candidate[PATHNAME_MAX];

  if (base_dir == 0 || import_path == 0 || resolved == 0 || resolved_cap <= 0)
    return -1;
  if (snprintf(candidate, sizeof(candidate), "%s/%s",
               base_dir, import_path) >= (int)sizeof(candidate))
    return -1;
  if (sxi_normalize_path(candidate, resolved, resolved_cap) < 0)
    return -1;
  if (sxi_path_exists(resolved) != 0)
    return 0;
  if (sxi_has_sx_suffix(import_path) != 0)
    return -1;
  if (snprintf(candidate, sizeof(candidate), "%s/%s.sx",
               base_dir, import_path) >= (int)sizeof(candidate))
    return -1;
  if (sxi_normalize_path(candidate, resolved, resolved_cap) < 0)
    return -1;
  return sxi_path_exists(resolved) != 0 ? 0 : -1;
}

static int sxi_resolve_stdlib_path(const char *import_path,
                                   char *resolved, int resolved_cap)
{
#ifdef TEST_BUILD
  static const char *roots[] = {
    "fixtures/sx/stdlib",
    "tests/fixtures/sx/stdlib",
  };
  int i;

  for (i = 0; i < (int)(sizeof(roots) / sizeof(roots[0])); i++) {
    if (sxi_try_resolve_candidate(roots[i], import_path,
                                  resolved, resolved_cap) == 0)
      return 0;
  }
#endif
  return sxi_try_resolve_candidate(SXI_GUEST_STDLIB_DIR, import_path,
                                   resolved, resolved_cap);
}

static int sxi_loader_has_path(char paths[][PATHNAME_MAX],
                               int count,
                               const char *path)
{
  int i;

  if (path == 0)
    return 0;
  for (i = 0; i < count; i++) {
    if (strcmp(paths[i], path) == 0)
      return 1;
  }
  return 0;
}

static int sxi_parse_import_line(const char *line, int len,
                                 char *import_path, int import_cap)
{
  int i = 0;
  int start;

  if (line == 0 || len < 0 || import_path == 0 || import_cap <= 0)
    return -1;
  import_path[0] = '\0';
  while (i < len && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= len)
    return 0;
  if (i + 1 < len && line[i] == '/' && line[i + 1] == '/')
    return 0;
  if (len - i < 6 || strncmp(line + i, "import", 6) != 0)
    return 0;
  if (i + 6 < len &&
      ((line[i + 6] >= 'a' && line[i + 6] <= 'z') ||
       (line[i + 6] >= 'A' && line[i + 6] <= 'Z') ||
       (line[i + 6] >= '0' && line[i + 6] <= '9') ||
       line[i + 6] == '_'))
    return 0;
  i += 6;
  while (i < len && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= len || line[i] != '"')
    return -1;
  i++;
  start = i;
  while (i < len && line[i] != '"')
    i++;
  if (i >= len || line[i] != '"')
    return -1;
  sx_copy_slice(import_path, import_cap, line + start, i - start);
  i++;
  while (i < len && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= len || line[i] != ';')
    return -1;
  i++;
  while (i < len && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= len)
    return 1;
  if (i + 1 < len && line[i] == '/' && line[i + 1] == '/')
    return 1;
  return -1;
}

static int sxi_collect_source_tree(const char *path,
                                   struct sxi_loader_state *loader,
                                   struct sxi_string_builder *builder,
                                   struct sx_diagnostic *diag,
                                   int *error_kind)
{
  char *text;
  int text_len = 0;
  int pos = 0;
  int line = 1;

  if (path == 0 || loader == 0 || builder == 0 || diag == 0 || error_kind == 0)
    return -1;
  if (sxi_loader_has_path(loader->loaded, loader->loaded_count, path) != 0)
    return 0;
  if (sxi_loader_has_path(loader->stack, loader->stack_count, path) != 0) {
    sx_set_diagnostic(diag, 0, 0, 1, 1, "import cycle detected");
    *error_kind = 2;
    return -1;
  }
  if (loader->stack_count >= SXI_IMPORT_MAX ||
      loader->loaded_count >= SXI_IMPORT_MAX) {
    sx_set_diagnostic(diag, 0, 0, 1, 1, "too many imported files");
    *error_kind = 2;
    return -1;
  }

  text = sxi_read_file_all(path, &text_len);
  if (text == 0) {
    sx_set_diagnostic(diag, 0, 0, 1, 1,
                      loader->stack_count == 0 ?
                          "cannot open source file" :
                          "import file not found");
    *error_kind = loader->stack_count == 0 ? 1 : 2;
    return -1;
  }

  sx_copy_text(loader->stack[loader->stack_count++],
               sizeof(loader->stack[0]), path);
  while (pos < text_len) {
    int line_start = pos;
    int line_end = pos;
    int line_len;
    int has_newline = 0;
    char import_path[PATHNAME_MAX];
    int import_status;

    while (line_end < text_len && text[line_end] != '\n')
      line_end++;
    line_len = line_end - line_start;
    if (line_end < text_len && text[line_end] == '\n')
      has_newline = 1;

    import_status = sxi_parse_import_line(text + line_start, line_len,
                                          import_path, sizeof(import_path));
    if (import_status < 0) {
      sx_set_diagnostic(diag, line_start, line_len, line, 1,
                        "invalid import statement");
      loader->stack_count--;
      free(text);
      *error_kind = 2;
      return -1;
    }
    if (import_status > 0) {
      char resolved[PATHNAME_MAX];

      if (sxi_resolve_import_path(path, import_path,
                                  resolved, sizeof(resolved)) < 0) {
        sx_set_diagnostic(diag, line_start, line_len, line, 1,
                          "import path is too long");
        loader->stack_count--;
        free(text);
        *error_kind = 2;
        return -1;
      }
      if (sxi_loader_has_path(loader->stack, loader->stack_count, resolved) != 0) {
        sx_set_diagnostic(diag, line_start, line_len, line, 1,
                          "import cycle detected");
        loader->stack_count--;
        free(text);
        *error_kind = 2;
        return -1;
      }
      if (sxi_collect_source_tree(resolved, loader, builder,
                                  diag, error_kind) < 0) {
        loader->stack_count--;
        free(text);
        return -1;
      }
    } else {
      if (sxi_builder_append(builder, text + line_start, line_len) < 0 ||
          (has_newline != 0 &&
           sxi_builder_append(builder, "\n", 1) < 0)) {
        sx_set_diagnostic(diag, line_start, line_len, line, 1,
                          "source buffer is full");
        loader->stack_count--;
        free(text);
        *error_kind = 2;
        return -1;
      }
    }

    pos = line_end + (has_newline != 0 ? 1 : 0);
    line++;
  }

  loader->stack_count--;
  sx_copy_text(loader->loaded[loader->loaded_count++],
               sizeof(loader->loaded[0]), path);
  free(text);
  return 0;
}

static char *sxi_read_fd_all(int fd, int *out_len)
{
  char *buf;
  int cap = 512;
  int len = 0;

  if (out_len == 0)
    return 0;
  buf = (char *)malloc((size_t)cap);
  if (buf == 0)
    return 0;

  while (1) {
    int nr;
    char *next;

    if (len >= cap - 1) {
      int next_cap = cap * 2;

      next = (char *)malloc((size_t)next_cap);
      if (next == 0) {
        free(buf);
        return 0;
      }
      memset(next, 0, (size_t)next_cap);
      memcpy(next, buf, (size_t)len);
      free(buf);
      buf = next;
      cap = next_cap;
    }

    nr = (int)read(fd, buf + len, (size_t)(cap - len - 1));
    if (nr <= 0)
      break;
    len += nr;
  }

  buf[len] = '\0';
  *out_len = len;
  return buf;
}

static char *sxi_read_file_all(const char *path, int *out_len)
{
  int fd;
  char *text;

  if (path == 0 || out_len == 0)
    return 0;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return 0;
  text = sxi_read_fd_all(fd, out_len);
  close(fd);
  return text;
}

static int sxi_run_text(struct sx_runtime *runtime,
                        const char *name,
                        const char *text,
                        int check_only)
{
  struct sx_program *program;
  struct sx_runtime *probe;
  struct sx_diagnostic diag;
  int status = 0;

  program = (struct sx_program *)malloc(sizeof(*program));
  if (program == 0) {
    sxi_write_text(STDERR_FILENO, "sxi: out of memory\n");
    return 1;
  }
  probe = (struct sx_runtime *)malloc(sizeof(*probe));
  if (probe == 0) {
    free(program);
    sxi_write_text(STDERR_FILENO, "sxi: out of memory\n");
    SXI_DEBUG_WRITE("AUDIT sxi_malloc_probe_fail\n", 28);
    return 1;
  }

  SXI_DEBUG_PRINTF("AUDIT sxi_run_text name=%s len=%d check=%d\n",
                   name != 0 ? name : "<input>",
                   text != 0 ? (int)strlen(text) : -1,
                   check_only);
  if (sx_parse_program(text, (int)strlen(text), program, &diag) < 0) {
    sxi_print_diagnostic(name, &diag, 0);
    status = 2;
    goto out;
  }
  sx_runtime_init(probe);
  probe->output = runtime->output;
  probe->output_ctx = runtime->output_ctx;
  probe->binding_count = runtime->binding_count;
  probe->scope_depth = runtime->scope_depth;
  memcpy(probe->bindings, runtime->bindings, sizeof(probe->bindings));
  probe->argc = runtime->argc;
  memcpy(probe->argv, runtime->argv, sizeof(probe->argv));
  memcpy(probe->pipes, runtime->pipes, sizeof(probe->pipes));
  memcpy(probe->sockets, runtime->sockets, sizeof(probe->sockets));
  memcpy(probe->lists, runtime->lists, sizeof(probe->lists));
  memcpy(probe->maps, runtime->maps, sizeof(probe->maps));
  memcpy(probe->results, runtime->results, sizeof(probe->results));
  probe->limits.max_bindings = runtime->limits.max_bindings;
  probe->limits.max_scope_depth = runtime->limits.max_scope_depth;
  probe->limits.max_call_depth = runtime->limits.max_call_depth;
  probe->limits.max_loop_iterations = runtime->limits.max_loop_iterations;
  if (sx_runtime_check_program(probe, program, &diag) < 0) {
    sxi_print_diagnostic(name, &diag, probe);
    status = 2;
    goto out;
  }
  if (check_only != 0)
    goto out;
  if (sx_runtime_execute_program(runtime, program, &diag) < 0) {
    sxi_print_diagnostic(name, &diag, runtime);
    status = 3;
    goto out;
  }

out:
  SXI_DEBUG_PRINTF("AUDIT sxi_run_text_done name=%s status=%d\n",
                   name != 0 ? name : "<input>", status);
  free(probe);
  free(program);
  return status;
}

static int sxi_run_file(struct sx_runtime *runtime,
                        const char *path,
                        int check_only)
{
  struct sxi_loader_state *loader;
  int status;

  loader = (struct sxi_loader_state *)malloc(sizeof(*loader));
  if (loader == 0) {
    sxi_write_text(STDERR_FILENO, "sxi: out of memory\n");
    return 1;
  }
  memset(loader, 0, sizeof(*loader));
  status = sxi_run_file_with_loader(runtime, path, check_only, loader);
  free(loader);
  return status;
}

static int sxi_run_file_with_loader(struct sx_runtime *runtime,
                                    const char *path,
                                    int check_only,
                                    struct sxi_loader_state *loader)
{
  struct sxi_string_builder builder;
  struct sx_diagnostic diag;
  char normalized[PATHNAME_MAX];
  int status = 0;
  int error_kind = 2;

  memset(&builder, 0, sizeof(builder));
  sx_clear_diagnostic(&diag);
  if (loader == 0)
    return 1;
  if (sxi_resolve_import_path(0, path, normalized, sizeof(normalized)) < 0) {
    sxi_write_text(STDERR_FILENO, "sxi: source path is too long\n");
    SXI_DEBUG_PRINTF("AUDIT sxi_resolve_path_fail path=%s\n",
                     path != 0 ? path : "(null)");
    return 1;
  }
  SXI_DEBUG_PRINTF("AUDIT sxi_run_file path=%s normalized=%s check=%d\n",
                   path != 0 ? path : "(null)", normalized, check_only);
  if (sxi_builder_init(&builder) < 0) {
    sxi_write_text(STDERR_FILENO, "sxi: out of memory\n");
    SXI_DEBUG_WRITE("AUDIT sxi_builder_init_fail\n", 28);
    return 1;
  }
  if (sxi_collect_source_tree(normalized, loader, &builder,
                              &diag, &error_kind) < 0) {
    sxi_print_diagnostic(normalized, &diag, 0);
    sxi_builder_reset(&builder);
    return error_kind;
  }
  SXI_DEBUG_PRINTF("AUDIT sxi_source_tree_done path=%s size=%d\n",
                   normalized, builder.len);
  status = sxi_run_text(runtime, normalized, builder.data, check_only);
  sxi_builder_reset(&builder);
  return status;
}

static int sxi_trim_line(char *line)
{
  int len;

  if (line == 0)
    return 0;
  len = (int)strlen(line);
  while (len > 0 &&
         (line[len - 1] == ' ' || line[len - 1] == '\t'))
    line[--len] = '\0';
  return len;
}

static int sxi_read_line(char *buf, int cap)
{
  int len = 0;

  if (buf == 0 || cap <= 1)
    return -1;
  while (len < cap - 1) {
    char ch;
    int nr = (int)read(STDIN_FILENO, &ch, 1);

    if (nr < 0)
      return -1;
    if (nr == 0) {
      if (len == 0)
        return 0;
      break;
    }
    if (ch == '\r')
      continue;
    if (ch == '\n')
      break;
    buf[len++] = ch;
  }
  buf[len] = '\0';
  return len;
}

static int sxi_append_line(char *buf, int cap, int *len, const char *line)
{
  int i;

  if (buf == 0 || len == 0 || line == 0 || cap <= 1)
    return -1;
  for (i = 0; line[i] != '\0'; i++) {
    if (*len >= cap - 1)
      return -1;
    buf[(*len)++] = line[i];
  }
  if (*len >= cap - 1)
    return -1;
  buf[(*len)++] = '\n';
  buf[*len] = '\0';
  return 0;
}

static int sxi_repl(void)
{
  struct sx_runtime *runtime;
  char line[SXI_REPL_LINE_MAX];
  char source[SXI_REPL_BUFFER_MAX];
  int source_len = 0;

  runtime = (struct sx_runtime *)malloc(sizeof(*runtime));
  if (runtime == 0) {
    sxi_write_text(STDERR_FILENO, "sxi: out of memory\n");
    return 1;
  }
  sx_runtime_init(runtime);
  source[0] = '\0';
  while (1) {
    int len;

    sxi_write_text(STDOUT_FILENO, source_len == 0 ? "sxi> " : "...> ");
    len = sxi_read_line(line, sizeof(line));
    if (len < 0) {
      sx_runtime_dispose(runtime);
      free(runtime);
      return 1;
    }
    if (len == 0) {
      if (source_len != 0) {
        sxi_write_text(STDERR_FILENO, "sxi: incomplete input\n");
        sx_runtime_dispose(runtime);
        free(runtime);
        return 1;
      }
      sxi_write_text(STDOUT_FILENO, "\n");
      sx_runtime_dispose(runtime);
      free(runtime);
      return 0;
    }
    len = sxi_trim_line(line);
    if (len <= 0)
      continue;

    if (source_len == 0 && line[0] == ':') {
      if (strcmp(line, ":quit") == 0)
        break;
      if (strcmp(line, ":reset") == 0) {
        sx_runtime_reset_session(runtime);
        source_len = 0;
        source[0] = '\0';
        continue;
      }
      if (strcmp(line, ":help") == 0) {
        sxi_print_help();
        continue;
      }
      if (strncmp(line, ":load", 5) == 0) {
        const char *path = line + 5;

        while (*path == ' ' || *path == '\t')
          path++;
        if (*path == '\0') {
          sxi_write_text(STDERR_FILENO, "sxi: missing path\n");
          continue;
        }
        sxi_run_file(runtime, path, 0);
        continue;
      }
      sxi_write_text(STDERR_FILENO, "sxi: unknown repl command\n");
      continue;
    }

    if (sxi_append_line(source, sizeof(source), &source_len, line) < 0) {
      sxi_write_text(STDERR_FILENO, "sxi: repl buffer is full\n");
      source_len = 0;
      source[0] = '\0';
      continue;
    }
    if (sxi_source_needs_more_input(source) != 0)
      continue;
    sxi_run_text(runtime, "<repl>", source, 0);
    source_len = 0;
    source[0] = '\0';
  }
  sx_runtime_dispose(runtime);
  free(runtime);
  return 0;
}
