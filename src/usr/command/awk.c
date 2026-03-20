#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

#define UTT_FIELD_MAX 64
#define UTT_EXPR_MAX 16
#define UTT_STMT_MAX 16
#define UTT_VAR_MAX 16

enum utt_awk_expr_type {
  UTT_AWK_EXPR_STRING = 1,
  UTT_AWK_EXPR_FIELD,
  UTT_AWK_EXPR_NR,
  UTT_AWK_EXPR_NF,
  UTT_AWK_EXPR_VAR
};

enum utt_awk_stmt_kind {
  UTT_AWK_STMT_BEGIN = 1,
  UTT_AWK_STMT_MAIN,
  UTT_AWK_STMT_END
};

struct utt_awk_expr {
  int type;
  int field_index;
  char text[128];
};

struct utt_awk_stmt {
  int kind;
  int has_pattern;
  char pattern[128];
  struct utt_awk_expr exprs[UTT_EXPR_MAX];
  int expr_count;
};

struct utt_awk_var {
  char name[32];
  char value[128];
};

struct utt_awk_program {
  char fs[32];
  struct utt_awk_var vars[UTT_VAR_MAX];
  int var_count;
  struct utt_awk_stmt stmts[UTT_STMT_MAX];
  int stmt_count;
};

struct utt_awk_record {
  char *line;
  int len;
  char *fields[UTT_FIELD_MAX];
  int field_lens[UTT_FIELD_MAX];
  int field_count;
  long nr;
};

static void utt_awk_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: awk [-F fs] [-v var=value]... [-f program_file]... "
                 "[program] [file ...]\n");
}

static int utt_append_program_text(struct utt_string *prog_buf, const char *text)
{
  if (prog_buf == 0 || text == 0)
    return -1;
  if (prog_buf->len > 0 && prog_buf->data[prog_buf->len - 1] != '\n') {
    if (utt_string_append_char(prog_buf, '\n') < 0)
      return -1;
  }
  return utt_string_append_text(prog_buf, text);
}

static int utt_append_program_file(struct utt_string *prog_buf, const char *path)
{
  char *data = 0;
  int len = 0;
  int ret = 0;

  if (utt_read_path_all(path, &data, &len) < 0)
    return -1;
  if (prog_buf->len > 0 && prog_buf->data[prog_buf->len - 1] != '\n') {
    if (utt_string_append_char(prog_buf, '\n') < 0)
      ret = -1;
  }
  if (ret == 0 && utt_string_append_len(prog_buf, data, len) < 0)
    ret = -1;
  free(data);
  return ret;
}

static int utt_awk_add_assignment(struct utt_awk_program *prog, const char *assign)
{
  const char *eq;
  int name_len;

  if (prog == 0 || assign == 0 || prog->var_count >= UTT_VAR_MAX)
    return -1;
  eq = strchr(assign, '=');
  if (eq == 0)
    return -1;
  name_len = (int)(eq - assign);
  if (name_len <= 0 ||
      name_len >= (int)sizeof(prog->vars[prog->var_count].name))
    return -1;
  memcpy(prog->vars[prog->var_count].name, assign, (size_t)name_len);
  prog->vars[prog->var_count].name[name_len] = '\0';
  strncpy(prog->vars[prog->var_count].value, eq + 1,
          sizeof(prog->vars[prog->var_count].value) - 1);
  prog->var_count++;
  return 0;
}

static const char *utt_awk_skip_space(const char *p)
{
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';')
    p++;
  return p;
}

static int utt_awk_parse_expr(const char *token, struct utt_awk_expr *expr)
{
  int len;

  memset(expr, 0, sizeof(*expr));
  len = utt_strlen_int(token);
  while (len > 0 && utt_is_space(token[len - 1]))
    len--;
  while (*token != '\0' && utt_is_space(*token)) {
    token++;
    len--;
  }
  if (len <= 0)
    return 0;
  if ((token[0] == '"' && token[len - 1] == '"') ||
      (token[0] == '\'' && token[len - 1] == '\'')) {
    expr->type = UTT_AWK_EXPR_STRING;
    if (len - 2 >= (int)sizeof(expr->text))
      len = (int)sizeof(expr->text) - 1 + 2;
    memcpy(expr->text, token + 1, (size_t)(len - 2));
    expr->text[len - 2] = '\0';
    return 0;
  }
  if (token[0] == '$' && utt_is_digit(token[1])) {
    expr->type = UTT_AWK_EXPR_FIELD;
    expr->field_index = atoi(token + 1);
    return 0;
  }
  if (len == 2 && token[0] == 'N' && token[1] == 'R') {
    expr->type = UTT_AWK_EXPR_NR;
    return 0;
  }
  if (len == 2 && token[0] == 'N' && token[1] == 'F') {
    expr->type = UTT_AWK_EXPR_NF;
    return 0;
  }
  expr->type = UTT_AWK_EXPR_VAR;
  if (len >= (int)sizeof(expr->text))
    len = (int)sizeof(expr->text) - 1;
  memcpy(expr->text, token, (size_t)len);
  expr->text[len] = '\0';
  return 0;
}

static int utt_awk_parse_print_list(const char *body, struct utt_awk_stmt *stmt)
{
  const char *p = body;

  if (strncmp(p, "print", 5) != 0)
    return -1;
  p += 5;
  p = utt_awk_skip_space(p);
  if (*p == '\0')
    return 0;
  while (*p != '\0') {
    char token[128];
    int tlen = 0;
    char quote = '\0';

    while (*p != '\0') {
      if (quote == '\0' && *p == ',')
        break;
      if ((*p == '"' || *p == '\'') && (quote == '\0' || quote == *p))
        quote = (quote == '\0') ? *p : '\0';
      if (tlen < (int)sizeof(token) - 1)
        token[tlen++] = *p;
      p++;
    }
    token[tlen] = '\0';
    if (stmt->expr_count >= UTT_EXPR_MAX)
      return -1;
    if (utt_awk_parse_expr(token, &stmt->exprs[stmt->expr_count]) < 0)
      return -1;
    stmt->expr_count++;
    if (*p == ',')
      p++;
    p = utt_awk_skip_space(p);
  }
  return 0;
}

static int utt_awk_parse_program(const char *text, struct utt_awk_program *prog)
{
  const char *p = text;

  while (*(p = utt_awk_skip_space(p)) != '\0') {
    struct utt_awk_stmt stmt;
    const char *body_start;
    const char *body_end;
    int body_len;

    if (prog->stmt_count >= UTT_STMT_MAX)
      return -1;
    memset(&stmt, 0, sizeof(stmt));
    if (strncmp(p, "BEGIN", 5) == 0 && !utt_is_alnum(p[5])) {
      stmt.kind = UTT_AWK_STMT_BEGIN;
      p += 5;
    } else if (strncmp(p, "END", 3) == 0 && !utt_is_alnum(p[3])) {
      stmt.kind = UTT_AWK_STMT_END;
      p += 3;
    } else {
      stmt.kind = UTT_AWK_STMT_MAIN;
      if (*p == '/') {
        int len = 0;

        p++;
        while (*p != '\0' && *p != '/' && len < (int)sizeof(stmt.pattern) - 1)
          stmt.pattern[len++] = *p++;
        stmt.pattern[len] = '\0';
        if (*p != '/')
          return -1;
        stmt.has_pattern = 1;
        p++;
      }
    }

    p = utt_awk_skip_space(p);
    if (*p != '{')
      return -1;
    p++;
    body_start = p;
    while (*p != '\0' && *p != '}')
      p++;
    if (*p != '}')
      return -1;
    body_end = p;
    body_len = (int)(body_end - body_start);
    {
      char *body = utt_strdup_len(body_start, body_len);
      int ret;

      if (body == 0)
        return -1;
      ret = utt_awk_parse_print_list(utt_awk_skip_space(body), &stmt);
      free(body);
      if (ret < 0)
        return -1;
    }
    prog->stmts[prog->stmt_count++] = stmt;
    p++;
  }
  return 0;
}

static const char *utt_awk_lookup_var(const struct utt_awk_program *prog,
                                      const char *name)
{
  int i;

  for (i = 0; i < prog->var_count; i++) {
    if (strcmp(prog->vars[i].name, name) == 0)
      return prog->vars[i].value;
  }
  return "";
}

static void utt_awk_split_fields(struct utt_awk_program *prog,
                                 struct utt_awk_record *record)
{
  int pos = 0;
  int fs_len = utt_strlen_int(prog->fs);

  record->field_count = 0;
  if (fs_len == 0 || (fs_len == 1 && prog->fs[0] == ' ')) {
    while (pos < record->len) {
      int start;

      while (pos < record->len && utt_is_space(record->line[pos]))
        pos++;
      if (pos >= record->len)
        break;
      start = pos;
      while (pos < record->len && !utt_is_space(record->line[pos]))
        pos++;
      if (record->field_count < UTT_FIELD_MAX) {
        record->fields[record->field_count] = record->line + start;
        record->field_lens[record->field_count] = pos - start;
        record->field_count++;
      }
    }
    return;
  }

  while (pos <= record->len) {
    int next = pos;

    while (next + fs_len <= record->len &&
           memcmp(record->line + next, prog->fs, (size_t)fs_len) != 0)
      next++;
    if (record->field_count < UTT_FIELD_MAX) {
      record->fields[record->field_count] = record->line + pos;
      if (next + fs_len <= record->len)
        record->field_lens[record->field_count] = next - pos;
      else
        record->field_lens[record->field_count] = record->len - pos;
      record->field_count++;
    }
    if (next + fs_len > record->len)
      break;
    pos = next + fs_len;
  }
}

static int utt_awk_stmt_match(const struct utt_awk_stmt *stmt,
                              const struct utt_awk_record *record)
{
  if (stmt->kind != UTT_AWK_STMT_MAIN)
    return 0;
  if (stmt->has_pattern == 0)
    return 1;
  return utt_regex_match(stmt->pattern, record->line, 0);
}

static void utt_awk_emit_expr(const struct utt_awk_program *prog,
                              const struct utt_awk_record *record,
                              const struct utt_awk_expr *expr)
{
  char buf[32];
  int len = 0;

  if (expr->type == UTT_AWK_EXPR_STRING) {
    utt_write_text(STDOUT_FILENO, expr->text);
  } else if (expr->type == UTT_AWK_EXPR_FIELD) {
    int index = expr->field_index - 1;

    if (index >= 0 && index < record->field_count)
      utt_write_raw(STDOUT_FILENO,
                    record->fields[index],
                    record->field_lens[index]);
  } else if (expr->type == UTT_AWK_EXPR_NR) {
    len = utt_format_long(buf, sizeof(buf), record->nr);
    utt_write_raw(STDOUT_FILENO, buf, len);
  } else if (expr->type == UTT_AWK_EXPR_NF) {
    len = snprintf(buf, sizeof(buf), "%d", record->field_count);
    utt_write_raw(STDOUT_FILENO, buf, len);
  } else if (expr->type == UTT_AWK_EXPR_VAR) {
    utt_write_text(STDOUT_FILENO, utt_awk_lookup_var(prog, expr->text));
  }
}

static void utt_awk_run_stmt(const struct utt_awk_program *prog,
                             const struct utt_awk_stmt *stmt,
                             const struct utt_awk_record *record)
{
  int i;

  if (stmt->expr_count == 0) {
    utt_write_raw(STDOUT_FILENO, record->line, record->len);
    utt_write_text(STDOUT_FILENO, "\n");
    return;
  }
  for (i = 0; i < stmt->expr_count; i++) {
    if (i > 0)
      utt_write_text(STDOUT_FILENO, " ");
    utt_awk_emit_expr(prog, record, &stmt->exprs[i]);
  }
  utt_write_text(STDOUT_FILENO, "\n");
}

int unix_awk_main(int argc, char **argv)
{
  struct utt_awk_program prog;
  struct utt_string program_buf;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  int i;
  const char *program_text = 0;
  long nr = 0;

  memset(&prog, 0, sizeof(prog));
  utt_string_init(&program_buf);
  strcpy(prog.fs, " ");
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (utt_is_help_option(argv[i])) {
      utt_awk_print_usage();
      utt_string_free(&program_buf);
      return 0;
    } else if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
      strncpy(prog.fs, argv[++i], sizeof(prog.fs) - 1);
    } else if (strncmp(argv[i], "-F", 2) == 0 && argv[i][2] != '\0') {
      strncpy(prog.fs, argv[i] + 2, sizeof(prog.fs) - 1);
    } else if (utt_match_long_option(argv[i], "field-separator", &value)) {
      const char *fs = value != 0 ? value : argv[++i];
      strncpy(prog.fs, fs, sizeof(prog.fs) - 1);
    } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
      if (utt_awk_add_assignment(&prog, argv[++i]) < 0) {
        utt_string_free(&program_buf);
        return utt_print_error("awk", "bad -v", argv[i]);
      }
    } else if (utt_match_long_option(argv[i], "assign", &value)) {
      const char *assign = value != 0 ? value : argv[++i];

      if (utt_awk_add_assignment(&prog, assign) < 0) {
        utt_string_free(&program_buf);
        return utt_print_error("awk", "bad -v", assign);
      }
    } else if ((strcmp(argv[i], "-f") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "file", &value)) {
      const char *path = value != 0 ? value : argv[++i];

      if (utt_append_program_file(&program_buf, path) < 0) {
        utt_string_free(&program_buf);
        return utt_print_error("awk", "bad program", path);
      }
    } else if (argv[i][0] == '-') {
      utt_string_free(&program_buf);
      return utt_print_error("awk", "unsupported option", argv[i]);
    } else {
      if (program_buf.len == 0) {
        if (utt_append_program_text(&program_buf, argv[i++]) < 0) {
          utt_string_free(&program_buf);
          return utt_print_error("awk", "out of memory", "");
        }
      }
      break;
    }
  }

  if (program_buf.len > 0)
    program_text = program_buf.data;
  if (program_text == 0) {
    utt_string_free(&program_buf);
    return utt_print_error("awk", "missing program", "");
  }
  if (utt_awk_parse_program(program_text, &prog) < 0) {
    utt_string_free(&program_buf);
    return utt_print_error("awk", "bad program", program_text);
  }
  if (utt_collect_input_texts(argv + i, argc - i, &texts, &text_count) < 0) {
    utt_string_free(&program_buf);
    return utt_print_error("awk", "read failed", "");
  }

  for (i = 0; i < prog.stmt_count; i++) {
    struct utt_awk_record empty;

    if (prog.stmts[i].kind != UTT_AWK_STMT_BEGIN)
      continue;
    memset(&empty, 0, sizeof(empty));
    utt_awk_run_stmt(&prog, &prog.stmts[i], &empty);
  }

  for (i = 0; i < text_count; i++) {
    int j;

    for (j = 0; j < texts[i].line_count; j++) {
      struct utt_awk_record record;
      int stmt_index;

      memset(&record, 0, sizeof(record));
      record.line = texts[i].lines[j].text;
      record.len = texts[i].lines[j].len;
      record.nr = ++nr;
      utt_awk_split_fields(&prog, &record);

      for (stmt_index = 0; stmt_index < prog.stmt_count; stmt_index++) {
        if (utt_awk_stmt_match(&prog.stmts[stmt_index], &record))
          utt_awk_run_stmt(&prog, &prog.stmts[stmt_index], &record);
      }
    }
  }

  for (i = 0; i < prog.stmt_count; i++) {
    struct utt_awk_record endrec;

    if (prog.stmts[i].kind != UTT_AWK_STMT_END)
      continue;
    memset(&endrec, 0, sizeof(endrec));
    endrec.nr = nr;
    utt_awk_run_stmt(&prog, &prog.stmts[i], &endrec);
  }

  utt_free_texts(texts, text_count);
  utt_string_free(&program_buf);
  return 0;
}

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_awk_main(argc, argv);
}
#endif
