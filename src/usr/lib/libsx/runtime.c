#include <sx_runtime.h>
#include <json.h>
#include <string.h>

#ifdef TEST_BUILD
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <stdlib.h>
#include <fs.h>
#endif

#define SX_WHILE_LIMIT 1024

enum sx_flow_kind {
  SX_FLOW_NEXT = 0,
  SX_FLOW_RETURN = 1
};

#ifndef TEST_BUILD
struct sx_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};
#endif

static int sx_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int sx_format_i32(char *buf, int cap, int value)
{
  char temp[16];
  unsigned int magnitude;
  int len = 0;
  int pos = 0;

  if (buf == 0 || cap <= 1)
    return -1;
  if (value < 0)
    magnitude = (unsigned int)(-value);
  else
    magnitude = (unsigned int)value;
  do {
    temp[len++] = (char)('0' + (magnitude % 10));
    magnitude /= 10;
  } while (magnitude > 0 && len < (int)sizeof(temp));
  if (value < 0)
    temp[len++] = '-';
  if (len >= cap)
    return -1;
  while (len > 0)
    buf[pos++] = temp[--len];
  buf[pos] = '\0';
  return pos;
}

static void sx_set_string_value(struct sx_value *value, const char *text)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_STRING;
  value->bool_value = 0;
  value->int_value = 0;
  sx_copy_text(value->text, sizeof(value->text), text);
}

static int sx_default_output(void *ctx, const char *text, int len)
{
  int written;

  (void)ctx;
  written = (int)write(STDOUT_FILENO, text, (size_t)len);
  return written == len ? 0 : -1;
}

static void sx_set_unit_value(struct sx_value *value)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_NONE;
  value->text[0] = '\0';
  value->bool_value = 0;
  value->int_value = 0;
}

static void sx_set_bool_value(struct sx_value *value, int bool_value)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_BOOL;
  value->bool_value = bool_value != 0;
  value->int_value = value->bool_value;
  sx_copy_text(value->text, sizeof(value->text),
               bool_value != 0 ? "true" : "false");
}

static void sx_set_i32_value(struct sx_value *value, int int_value)
{
  if (value == 0)
    return;
  value->kind = SX_VALUE_I32;
  value->bool_value = int_value != 0;
  value->int_value = int_value;
  if (sx_format_i32(value->text, sizeof(value->text), int_value) < 0)
    value->text[0] = '\0';
}

static int sx_find_binding(const struct sx_runtime *runtime, const char *name)
{
  int i;

  for (i = runtime->binding_count - 1; i >= 0; i--) {
    if (strcmp(runtime->bindings[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int sx_find_binding_in_current_scope(const struct sx_runtime *runtime,
                                            const char *name)
{
  int i;

  for (i = runtime->binding_count - 1; i >= 0; i--) {
    if (runtime->bindings[i].scope_depth != runtime->scope_depth)
      continue;
    if (strcmp(runtime->bindings[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int sx_find_function(const struct sx_program *program, const char *name)
{
  int i;

  for (i = 0; i < program->function_count; i++) {
    if (strcmp(program->functions[i].name, name) == 0)
      return i;
  }
  return -1;
}

static void sx_snapshot_error_stack(struct sx_runtime *runtime)
{
  int i;

  if (runtime == 0)
    return;
  if (runtime->error_call_depth >= runtime->call_depth)
    return;
  runtime->error_call_depth = runtime->call_depth;
  for (i = 0; i < runtime->call_depth && i < SX_MAX_CALL_DEPTH; i++) {
    sx_copy_text(runtime->error_call_stack[i],
                 sizeof(runtime->error_call_stack[0]),
                 runtime->call_stack[i]);
  }
}

static int sx_value_to_bool(const struct sx_value *value,
                            const struct sx_source_span *span,
                            struct sx_diagnostic *diag)
{
  if (value->kind == SX_VALUE_BOOL)
    return value->bool_value;
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected bool value");
  return -1;
}

static int sx_value_to_string(const struct sx_value *value,
                              const struct sx_source_span *span,
                              struct sx_diagnostic *diag)
{
  if (value->kind == SX_VALUE_STRING)
    return 0;
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected string value");
  return -1;
}

static int sx_value_to_i32(const struct sx_value *value,
                           const struct sx_source_span *span,
                           struct sx_diagnostic *diag,
                           int *out_value)
{
  if (value->kind == SX_VALUE_I32) {
    if (out_value != 0)
      *out_value = value->int_value;
    return 0;
  }
  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "expected i32 value");
  return -1;
}

static int sx_read_text_file(const char *path, char *buf, int cap)
{
  int fd;
  int len = 0;

  if (path == 0 || buf == 0 || cap <= 1)
    return -1;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  while (len < cap - 1) {
    int nr = (int)read(fd, buf + len, (size_t)(cap - len - 1));

    if (nr < 0) {
      close(fd);
      return -1;
    }
    if (nr == 0)
      break;
    len += nr;
  }
  buf[len] = '\0';
  close(fd);
  return len;
}

static int sx_write_text_file(const char *path, const char *text, int append)
{
  int fd;
  int flags;
  int len;
  int written = 0;

  if (path == 0 || text == 0)
    return -1;
  flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  fd = open(path, flags, 0644);
  if (fd < 0)
    return -1;
  len = (int)strlen(text);
  while (written < len) {
    int nr = (int)write(fd, text + written, (size_t)(len - written));

    if (nr <= 0) {
      close(fd);
      return -1;
    }
    written += nr;
  }
  close(fd);
  return 0;
}

static int sx_path_exists(const char *path)
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

static int sx_list_dir_text(const char *path, char *buf, int cap)
{
  int len = 0;

  if (path == 0 || buf == 0 || cap <= 1)
    return -1;
#ifdef TEST_BUILD
  {
    DIR *dirp;
    struct dirent *de;

    dirp = opendir(path);
    if (dirp == 0)
      return -1;

    while ((de = readdir(dirp)) != 0) {
      int name_len;
      int i;

      if ((strcmp(de->d_name, ".") == 0) ||
          (strcmp(de->d_name, "..") == 0))
        continue;
      name_len = (int)strlen(de->d_name);
      if (len > 0) {
        if (len >= cap - 1) {
          closedir(dirp);
          return -1;
        }
        buf[len++] = '\n';
      }
      if (len + name_len >= cap) {
        closedir(dirp);
        return -1;
      }
      for (i = 0; i < name_len; i++)
        buf[len++] = de->d_name[i];
    }
    buf[len] = '\0';
    closedir(dirp);
    return len;
  }
#else
  {
    int fd;
    int bytes_read;
    char dir_buf[4096];

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
      return -1;
    while ((bytes_read = (int)read(fd, dir_buf, sizeof(dir_buf))) > 0) {
      int offset = 0;

      while (offset < bytes_read) {
        struct sx_dir_entry *de = (struct sx_dir_entry *)(dir_buf + offset);
        int name_len;
        int i;

        if (de->rec_len < 8 || offset + de->rec_len > bytes_read)
          break;
        if (de->inode == 0 || de->name_len == 0) {
          offset += de->rec_len;
          continue;
        }
        name_len = (int)de->name_len;
        if ((name_len == 1 && de->name[0] == '.') ||
            (name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
          offset += de->rec_len;
          continue;
        }
        if (len > 0) {
          if (len >= cap - 1) {
            close(fd);
            return -1;
          }
          buf[len++] = '\n';
        }
        if (len + name_len >= cap) {
          close(fd);
          return -1;
        }
        for (i = 0; i < name_len; i++)
          buf[len++] = de->name[i];
        offset += de->rec_len;
      }
    }
    close(fd);
    if (bytes_read < 0)
      return -1;
    buf[len] = '\0';
    return len;
  }
#endif
}

static int sx_text_contains(const char *text, const char *needle)
{
  int i;
  int j;

  if (text == 0 || needle == 0)
    return 0;
  if (needle[0] == '\0')
    return 1;
  for (i = 0; text[i] != '\0'; i++) {
    for (j = 0; needle[j] != '\0'; j++) {
      if (text[i + j] == '\0' || text[i + j] != needle[j])
        break;
    }
    if (needle[j] == '\0')
      return 1;
  }
  return 0;
}

static int sx_trim_text(const char *text, char *buf, int cap)
{
  int start = 0;
  int end;
  int len;
  int i;

  if (text == 0 || buf == 0 || cap <= 0)
    return -1;
  end = (int)strlen(text);
  while (text[start] != '\0' && sx_is_space(text[start]) != 0)
    start++;
  while (end > start && sx_is_space(text[end - 1]) != 0)
    end--;
  len = end - start;
  if (len >= cap)
    return -1;
  for (i = 0; i < len; i++)
    buf[i] = text[start + i];
  buf[len] = '\0';
  return len;
}

static int sx_concat_text(const char *lhs, const char *rhs, char *buf, int cap)
{
  int len = 0;
  int i;

  if (lhs == 0 || rhs == 0 || buf == 0 || cap <= 0)
    return -1;
  for (i = 0; lhs[i] != '\0'; i++) {
    if (len >= cap - 1)
      return -1;
    buf[len++] = lhs[i];
  }
  for (i = 0; rhs[i] != '\0'; i++) {
    if (len >= cap - 1)
      return -1;
    buf[len++] = rhs[i];
  }
  buf[len] = '\0';
  return len;
}

static int sx_parse_json(const char *text,
                         struct json_token *tokens,
                         int *token_count)
{
  struct json_parser parser;
  int status;

  if (text == 0 || tokens == 0 || token_count == 0)
    return -1;
  json_init(&parser);
  status = json_parse(&parser, text, (int)strlen(text), tokens, JSON_MAX_TOKENS);
  if (status < 0)
    return status;
  *token_count = status;
  return 0;
}

static int sx_json_find_value(const char *text,
                              const struct json_token *tokens,
                              int token_count,
                              const char *key)
{
  if (token_count <= 0 || tokens[0].type != JSON_OBJECT)
    return -1;
  return json_find_key(text, tokens, token_count, 0, key);
}

static int sx_capture_process_output(int fd, char *buf, int cap)
{
  int len = 0;

  if (fd < 0 || buf == 0 || cap <= 1)
    return -1;
  while (len < cap - 1) {
    int nr = (int)read(fd, buf + len, (size_t)(cap - len - 1));

    if (nr < 0)
      return -1;
    if (nr == 0)
      break;
    len += nr;
  }
  buf[len] = '\0';
  return len;
}

static int sx_run_process(const struct sx_value *args,
                          int arg_count,
                          int capture_output,
                          int execute_side_effects,
                          struct sx_value *value)
{
  char *argv[SX_CALL_MAX_ARGS + 1];
  int i;
  int status = 0;

  if (value == 0 || arg_count <= 0)
    return -1;
  if (execute_side_effects == 0) {
    if (capture_output != 0)
      sx_set_string_value(value, "");
    else
      sx_set_i32_value(value, 0);
    return 0;
  }
  for (i = 0; i < arg_count; i++)
    argv[i] = (char *)args[i].text;
  argv[arg_count] = 0;

  if (capture_output != 0) {
    int pipefd[2];
    int saved_stdout;
    pid_t pid;

    if (pipe(pipefd) < 0)
      return -1;
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      return -1;
    }
    close(STDOUT_FILENO);
    if (dup(pipefd[1]) != STDOUT_FILENO) {
      close(saved_stdout);
      close(pipefd[0]);
      close(pipefd[1]);
      return -1;
    }
    close(pipefd[1]);
    pid = execve(args[0].text, argv, 0);
    close(STDOUT_FILENO);
    dup(saved_stdout);
    close(saved_stdout);
    if (pid < 0) {
      close(pipefd[0]);
      return -1;
    }
    if (waitpid(pid, &status, 0) < 0) {
      close(pipefd[0]);
      return -1;
    }
    if (sx_capture_process_output(pipefd[0], value->text,
                                  sizeof(value->text)) < 0) {
      close(pipefd[0]);
      return -1;
    }
    close(pipefd[0]);
    value->kind = SX_VALUE_STRING;
    value->bool_value = 0;
    value->int_value = 0;
    return 0;
  }

  {
    pid_t pid = execve(args[0].text, argv, 0);

    if (pid < 0)
      return -1;
    if (waitpid(pid, &status, 0) < 0)
      return -1;
    sx_set_i32_value(value, status);
    return 0;
  }
}

static int sx_enter_scope(struct sx_runtime *runtime,
                          const struct sx_source_span *span,
                          struct sx_diagnostic *diag)
{
  if (runtime->scope_depth + 1 >= SX_MAX_SCOPE_DEPTH) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "scope depth limit exceeded");
    return -1;
  }
  runtime->scope_depth++;
  return 0;
}

static void sx_leave_scope(struct sx_runtime *runtime)
{
  if (runtime->scope_depth <= 0)
    return;
  while (runtime->binding_count > 0 &&
         runtime->bindings[runtime->binding_count - 1].scope_depth ==
             runtime->scope_depth)
    runtime->binding_count--;
  runtime->scope_depth--;
}

static int sx_eval_atom(struct sx_runtime *runtime,
                        const struct sx_atom *atom,
                        struct sx_value *value,
                        struct sx_diagnostic *diag)
{
  int index;

  if (atom->kind == SX_ATOM_STRING) {
    sx_set_string_value(value, atom->text);
    return 0;
  }
  if (atom->kind == SX_ATOM_BOOL) {
    sx_set_bool_value(value, atom->bool_value);
    return 0;
  }
  if (atom->kind == SX_ATOM_I32) {
    sx_set_i32_value(value, atom->int_value);
    return 0;
  }
  if (atom->kind != SX_ATOM_NAME) {
    sx_set_diagnostic(diag, atom->span.offset, atom->span.length,
                      atom->span.line, atom->span.column,
                      "unsupported atom");
    return -1;
  }

  index = sx_find_binding(runtime, atom->text);
  if (index < 0) {
    sx_set_diagnostic(diag, atom->span.offset, atom->span.length,
                      atom->span.line, atom->span.column,
                      "undefined name");
    return -1;
  }
  *value = runtime->bindings[index].value;
  return 0;
}

static int sx_validate_functions(const struct sx_program *program,
                                 struct sx_diagnostic *diag)
{
  int i;
  int j;

  for (i = 0; i < program->function_count; i++) {
    for (j = i + 1; j < program->function_count; j++) {
      if (strcmp(program->functions[i].name, program->functions[j].name) == 0) {
        sx_set_diagnostic(diag,
                          program->functions[j].span.offset,
                          program->functions[j].span.length,
                          program->functions[j].span.line,
                          program->functions[j].span.column,
                          "duplicate function");
        return -1;
      }
    }
  }
  return 0;
}

static int sx_register_binding(struct sx_runtime *runtime,
                               const char *name,
                               const struct sx_value *value,
                               const struct sx_source_span *span,
                               struct sx_diagnostic *diag)
{
  int index;

  index = sx_find_binding_in_current_scope(runtime, name);
  if (index >= 0) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "duplicate binding");
    return -1;
  }
  if (runtime->binding_count >= SX_MAX_BINDINGS) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "binding table is full");
    return -1;
  }
  index = runtime->binding_count++;
  sx_copy_text(runtime->bindings[index].name,
               sizeof(runtime->bindings[index].name), name);
  runtime->bindings[index].value = *value;
  runtime->bindings[index].scope_depth = runtime->scope_depth;
  return 0;
}

static int sx_eval_expr(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        const struct sx_expr *expr,
                        struct sx_value *value,
                        int execute_side_effects,
                        struct sx_diagnostic *diag);

static int sx_eval_expr_from_index(struct sx_runtime *runtime,
                                   const struct sx_program *program,
                                   int expr_index,
                                   struct sx_value *value,
                                   int execute_side_effects,
                                   struct sx_diagnostic *diag)
{
  if (expr_index < 0 || expr_index >= program->expr_count) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid expression");
    return -1;
  }
  return sx_eval_expr(runtime, program, &program->exprs[expr_index],
                      value, execute_side_effects, diag);
}

static int sx_run_block(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        int block_index,
                        int execute_calls,
                        int create_scope,
                        struct sx_value *value,
                        struct sx_diagnostic *diag);

static int sx_call_user_function(struct sx_runtime *runtime,
                                 const struct sx_program *program,
                                 const struct sx_call_expr *call,
                                 const struct sx_source_span *span,
                                 int execute_side_effects,
                                 struct sx_value *value,
                                 struct sx_diagnostic *diag)
{
  struct sx_value args[SX_CALL_MAX_ARGS];
  struct sx_value result;
  const struct sx_function *fn;
  int function_index;
  int i;
  int flow;
  int entered_scope = 0;

  memset(args, 0, sizeof(args));
  sx_set_unit_value(&result);

  function_index = sx_find_function(program, call->target_name);
  if (function_index < 0) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "unknown function");
    return -1;
  }
  fn = &program->functions[function_index];
  if (call->arg_count != fn->param_count) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "function argument count mismatch");
    return -1;
  }
  if (runtime->call_depth >= SX_MAX_CALL_DEPTH) {
    sx_set_diagnostic(diag, span->offset, span->length,
                      span->line, span->column,
                      "call depth limit exceeded");
    return -1;
  }
  for (i = 0; i < call->arg_count; i++) {
    if (sx_eval_expr_from_index(runtime, program, call->args[i],
                                &args[i], execute_side_effects, diag) < 0)
      return -1;
  }

  runtime->call_depth++;
  sx_copy_text(runtime->call_stack[runtime->call_depth - 1],
               sizeof(runtime->call_stack[0]), fn->name);
  runtime->inside_function++;
  if (sx_enter_scope(runtime, span, diag) < 0)
    goto fail;
  entered_scope = 1;
  for (i = 0; i < fn->param_count; i++) {
    if (sx_register_binding(runtime, fn->params[i], &args[i], span, diag) < 0)
      goto fail;
  }
  flow = sx_run_block(runtime, program, fn->body_block_index,
                      execute_side_effects, 0, &result, diag);
  if (flow < 0)
    goto fail;

  if (entered_scope != 0)
    sx_leave_scope(runtime);
  runtime->inside_function--;
  runtime->call_stack[runtime->call_depth - 1][0] = '\0';
  runtime->call_depth--;
  if (flow == SX_FLOW_RETURN)
    *value = result;
  else
    sx_set_unit_value(value);
  return 0;

fail:
  sx_snapshot_error_stack(runtime);
  if (entered_scope != 0)
    sx_leave_scope(runtime);
  runtime->inside_function--;
  runtime->call_stack[runtime->call_depth - 1][0] = '\0';
  runtime->call_depth--;
  return -1;
}

static int sx_eval_namespace_call(struct sx_runtime *runtime,
                                  const struct sx_program *program,
                                  const struct sx_call_expr *call,
                                  const struct sx_source_span *span,
                                  int execute_side_effects,
                                  struct sx_value *value,
                                  struct sx_diagnostic *diag)
{
  struct sx_value args[SX_CALL_MAX_ARGS];
  struct json_token tokens[JSON_MAX_TOKENS];
  int i;

  memset(args, 0, sizeof(args));
  memset(tokens, 0, sizeof(tokens));
  for (i = 0; i < call->arg_count; i++) {
    if (sx_eval_expr_from_index(runtime, program, call->args[i],
                                &args[i], execute_side_effects, diag) < 0)
      return -1;
  }

  if (strcmp(call->target_name, "io") == 0) {
    if (call->arg_count != 1) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "io builtin expects exactly 1 argument");
      return -1;
    }
    if (strcmp(call->member_name, "print") == 0 ||
        strcmp(call->member_name, "println") == 0) {
      if (execute_side_effects != 0 &&
          runtime->output(runtime->output_ctx, args[0].text,
                          (int)strlen(args[0].text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "output failed");
        return -1;
      }
      if (execute_side_effects != 0 &&
          strcmp(call->member_name, "println") == 0 &&
          runtime->output(runtime->output_ctx, "\n", 1) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "output failed");
        return -1;
      }
      sx_set_unit_value(value);
      return 0;
    }
  }

  if (strcmp(call->target_name, "fs") == 0) {
    if (strcmp(call->member_name, "exists") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.exists expects 1 string argument");
        return -1;
      }
      sx_set_bool_value(value, sx_path_exists(args[0].text));
      return 0;
    }
    if (strcmp(call->member_name, "read_text") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.read_text expects 1 string argument");
        return -1;
      }
      if (sx_read_text_file(args[0].text, value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.read_text failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      return 0;
    }
    if (strcmp(call->member_name, "list_dir") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.list_dir expects 1 string argument");
        return -1;
      }
      if (sx_list_dir_text(args[0].text, value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.list_dir failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      return 0;
    }
    if (strcmp(call->member_name, "write_text") == 0 ||
        strcmp(call->member_name, "append_text") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.write_text/fs.append_text expects 2 string arguments");
        return -1;
      }
      if (execute_side_effects != 0 &&
          sx_write_text_file(args[0].text, args[1].text,
                             strcmp(call->member_name, "append_text") == 0) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "fs.write_text/fs.append_text failed");
        return -1;
      }
      sx_set_bool_value(value, 1);
      return 0;
    }
  }

  if (strcmp(call->target_name, "text") == 0) {
    if (strcmp(call->member_name, "contains") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.contains expects 2 string arguments");
        return -1;
      }
      sx_set_bool_value(value, sx_text_contains(args[0].text, args[1].text));
      return 0;
    }
    if (strcmp(call->member_name, "trim") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.trim expects 1 string argument");
        return -1;
      }
      if (sx_trim_text(args[0].text, value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.trim failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      return 0;
    }
    if (strcmp(call->member_name, "concat") == 0) {
      if (call->arg_count != 2 ||
          sx_value_to_string(&args[0], span, diag) < 0 ||
          sx_value_to_string(&args[1], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.concat expects 2 string arguments");
        return -1;
      }
      if (sx_concat_text(args[0].text, args[1].text,
                         value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "text.concat failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      return 0;
    }
  }

  if (strcmp(call->target_name, "json") == 0) {
    int token_count = 0;
    int value_index = -1;

    if (strcmp(call->member_name, "valid") == 0) {
      if (call->arg_count != 1 ||
          sx_value_to_string(&args[0], span, diag) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.valid expects 1 string argument");
        return -1;
      }
      sx_set_bool_value(value,
                        sx_parse_json(args[0].text, tokens, &token_count) == 0);
      return 0;
    }

    if (call->arg_count != 2 ||
        sx_value_to_string(&args[0], span, diag) < 0 ||
        sx_value_to_string(&args[1], span, diag) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "json.get_* expects json text and key string");
      return -1;
    }
    if (sx_parse_json(args[0].text, tokens, &token_count) < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "json.parse failed");
      return -1;
    }
    value_index = sx_json_find_value(args[0].text, tokens, token_count, args[1].text);
    if (value_index < 0) {
      sx_set_diagnostic(diag, span->offset, span->length,
                        span->line, span->column,
                        "json key not found");
      return -1;
    }

    if (strcmp(call->member_name, "get_str") == 0) {
      if (json_token_str(args[0].text, &tokens[value_index],
                         value->text, sizeof(value->text)) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.get_str failed");
        return -1;
      }
      value->kind = SX_VALUE_STRING;
      value->bool_value = 0;
      value->int_value = 0;
      return 0;
    }
    if (strcmp(call->member_name, "get_bool") == 0) {
      int bool_value = 0;

      if (json_token_bool(args[0].text, &tokens[value_index], &bool_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.get_bool failed");
        return -1;
      }
      sx_set_bool_value(value, bool_value);
      return 0;
    }
    if (strcmp(call->member_name, "get_i32") == 0) {
      int int_value = 0;

      if (json_token_int(args[0].text, &tokens[value_index], &int_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "json.get_i32 failed");
        return -1;
      }
      sx_set_i32_value(value, int_value);
      return 0;
    }
  }

  if (strcmp(call->target_name, "proc") == 0) {
    if (strcmp(call->member_name, "status_ok") == 0) {
      int status_value = 0;

      if (call->arg_count != 1 ||
          sx_value_to_i32(&args[0], span, diag, &status_value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.status_ok expects 1 i32 argument");
        return -1;
      }
      sx_set_bool_value(value, status_value == 0);
      return 0;
    }
    if (strcmp(call->member_name, "run") == 0 ||
        strcmp(call->member_name, "capture") == 0) {
      for (i = 0; i < call->arg_count; i++) {
        if (sx_value_to_string(&args[i], span, diag) < 0) {
          sx_set_diagnostic(diag, span->offset, span->length,
                            span->line, span->column,
                            "proc.run/proc.capture expects string arguments");
          return -1;
        }
      }
      if (sx_run_process(args, call->arg_count,
                         strcmp(call->member_name, "capture") == 0,
                         execute_side_effects, value) < 0) {
        sx_set_diagnostic(diag, span->offset, span->length,
                          span->line, span->column,
                          "proc.run/proc.capture failed");
        return -1;
      }
      return 0;
    }
  }

  sx_set_diagnostic(diag, span->offset, span->length,
                    span->line, span->column,
                    "unknown builtin");
  return -1;
}

static int sx_eval_call_expr(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             const struct sx_call_expr *call,
                             const struct sx_source_span *span,
                             int execute_side_effects,
                             struct sx_value *value,
                             struct sx_diagnostic *diag)
{
  if (call->target_kind == SX_CALL_TARGET_FUNCTION) {
    return sx_call_user_function(runtime, program, call, span,
                                 execute_side_effects, value, diag);
  }
  return sx_eval_namespace_call(runtime, program, call, span,
                                execute_side_effects, value, diag);
}

static int sx_eval_expr(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        const struct sx_expr *expr,
                        struct sx_value *value,
                        int execute_side_effects,
                        struct sx_diagnostic *diag)
{
  if (expr->kind == SX_EXPR_ATOM)
    return sx_eval_atom(runtime, &expr->data.atom, value, diag);
  if (expr->kind == SX_EXPR_CALL)
    return sx_eval_call_expr(runtime, program, &expr->data.call_expr,
                             &expr->span, execute_side_effects, value, diag);
  sx_set_diagnostic(diag, expr->span.offset, expr->span.length,
                    expr->span.line, expr->span.column,
                    "unsupported expression");
  return -1;
}

static int sx_run_statement(struct sx_runtime *runtime,
                            const struct sx_program *program,
                            const struct sx_stmt *stmt,
                            int execute_calls,
                            struct sx_value *value,
                            struct sx_diagnostic *diag)
{
  sx_set_unit_value(value);

  if (stmt->kind == SX_STMT_LET) {
    if (sx_eval_expr(runtime, program, &stmt->data.let_stmt.value,
                     value, execute_calls, diag) < 0)
      return -1;
    if (sx_register_binding(runtime, stmt->data.let_stmt.name,
                            value, &stmt->span, diag) < 0)
      return -1;
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_CALL) {
    if (sx_eval_call_expr(runtime, program, &stmt->data.call_stmt.call_expr,
                          &stmt->span, execute_calls, value, diag) < 0)
      return -1;
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_BLOCK) {
    return sx_run_block(runtime, program,
                        stmt->data.block_stmt.block_index,
                        execute_calls, 1, value, diag);
  }

  if (stmt->kind == SX_STMT_IF) {
    struct sx_value condition_value;
    int condition_bool;

    sx_set_unit_value(&condition_value);
    if (sx_eval_expr(runtime, program, &stmt->data.if_stmt.condition,
                     &condition_value, execute_calls, diag) < 0)
      return -1;
    condition_bool = sx_value_to_bool(&condition_value,
                                      &stmt->data.if_stmt.condition.span, diag);
    if (condition_bool < 0)
      return -1;
    if (condition_bool != 0) {
      return sx_run_block(runtime, program,
                          stmt->data.if_stmt.then_block_index,
                          execute_calls, 1, value, diag);
    }
    if (stmt->data.if_stmt.else_block_index >= 0) {
      return sx_run_block(runtime, program,
                          stmt->data.if_stmt.else_block_index,
                          execute_calls, 1, value, diag);
    }
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_WHILE) {
    int iterations = 0;

    while (1) {
      struct sx_value condition_value;
      int condition_bool;
      int flow;

      sx_set_unit_value(&condition_value);
      if (sx_eval_expr(runtime, program, &stmt->data.while_stmt.condition,
                       &condition_value, execute_calls, diag) < 0)
        return -1;
      condition_bool = sx_value_to_bool(&condition_value,
                                        &stmt->data.while_stmt.condition.span, diag);
      if (condition_bool < 0)
        return -1;
      if (condition_bool == 0)
        break;
      if (iterations++ >= SX_WHILE_LIMIT) {
        sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                          stmt->span.line, stmt->span.column,
                          "while iteration limit exceeded");
        return -1;
      }
      flow = sx_run_block(runtime, program,
                          stmt->data.while_stmt.body_block_index,
                          execute_calls, 1, value, diag);
      if (flow < 0)
        return -1;
      if (flow == SX_FLOW_RETURN)
        return SX_FLOW_RETURN;
    }
    sx_set_unit_value(value);
    return SX_FLOW_NEXT;
  }

  if (stmt->kind == SX_STMT_RETURN) {
    if (runtime->inside_function <= 0) {
      sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                        stmt->span.line, stmt->span.column,
                        "return outside function");
      return -1;
    }
    if (stmt->data.return_stmt.has_value == 0) {
      sx_set_unit_value(value);
      return SX_FLOW_RETURN;
    }
    if (sx_eval_expr(runtime, program, &stmt->data.return_stmt.value,
                     value, execute_calls, diag) < 0)
      return -1;
    return SX_FLOW_RETURN;
  }

  sx_set_diagnostic(diag, stmt->span.offset, stmt->span.length,
                    stmt->span.line, stmt->span.column,
                    "unsupported statement");
  return -1;
}

static int sx_run_block(struct sx_runtime *runtime,
                        const struct sx_program *program,
                        int block_index,
                        int execute_calls,
                        int create_scope,
                        struct sx_value *value,
                        struct sx_diagnostic *diag)
{
  struct sx_source_span scope_span;
  int stmt_index;
  int flow = SX_FLOW_NEXT;

  memset(&scope_span, 0, sizeof(scope_span));
  if (block_index < 0 || block_index >= program->block_count) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid block");
    return -1;
  }
  if (create_scope != 0 &&
      sx_enter_scope(runtime, &scope_span, diag) < 0)
    return -1;

  stmt_index = program->blocks[block_index].first_stmt_index;
  while (stmt_index >= 0) {
    flow = sx_run_statement(runtime, program,
                            &program->statements[stmt_index],
                            execute_calls, value, diag);
    if (flow < 0) {
      if (create_scope != 0)
        sx_leave_scope(runtime);
      return -1;
    }
    if (flow == SX_FLOW_RETURN) {
      if (create_scope != 0)
        sx_leave_scope(runtime);
      return SX_FLOW_RETURN;
    }
    stmt_index = program->statements[stmt_index].next_stmt_index;
  }

  if (create_scope != 0)
    sx_leave_scope(runtime);
  return SX_FLOW_NEXT;
}

static int sx_run_program(struct sx_runtime *runtime,
                          const struct sx_program *program,
                          int execute_calls,
                          struct sx_diagnostic *diag)
{
  struct sx_value value;
  int flow;

  sx_clear_diagnostic(diag);
  runtime->error_call_depth = 0;
  if (sx_validate_functions(program, diag) < 0)
    return -1;
  sx_set_unit_value(&value);
  flow = sx_run_block(runtime, program, program->top_level_block_index,
                      execute_calls, 0, &value, diag);
  if (flow < 0)
    return -1;
  if (flow == SX_FLOW_RETURN) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "return outside function");
    return -1;
  }
  return 0;
}

int sx_runtime_format_stack_trace(const struct sx_runtime *runtime,
                                  char *buf, int cap)
{
  const char (*stack)[SX_NAME_MAX];
  int depth;
  int len = 0;
  int i;

  if (buf == 0 || cap <= 0)
    return -1;
  buf[0] = '\0';
  if (runtime == 0)
    return 0;
  depth = runtime->error_call_depth > 0 ?
              runtime->error_call_depth : runtime->call_depth;
  stack = runtime->error_call_depth > 0 ?
              runtime->error_call_stack : runtime->call_stack;
  for (i = depth - 1; i >= 0; i--) {
    int name_len = (int)strlen(stack[i]);

    if (name_len <= 0)
      continue;
    if (len + 5 + name_len + 1 >= cap)
      return -1;
    memcpy(buf + len, "  at ", 5);
    len += 5;
    memcpy(buf + len, stack[i], (size_t)name_len);
    len += name_len;
    buf[len++] = '\n';
    buf[len] = '\0';
  }
  return len;
}

void sx_runtime_init(struct sx_runtime *runtime)
{
  if (runtime == 0)
    return;
  memset(runtime, 0, sizeof(*runtime));
  runtime->output = sx_default_output;
  runtime->output_ctx = 0;
}

void sx_runtime_set_output(struct sx_runtime *runtime,
                           sx_output_fn output, void *ctx)
{
  if (runtime == 0)
    return;
  runtime->output = output != 0 ? output : sx_default_output;
  runtime->output_ctx = ctx;
}

int sx_runtime_check_program(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             struct sx_diagnostic *diag)
{
  return sx_run_program(runtime, program, 0, diag);
}

int sx_runtime_execute_program(struct sx_runtime *runtime,
                               const struct sx_program *program,
                               struct sx_diagnostic *diag)
{
  return sx_run_program(runtime, program, 1, diag);
}
