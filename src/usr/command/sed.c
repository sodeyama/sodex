#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

struct utt_sed_command {
  int addr_line;
  int addr_last;
  char type;
  char *find;
  char *replace;
  int global;
};

struct utt_sed_program {
  int suppress_default;
  struct utt_sed_command *commands;
  int command_count;
};

static void utt_sed_free_program(struct utt_sed_program *prog)
{
  int i;

  if (prog == 0)
    return;
  for (i = 0; i < prog->command_count; i++) {
    if (prog->commands[i].find != 0)
      free(prog->commands[i].find);
    if (prog->commands[i].replace != 0)
      free(prog->commands[i].replace);
  }
  free(prog->commands);
  memset(prog, 0, sizeof(*prog));
}

static int utt_sed_add_command(struct utt_sed_program *prog,
                               const struct utt_sed_command *cmd)
{
  struct utt_sed_command *next;

  next = (struct utt_sed_command *)malloc(sizeof(*next) * (size_t)(prog->command_count + 1));
  if (next == 0)
    return -1;
  if (prog->commands != 0) {
    memcpy(next, prog->commands, sizeof(*next) * (size_t)prog->command_count);
    free(prog->commands);
  }
  next[prog->command_count] = *cmd;
  prog->commands = next;
  prog->command_count++;
  return 0;
}

static const char *utt_sed_skip_ws(const char *p)
{
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}

static int utt_sed_parse_piece(const char *piece, struct utt_sed_program *prog)
{
  struct utt_sed_command cmd;
  const char *p = utt_sed_skip_ws(piece);
  const char *start;
  const char *end;
  char delim;

  memset(&cmd, 0, sizeof(cmd));
  if (*p == '\0')
    return 0;
  if (utt_is_digit(*p)) {
    start = p;
    while (utt_is_digit(*p))
      p++;
    cmd.addr_line = (int)utt_parse_long_substr(start, (int)(p - start));
  } else if (*p == '$') {
    cmd.addr_last = 1;
    p++;
  }
  p = utt_sed_skip_ws(p);
  if (*p == 'p' || *p == 'd' || *p == 'q') {
    cmd.type = *p++;
    p = utt_sed_skip_ws(p);
    if (*p != '\0')
      return -1;
    return utt_sed_add_command(prog, &cmd);
  }
  if (*p != 's')
    return -1;
  cmd.type = 's';
  p++;
  delim = *p++;
  start = p;
  while (*p != '\0' && *p != delim) {
    if (*p == '\\' && p[1] != '\0')
      p += 2;
    else
      p++;
  }
  if (*p != delim)
    return -1;
  end = p;
  cmd.find = utt_strdup_len(start, (int)(end - start));
  p++;
  start = p;
  while (*p != '\0' && *p != delim) {
    if (*p == '\\' && p[1] != '\0')
      p += 2;
    else
      p++;
  }
  if (*p != delim) {
    if (cmd.find != 0)
      free(cmd.find);
    return -1;
  }
  end = p;
  cmd.replace = utt_strdup_len(start, (int)(end - start));
  p++;
  while (*p != '\0') {
    if (*p == 'g')
      cmd.global = 1;
    else if (*p != ' ' && *p != '\t')
      return -1;
    p++;
  }
  return utt_sed_add_command(prog, &cmd);
}

static int utt_sed_parse_script(const char *script, struct utt_sed_program *prog)
{
  const char *p = script;
  const char *start = script;
  char quote = '\0';

  while (*p != '\0') {
    if (quote == '\0' && (*p == ';' || *p == '\n')) {
      char *piece = utt_strdup_len(start, (int)(p - start));
      int ret;

      if (piece == 0)
        return -1;
      ret = utt_sed_parse_piece(piece, prog);
      free(piece);
      if (ret < 0)
        return -1;
      p++;
      start = p;
      continue;
    }
    if (*p == '\\' && p[1] != '\0') {
      p += 2;
      continue;
    }
    if (*p == '\'' || *p == '"') {
      if (quote == *p)
        quote = '\0';
      else if (quote == '\0')
        quote = *p;
    }
    p++;
  }
  if (p > start) {
    char *piece = utt_strdup_len(start, (int)(p - start));
    int ret;

    if (piece == 0)
      return -1;
    ret = utt_sed_parse_piece(piece, prog);
    free(piece);
    if (ret < 0)
      return -1;
  }
  return 0;
}

static int utt_sed_add_script_file(const char *path,
                                   struct utt_sed_program *prog)
{
  char *data = 0;
  int len = 0;
  int ret;

  if (utt_read_path_all(path, &data, &len) < 0)
    return -1;
  ret = utt_sed_parse_script(data, prog);
  free(data);
  return ret;
}

static int utt_sed_addr_match(const struct utt_sed_command *cmd,
                              int line_no,
                              int is_last)
{
  if (cmd->addr_line > 0)
    return line_no == cmd->addr_line;
  if (cmd->addr_last != 0)
    return is_last != 0;
  return 1;
}

static int utt_sed_substitute(struct utt_string *line,
                              const struct utt_sed_command *cmd)
{
  struct utt_string out;
  int pos = 0;
  int copy_from = 0;
  int found = 0;
  int find_len = utt_strlen_int(cmd->find);
  int repl_len = utt_strlen_int(cmd->replace);
  char *orig_data = line->data;
  int orig_len = line->len;

  if (find_len <= 0)
    return 0;
  utt_string_init(&out);
  while (pos <= orig_len - find_len) {
    if (memcmp(orig_data + pos, cmd->find, (size_t)find_len) == 0) {
      utt_string_append_len(&out, orig_data + copy_from, pos - copy_from);
      utt_string_append_len(&out, cmd->replace, repl_len);
      pos += find_len;
      copy_from = pos;
      found = 1;
      if (cmd->global == 0)
        break;
      continue;
    }
    if (found != 0 && cmd->global == 0)
      break;
    pos++;
  }
  if (found == 0) {
    utt_string_free(&out);
    return 0;
  }
  utt_string_append_len(&out, orig_data + copy_from, orig_len - copy_from);
  free(orig_data);
  *line = out;
  return 1;
}

int unix_sed_main(int argc, char **argv)
{
  struct utt_sed_program prog;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  int i;

  memset(&prog, 0, sizeof(prog));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if (strcmp(argv[i], "-n") == 0 ||
               utt_match_long_option(argv[i], "quiet", 0) ||
               utt_match_long_option(argv[i], "silent", 0)) {
      prog.suppress_default = 1;
    } else if ((strcmp(argv[i], "-e") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "expression", &value)) {
      const char *script = value != 0 ? value : argv[++i];

      if (utt_sed_parse_script(script, &prog) < 0) {
        utt_sed_free_program(&prog);
        return utt_print_error("sed", "bad script", script);
      }
    } else if ((strcmp(argv[i], "-f") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "file", &value)) {
      const char *path = value != 0 ? value : argv[++i];

      if (utt_sed_add_script_file(path, &prog) < 0) {
        utt_sed_free_program(&prog);
        return utt_print_error("sed", "bad script", path);
      }
    } else if (argv[i][0] == '-') {
      utt_sed_free_program(&prog);
      return utt_print_error("sed", "unsupported option", argv[i]);
    } else {
      if (prog.command_count == 0) {
        if (utt_sed_parse_script(argv[i], &prog) < 0) {
          utt_sed_free_program(&prog);
          return utt_print_error("sed", "bad script", argv[i]);
        }
        i++;
      }
      break;
    }
  }

  if (prog.command_count == 0)
    return utt_print_error("sed", "missing script", "");
  if (utt_collect_input_texts(argv + i, argc - i, &texts, &text_count) < 0) {
    utt_sed_free_program(&prog);
    return utt_print_error("sed", "read failed", "");
  }

  for (i = 0; i < text_count; i++) {
    int j;

    for (j = 0; j < texts[i].line_count; j++) {
      struct utt_string line;
      int cmd_index;
      int deleted = 0;
      int quit_after = 0;

      utt_string_init(&line);
      utt_string_append_len(&line, texts[i].lines[j].text, texts[i].lines[j].len);
      for (cmd_index = 0; cmd_index < prog.command_count; cmd_index++) {
        struct utt_sed_command *cmd = &prog.commands[cmd_index];

        if (!utt_sed_addr_match(cmd, j + 1, j == texts[i].line_count - 1))
          continue;
        if (cmd->type == 's')
          utt_sed_substitute(&line, cmd);
        else if (cmd->type == 'p') {
          utt_write_raw(STDOUT_FILENO, line.data, line.len);
          utt_write_text(STDOUT_FILENO, "\n");
        } else if (cmd->type == 'd') {
          deleted = 1;
          break;
        } else if (cmd->type == 'q') {
          quit_after = 1;
          break;
        }
      }
      if (deleted == 0 && prog.suppress_default == 0) {
        utt_write_raw(STDOUT_FILENO, line.data, line.len);
        utt_write_text(STDOUT_FILENO, "\n");
      }
      utt_string_free(&line);
      if (quit_after != 0) {
        utt_free_texts(texts, text_count);
        utt_sed_free_program(&prog);
        return 0;
      }
    }
  }

  utt_free_texts(texts, text_count);
  utt_sed_free_program(&prog);
  return 0;
}

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_sed_main(argc, argv);
}
#endif
