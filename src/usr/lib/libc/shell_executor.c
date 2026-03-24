#include <shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>
#include <debug.h>

#ifndef TEST_BUILD
#include <ext3fs.h>
extern int chdir(char *path);
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifdef TEST_BUILD
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

int debug_write(const char *buf, size_t len)
{
  (void)buf;
  (void)len;
  return 0;
}

int set_foreground_pid(int fd, pid_t pid)
{
  (void)fd;
  (void)pid;
  return 0;
}

pid_t get_foreground_pid(int fd)
{
  (void)fd;
  return 0;
}
#endif

struct shell_expanded_command {
  char *argv[SHELL_MAX_ARGS];
  char argv_storage[SHELL_MAX_ARGS][SHELL_WORD_SIZE];
  int argc;
  char assign_name[SHELL_MAX_ASSIGNMENTS][SHELL_VAR_NAME_MAX];
  char assign_value[SHELL_MAX_ASSIGNMENTS][SHELL_VAR_VALUE_MAX];
  int assignment_count;
  struct shell_redirection redirections[SHELL_MAX_REDIRECTIONS];
  char redirection_storage[SHELL_MAX_REDIRECTIONS][SHELL_WORD_SIZE];
  int redirection_count;
};

enum shell_lookup_kind {
  SHELL_LOOKUP_MISSING = 0,
  SHELL_LOOKUP_ALIAS = 1,
  SHELL_LOOKUP_BUILTIN = 2,
  SHELL_LOOKUP_EXTERNAL = 3
};

#define SHELL_GLOB_DIR_BUF_SIZE 4096

#ifndef TEST_BUILD
struct shell_glob_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};
#endif

static int shell_write_fd_text(int fd, const char *text);
static int shell_write_text(const char *text);
static int shell_write_error_text(const char *text);
static int shell_test_path_exists(const char *path);
static int shell_execute_list(struct shell_state *state,
                              const struct shell_program *program,
                              int list_index);
static int shell_execute_node(struct shell_state *state,
                              const struct shell_program *program,
                              int node_index, int async);

static void shell_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0)
    src = "";

  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static int shell_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int shell_is_name_start(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    return 1;
  if (ch >= 'A' && ch <= 'Z')
    return 1;
  return ch == '_';
}

static int shell_is_name_char(char ch)
{
  if (shell_is_name_start(ch))
    return 1;
  return shell_is_digit(ch);
}

static int shell_parse_assignment(const char *text, char *name, int name_cap,
                                  char *value, int value_cap)
{
  int i;

  if (text == 0 || shell_is_name_start(text[0]) == 0)
    return -1;

  for (i = 1; text[i] != '\0'; i++) {
    if (text[i] == '=')
      break;
    if (shell_is_name_char(text[i]) == 0)
      return -1;
  }
  if (text[i] != '=')
    return -1;

  if (name != 0 && name_cap > 0) {
    int j;
    for (j = 0; j < i && j < name_cap - 1; j++)
      name[j] = text[j];
    name[j] = '\0';
  }
  if (value != 0 && value_cap > 0)
    shell_copy_text(value, value_cap, text + i + 1);
  return 0;
}

static void shell_status_text(int value, char *buf, int cap)
{
  char tmp[16];
  int len = 0;
  int negative = 0;

  if (cap <= 0 || buf == 0)
    return;
  if (value == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  if (value < 0) {
    negative = 1;
    value = -value;
  }
  while (value > 0 && len < (int)sizeof(tmp)) {
    tmp[len++] = (char)('0' + (value % 10));
    value /= 10;
  }
  if (negative && len < (int)sizeof(tmp))
    tmp[len++] = '-';
  if (len >= cap)
    len = cap - 1;
  while (len > 0) {
    *buf++ = tmp[--len];
  }
  *buf = '\0';
}

static const char *shell_param_value(const struct shell_state *state, int index)
{
  if (state == 0)
    return "";
  if (index == 0)
    return state->script_name;
  if (index < 0 || index > state->param_count)
    return "";
  return state->param_storage[index - 1];
}

static void shell_join_params(const struct shell_state *state,
                              char *buf, int cap)
{
  int i;
  int len = 0;

  if (buf == 0 || cap <= 0)
    return;
  buf[0] = '\0';
  if (state == 0)
    return;

  for (i = 0; i < state->param_count; i++) {
    const char *text = state->param_storage[i];
    int j;

    if (i > 0 && len < cap - 1)
      buf[len++] = ' ';
    for (j = 0; text[j] != '\0' && len < cap - 1; j++)
      buf[len++] = text[j];
  }
  buf[len] = '\0';
}

static void shell_append_text(char *dst, int cap, int *len, const char *src)
{
  int i;

  if (dst == 0 || len == 0 || cap <= 0 || src == 0)
    return;

  for (i = 0; src[i] != '\0' && *len < cap - 1; i++)
    dst[(*len)++] = src[i];
  dst[*len] = '\0';
}

static int shell_builtin_name(const char *name)
{
  if (name == 0 || name[0] == '\0')
    return 0;
  if (strcmp(name, "cd") == 0)
    return 1;
  if (strcmp(name, "exit") == 0)
    return 1;
  if (strcmp(name, "export") == 0)
    return 1;
  if (strcmp(name, "set") == 0)
    return 1;
  if (strcmp(name, ".") == 0)
    return 1;
  if (strcmp(name, "wait") == 0)
    return 1;
  if (strcmp(name, "jobs") == 0)
    return 1;
  if (strcmp(name, "fg") == 0)
    return 1;
  if (strcmp(name, "bg") == 0)
    return 1;
  if (strcmp(name, "trap") == 0)
    return 1;
  if (strcmp(name, "break") == 0)
    return 1;
  if (strcmp(name, "continue") == 0)
    return 1;
  if (strcmp(name, "echo") == 0)
    return 1;
  if (strcmp(name, "true") == 0)
    return 1;
  if (strcmp(name, "false") == 0)
    return 1;
  if (strcmp(name, "[") == 0)
    return 1;
  if (strcmp(name, "test") == 0)
    return 1;
  if (strcmp(name, "alias") == 0)
    return 1;
  if (strcmp(name, "unalias") == 0)
    return 1;
  if (strcmp(name, "type") == 0)
    return 1;
  if (strcmp(name, "history") == 0)
    return 1;
  if (strcmp(name, "command") == 0)
    return 1;
  return 0;
}

static int shell_append_command_piece(char *dst, int cap, int *len,
                                      const char *src)
{
  if (dst == 0 || len == 0 || cap <= 0 || src == 0 || src[0] == '\0')
    return 0;
  if (*len > 0) {
    if (*len >= cap - 1)
      return -1;
    dst[(*len)++] = ' ';
  }
  shell_append_text(dst, cap, len, src);
  return 0;
}

static int shell_append_command_redirection(char *dst, int cap, int *len,
                                            const struct shell_redirection *redirection)
{
  char op[8];
  int op_len = 0;

  if (dst == 0 || len == 0 || cap <= 0 || redirection == 0)
    return -1;
  if (*len > 0) {
    if (*len >= cap - 1)
      return -1;
    dst[(*len)++] = ' ';
  }

  if (redirection->type == SHELL_REDIR_INPUT) {
    if (redirection->fd != STDIN_FILENO)
      op[op_len++] = (char)('0' + redirection->fd);
    op[op_len++] = '<';
    op[op_len] = '\0';
    shell_append_text(dst, cap, len, op);
    if (redirection->path == 0)
      return 0;
    shell_append_text(dst, cap, len, redirection->path);
    return 0;
  }

  if (redirection->type == SHELL_REDIR_OUTPUT ||
      redirection->type == SHELL_REDIR_APPEND) {
    if (redirection->fd != STDOUT_FILENO)
      op[op_len++] = (char)('0' + redirection->fd);
    op[op_len++] = '>';
    if (redirection->type == SHELL_REDIR_APPEND)
      op[op_len++] = '>';
    op[op_len] = '\0';
    shell_append_text(dst, cap, len, op);
    if (redirection->path == 0)
      return 0;
    shell_append_text(dst, cap, len, redirection->path);
    return 0;
  }

  if (redirection->fd != STDOUT_FILENO)
    op[op_len++] = (char)('0' + redirection->fd);
  op[op_len++] = '>';
  op[op_len++] = '&';
  op[op_len++] = (char)('0' + redirection->target_fd);
  op[op_len] = '\0';
  shell_append_text(dst, cap, len, op);
  return 0;
}

static int shell_build_command_text(const struct shell_command *command,
                                    const char *first_word,
                                    char *out, int cap)
{
  int len = 0;
  int i;

  if (command == 0 || out == 0 || cap <= 0)
    return -1;
  out[0] = '\0';

  for (i = 0; i < command->assignment_count; i++) {
    if (shell_append_command_piece(out, cap, &len,
                                   command->assignments[i]) < 0)
      return -1;
  }

  if (command->argc > 0) {
    if (shell_append_command_piece(out, cap, &len,
                                   first_word != 0 ? first_word : command->argv[0]) < 0)
      return -1;
  }
  for (i = 1; i < command->argc; i++) {
    if (shell_append_command_piece(out, cap, &len,
                                   command->argv[i]) < 0)
      return -1;
  }
  for (i = 0; i < command->redirection_count; i++) {
    if (shell_append_command_redirection(out, cap, &len,
                                         &command->redirections[i]) < 0)
      return -1;
  }

  out[len] = '\0';
  return 0;
}

static int shell_extract_single_command(const struct shell_program *program,
                                        const struct shell_command **command)
{
  const struct shell_node *node;
  const struct shell_pipeline *pipeline;
  int root_index;

  if (program == 0 || command == 0)
    return -1;

  root_index = program->root_list_index;
  if (root_index < 0 || root_index >= program->list_count)
    return -1;
  if (program->lists[root_index].item_count != 1)
    return -1;

  node = &program->nodes[program->lists[root_index].items[0].node_index];
  if (node->type != SHELL_NODE_PIPELINE)
    return -1;
  pipeline = &program->pipelines[node->data.pipeline_index];
  if (pipeline->command_count != 1)
    return -1;
  *command = &pipeline->commands[0];
  return 0;
}

static int shell_resolve_alias_command_text(const struct shell_state *state,
                                            const struct shell_command *command,
                                            char *out, int cap)
{
  const struct shell_command *current = command;
  struct shell_program *programs[2] = {0, 0};
  const char *alias_value;
  int depth = 0;
  int slot = 0;
  int expanded = 0;

  if (out == 0 || cap <= 0)
    return -1;
  out[0] = '\0';
  if (state == 0 || command == 0 || command->argc <= 0)
    return 0;

  while (current != 0 && current->argc > 0) {
    alias_value = shell_alias_get(state, current->argv[0]);

    if (alias_value == 0 || alias_value[0] == '\0')
      break;
    if (depth >= 8) {
      shell_write_error_text("sh: alias expansion loop\n");
      goto fail;
    }
    if (shell_build_command_text(current, alias_value, out, cap) < 0) {
      shell_write_error_text("sh: alias too large\n");
      goto fail;
    }
    if (programs[slot] == 0) {
      programs[slot] = (struct shell_program *)malloc(sizeof(*programs[slot]));
      if (programs[slot] == 0)
        goto fail;
    }
    if (shell_parse_program(out, (int)strlen(out), programs[slot]) < 0 ||
        shell_extract_single_command(programs[slot], &current) < 0) {
      shell_write_error_text("sh: alias must expand to a simple command\n");
      goto fail;
    }
    expanded = 1;
    slot = 1 - slot;
    depth++;
  }

  if (programs[0] != 0)
    free(programs[0]);
  if (programs[1] != 0)
    free(programs[1]);
  return expanded;

 fail:
  if (programs[0] != 0)
    free(programs[0]);
  if (programs[1] != 0)
    free(programs[1]);
  return -1;
}

static int shell_wildcard_match(const char *pattern, const char *text)
{
  if (pattern == 0 || text == 0)
    return 0;
  while (*pattern != '\0') {
    if (*pattern == '*') {
      pattern++;
      if (*pattern == '\0')
        return 1;
      while (*text != '\0') {
        if (shell_wildcard_match(pattern, text) != 0)
          return 1;
        text++;
      }
      return shell_wildcard_match(pattern, text);
    }
    if (*text == '\0')
      return 0;
    if (*pattern != '?' && *pattern != *text)
      return 0;
    pattern++;
    text++;
  }
  return *text == '\0';
}

static void shell_sort_words(char words[][SHELL_WORD_SIZE], int count)
{
  int i;
  int j;

  for (i = 1; i < count; i++) {
    char tmp[SHELL_WORD_SIZE];

    shell_copy_text(tmp, sizeof(tmp), words[i]);
    j = i - 1;
    while (j >= 0 && strcmp(words[j], tmp) > 0) {
      shell_copy_text(words[j + 1], SHELL_WORD_SIZE, words[j]);
      j--;
    }
    shell_copy_text(words[j + 1], SHELL_WORD_SIZE, tmp);
  }
}

static int shell_word_has_unquoted_glob(const char *raw)
{
  char quote = '\0';
  int i;

  if (raw == 0)
    return 0;

  for (i = 0; raw[i] != '\0'; i++) {
    char ch = raw[i];

    if (quote == '\'') {
      if (ch == '\'')
        quote = '\0';
      continue;
    }
    if (quote == '"') {
      if (ch == '"')
        quote = '\0';
      else if (ch == '\\' && raw[i + 1] != '\0')
        i++;
      continue;
    }
    if (ch == '\'') {
      quote = '\'';
      continue;
    }
    if (ch == '"') {
      quote = '"';
      continue;
    }
    if (ch == '\\' && raw[i + 1] != '\0') {
      i++;
      continue;
    }
    if (ch == '*' || ch == '?')
      return 1;
  }
  return 0;
}

static int shell_expand_glob_pattern(const char *pattern,
                                     char words[][SHELL_WORD_SIZE],
                                     int cap)
{
  char dir_path[SHELL_WORD_SIZE];
  char path_prefix[SHELL_WORD_SIZE];
  const char *slash;
  const char *base_pattern;
  int count = 0;

  if (pattern == 0 || words == 0 || cap <= 0)
    return 0;

  slash = strrchr(pattern, '/');
  if (slash == 0) {
    shell_copy_text(dir_path, sizeof(dir_path), ".");
    path_prefix[0] = '\0';
    base_pattern = pattern;
  } else if (slash == pattern) {
    shell_copy_text(dir_path, sizeof(dir_path), "/");
    shell_copy_text(path_prefix, sizeof(path_prefix), "/");
    base_pattern = slash + 1;
  } else {
    int dir_len = (int)(slash - pattern);

    if (dir_len >= (int)sizeof(dir_path))
      dir_len = sizeof(dir_path) - 1;
    memcpy(dir_path, pattern, (size_t)dir_len);
    dir_path[dir_len] = '\0';
    shell_copy_text(path_prefix, sizeof(path_prefix), dir_path);
    base_pattern = slash + 1;
  }
  if (base_pattern[0] == '\0')
    return 0;

#ifdef TEST_BUILD
  {
    DIR *dirp;
    struct dirent *de;

    dirp = opendir(dir_path);
    if (dirp == 0)
      return 0;
    while ((de = readdir(dirp)) != 0 && count < cap) {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;
      if (base_pattern[0] != '.' && de->d_name[0] == '.')
        continue;
      if (shell_wildcard_match(base_pattern, de->d_name) == 0)
        continue;

      if (path_prefix[0] == '\0') {
        shell_copy_text(words[count], SHELL_WORD_SIZE, de->d_name);
      } else if (strcmp(path_prefix, "/") == 0) {
        int len = 1;

        words[count][0] = '/';
        words[count][1] = '\0';
        shell_append_text(words[count], SHELL_WORD_SIZE, &len, de->d_name);
      } else {
        int len = 0;

        words[count][0] = '\0';
        shell_append_text(words[count], SHELL_WORD_SIZE, &len, path_prefix);
        if (len < SHELL_WORD_SIZE - 1)
          words[count][len++] = '/';
        words[count][len] = '\0';
        shell_append_text(words[count], SHELL_WORD_SIZE, &len, de->d_name);
      }
      count++;
    }
    closedir(dirp);
  }
#else
  {
    int fd;
    char dir_buf[SHELL_GLOB_DIR_BUF_SIZE];
    int bytes_read;
    int offset = 0;

    fd = open(dir_path, O_RDONLY, 0);
    if (fd < 0)
      return 0;
    bytes_read = (int)read(fd, dir_buf, sizeof(dir_buf));
    close(fd);
    if (bytes_read <= 0)
      return 0;

    while (offset < bytes_read && count < cap) {
      struct shell_glob_dir_entry *de =
          (struct shell_glob_dir_entry *)(dir_buf + offset);
      char name[256];
      int name_len;

      if (de->rec_len == 0 || de->rec_len < 8)
        break;
      if (offset + de->rec_len > bytes_read)
        break;
      if (de->inode == 0 || de->name_len == 0) {
        offset += de->rec_len;
        continue;
      }

      name_len = de->name_len;
      if (name_len >= (int)sizeof(name))
        name_len = sizeof(name) - 1;
      memcpy(name, de->name, (size_t)name_len);
      name[name_len] = '\0';

      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        offset += de->rec_len;
        continue;
      }
      if (base_pattern[0] != '.' && name[0] == '.') {
        offset += de->rec_len;
        continue;
      }
      if (shell_wildcard_match(base_pattern, name) == 0) {
        offset += de->rec_len;
        continue;
      }

      if (path_prefix[0] == '\0') {
        shell_copy_text(words[count], SHELL_WORD_SIZE, name);
      } else if (strcmp(path_prefix, "/") == 0) {
        int len = 1;

        words[count][0] = '/';
        words[count][1] = '\0';
        shell_append_text(words[count], SHELL_WORD_SIZE, &len, name);
      } else {
        int len = 0;

        words[count][0] = '\0';
        shell_append_text(words[count], SHELL_WORD_SIZE, &len, path_prefix);
        if (len < SHELL_WORD_SIZE - 1)
          words[count][len++] = '/';
        words[count][len] = '\0';
        shell_append_text(words[count], SHELL_WORD_SIZE, &len, name);
      }
      count++;
      offset += de->rec_len;
    }
  }
#endif
  shell_sort_words(words, count);
  return count;
}

static int shell_resolve_external_path(const struct shell_state *state,
                                       const char *name,
                                       char *out, int cap)
{
  const char *path_env;
  const char *segment;

  if (out == 0 || cap <= 0)
    return 0;
  out[0] = '\0';
  if (name == 0 || name[0] == '\0')
    return 0;

  if (strchr(name, '/') != 0) {
    if (shell_test_path_exists(name) == 0)
      return 0;
    shell_copy_text(out, cap, name);
    return 1;
  }

  path_env = shell_var_get(state, "PATH");
  if (path_env == 0 || path_env[0] == '\0')
    path_env = "/usr/bin";

  segment = path_env;
  while (1) {
    char candidate[SHELL_WORD_SIZE];
    int len = 0;
    int seg_len = 0;

    while (segment[seg_len] != '\0' && segment[seg_len] != ':')
      seg_len++;
    candidate[0] = '\0';
    if (seg_len <= 0) {
      shell_append_text(candidate, sizeof(candidate), &len, ".");
    } else if (seg_len == 1 && segment[0] == '/') {
      candidate[0] = '/';
      candidate[1] = '\0';
      len = 1;
    } else {
      int copy_len = seg_len;

      if (copy_len >= (int)sizeof(candidate))
        copy_len = sizeof(candidate) - 1;
      memcpy(candidate, segment, (size_t)copy_len);
      candidate[copy_len] = '\0';
      len = copy_len;
    }
    if (len > 0 && candidate[len - 1] != '/' && len < (int)sizeof(candidate) - 1) {
      candidate[len++] = '/';
      candidate[len] = '\0';
    }
    shell_append_text(candidate, sizeof(candidate), &len, name);
    if (shell_test_path_exists(candidate) != 0) {
      shell_copy_text(out, cap, candidate);
      return 1;
    }
    if (segment[seg_len] == '\0')
      break;
    segment += seg_len + 1;
  }
  return 0;
}

static enum shell_lookup_kind shell_lookup_command(const struct shell_state *state,
                                                   const char *name,
                                                   const char **alias_value,
                                                   char *path, int path_cap)
{
  if (alias_value != 0)
    *alias_value = shell_alias_get(state, name);
  if (alias_value != 0 && *alias_value != 0)
    return SHELL_LOOKUP_ALIAS;
  if (shell_builtin_name(name) != 0)
    return SHELL_LOOKUP_BUILTIN;
  if (shell_resolve_external_path(state, name, path, path_cap) != 0)
    return SHELL_LOOKUP_EXTERNAL;
  return SHELL_LOOKUP_MISSING;
}

static int shell_expand_word(const struct shell_state *state, const char *raw,
                             char *out, int cap)
{
  int len = 0;
  char quote = '\0';
  int i = 0;

  if (raw == 0 || out == 0 || cap <= 0)
    return -1;

  out[0] = '\0';
  if (raw[0] == '~' && (raw[1] == '\0' || raw[1] == '/')) {
    shell_append_text(out, cap, &len, shell_var_get(state, "HOME"));
    i = 1;
  }
  for (; raw[i] != '\0'; i++) {
    char ch = raw[i];

    if (quote == '\'') {
      if (ch == '\'') {
        quote = '\0';
        continue;
      }
      if (len >= cap - 1)
        return -1;
      out[len++] = ch;
      out[len] = '\0';
      continue;
    }

    if (ch == '\'' && quote == '\0') {
      quote = '\'';
      continue;
    }
    if (ch == '"' && quote == '\0') {
      quote = '"';
      continue;
    }
    if (ch == '"' && quote == '"') {
      quote = '\0';
      continue;
    }
    if (ch == '\\') {
      char next = raw[i + 1];
      if (next == '\0')
        continue;
      if (quote == '"' && next != '"' && next != '$' && next != '\\') {
        if (len >= cap - 1)
          return -1;
        out[len++] = ch;
        out[len] = '\0';
        continue;
      }
      if (len >= cap - 1)
        return -1;
      out[len++] = next;
      out[len] = '\0';
      i++;
      continue;
    }
    if (ch == '$') {
      char value[SHELL_VAR_VALUE_MAX];
      int j = i + 1;

      value[0] = '\0';
      if (raw[j] == '?') {
        shell_status_text(state->last_status, value, sizeof(value));
        i = j;
      } else if (raw[j] == '!') {
        shell_status_text((int)state->last_background_pid,
                          value, sizeof(value));
        i = j;
      } else if (raw[j] == '@' || raw[j] == '*') {
        shell_join_params(state, value, sizeof(value));
        i = j;
      } else if (shell_is_digit(raw[j])) {
        int index = 0;
        while (shell_is_digit(raw[j])) {
          index = index * 10 + (raw[j] - '0');
          j++;
        }
        shell_copy_text(value, sizeof(value), shell_param_value(state, index));
        i = j - 1;
      } else if (shell_is_name_start(raw[j])) {
        char name[SHELL_VAR_NAME_MAX];
        int name_len = 0;

        while (shell_is_name_char(raw[j]) && name_len < SHELL_VAR_NAME_MAX - 1)
          name[name_len++] = raw[j++];
        name[name_len] = '\0';
        shell_copy_text(value, sizeof(value), shell_var_get(state, name));
        i = j - 1;
      } else {
        value[0] = '$';
        value[1] = '\0';
      }

      shell_append_text(out, cap, &len, value);
      continue;
    }
    if (len >= cap - 1)
      return -1;
    out[len++] = ch;
    out[len] = '\0';
  }

  if (quote != '\0')
    return -1;
  return 0;
}

static int shell_expand_command(const struct shell_state *state,
                                const struct shell_command *command,
                                struct shell_expanded_command *out)
{
  const struct shell_command *resolved_command = command;
  struct shell_program *alias_program = 0;
  char alias_text[SHELL_STORAGE_SIZE];
  struct shell_redirection *redirection;
  int i;

  if (command == 0 || out == 0)
    return -1;
  i = shell_resolve_alias_command_text(state, command,
                                       alias_text, sizeof(alias_text));
  if (i < 0)
    return -1;
  if (i > 0) {
    alias_program = (struct shell_program *)malloc(sizeof(*alias_program));
    if (alias_program == 0)
      return -1;
    if (shell_parse_program(alias_text, (int)strlen(alias_text), alias_program) < 0 ||
        shell_extract_single_command(alias_program, &resolved_command) < 0) {
      free(alias_program);
      shell_write_error_text("sh: alias must expand to a simple command\n");
      return -1;
    }
  }

  memset(out, 0, sizeof(*out));
  for (i = 0; i < resolved_command->assignment_count; i++) {
    char text[SHELL_WORD_SIZE];

    if (i >= SHELL_MAX_ASSIGNMENTS)
      goto fail;
    if (shell_expand_word(state, resolved_command->assignments[i],
                          text, sizeof(text)) < 0)
      goto fail;
    if (shell_parse_assignment(text,
                               out->assign_name[i], sizeof(out->assign_name[i]),
                               out->assign_value[i], sizeof(out->assign_value[i])) < 0)
      goto fail;
    out->assignment_count++;
  }

  for (i = 0; i < resolved_command->argc; i++) {
    char text[SHELL_WORD_SIZE];
    int match_count = 0;
    int j;

    if (shell_expand_word(state, resolved_command->argv[i],
                          text, sizeof(text)) < 0)
      goto fail;
    if (shell_word_has_unquoted_glob(resolved_command->argv[i]) != 0) {
      match_count = shell_expand_glob_pattern(text, out->argv_storage + out->argc,
                                              SHELL_MAX_ARGS - 1 - out->argc);
    }
    if (match_count > 0) {
      for (j = 0; j < match_count; j++) {
        out->argv[out->argc] = out->argv_storage[out->argc];
        out->argc++;
      }
      continue;
    }
    if (out->argc >= SHELL_MAX_ARGS - 1)
      goto fail;
    shell_copy_text(out->argv_storage[out->argc],
                    sizeof(out->argv_storage[out->argc]), text);
    out->argv[out->argc] = out->argv_storage[out->argc];
    out->argc++;
  }
  out->argv[out->argc] = 0;

  for (i = 0; i < resolved_command->redirection_count; i++) {
    if (i >= SHELL_MAX_REDIRECTIONS)
      goto fail;
    redirection = &out->redirections[i];
    redirection->type = resolved_command->redirections[i].type;
    redirection->fd = resolved_command->redirections[i].fd;
    redirection->target_fd = resolved_command->redirections[i].target_fd;
    redirection->path = 0;
    if (resolved_command->redirections[i].path != 0) {
      if (shell_expand_word(state, resolved_command->redirections[i].path,
                            out->redirection_storage[i],
                            sizeof(out->redirection_storage[i])) < 0)
        goto fail;
      redirection->path = out->redirection_storage[i];
    }
    out->redirection_count++;
  }

  if (alias_program != 0)
    free(alias_program);
  return 0;

 fail:
  if (alias_program != 0)
    free(alias_program);
  return -1;
}

static int shell_save_fd_once(int target_fd, int *saved_fds)
{
  if (saved_fds == 0 || target_fd < 0 || target_fd > STDERR_FILENO)
    return -1;
  if (saved_fds[target_fd] >= 0)
    return 0;
  saved_fds[target_fd] = dup(target_fd);
  if (saved_fds[target_fd] < 0)
    return -1;
  return 0;
}

static int shell_dup_to_target(int source_fd, int target_fd)
{
  int new_fd;

  if (source_fd == target_fd)
    return 0;
  close(target_fd);
  new_fd = dup(source_fd);
  if (new_fd != target_fd) {
    if (new_fd >= 0)
      close(new_fd);
    return -1;
  }
  return 0;
}

static void shell_append_job_text(char *dst, int cap, int *len, const char *src)
{
  int i;

  if (dst == 0 || len == 0 || cap <= 0 || src == 0)
    return;

  for (i = 0; src[i] != '\0' && *len < cap - 1; i++)
    dst[(*len)++] = src[i];
  dst[*len] = '\0';
}

static void shell_append_number(char *dst, int cap, int *len, int value)
{
  char buf[16];

  shell_status_text(value, buf, sizeof(buf));
  shell_append_job_text(dst, cap, len, buf);
}

static void shell_describe_redirection(char *dst, int cap, int *len,
                                       struct shell_redirection *redirection)
{
  if (redirection == 0)
    return;

  if (*len > 0 && *len < cap - 1)
    dst[(*len)++] = ' ';

  if (redirection->type == SHELL_REDIR_INPUT) {
    if (redirection->fd != STDIN_FILENO)
      shell_append_number(dst, cap, len, redirection->fd);
    shell_append_job_text(dst, cap, len, "< ");
    shell_append_job_text(dst, cap, len, redirection->path);
    return;
  }

  if (redirection->type == SHELL_REDIR_OUTPUT ||
      redirection->type == SHELL_REDIR_APPEND) {
    if (redirection->fd != STDOUT_FILENO)
      shell_append_number(dst, cap, len, redirection->fd);
    if (redirection->type == SHELL_REDIR_APPEND)
      shell_append_job_text(dst, cap, len, ">> ");
    else
      shell_append_job_text(dst, cap, len, "> ");
    shell_append_job_text(dst, cap, len, redirection->path);
    return;
  }

  if (redirection->fd != STDOUT_FILENO)
    shell_append_number(dst, cap, len, redirection->fd);
  shell_append_job_text(dst, cap, len, ">&");
  shell_append_number(dst, cap, len, redirection->target_fd);
}

static void shell_describe_command(char *dst, int cap, int *len,
                                   struct shell_expanded_command *command,
                                   int add_pipe)
{
  int i;

  if (command == 0)
    return;

  for (i = 0; i < command->argc; i++) {
    if (*len > 0 && *len < cap - 1)
      dst[(*len)++] = ' ';
    shell_append_job_text(dst, cap, len, command->argv[i]);
  }
  for (i = 0; i < command->redirection_count; i++)
    shell_describe_redirection(dst, cap, len, &command->redirections[i]);
  if (add_pipe != 0)
    shell_append_job_text(dst, cap, len, " |");
  dst[*len] = '\0';
}

static void shell_bg_remove(struct shell_state *state, pid_t pid)
{
  int i;

  if (state == 0 || pid < 0)
    return;
  for (i = 0; i < state->background_count; i++) {
    if (state->background_pids[i] != pid)
      continue;
    for (; i + 1 < state->background_count; i++)
      state->background_pids[i] = state->background_pids[i + 1];
    state->background_count--;
    break;
  }
  for (i = 0; i < state->job_count; i++) {
    if (state->jobs[i].pid != pid)
      continue;
    for (; i + 1 < state->job_count; i++)
      state->jobs[i] = state->jobs[i + 1];
    state->job_count--;
    break;
  }
  if (state->background_count > 0)
    state->last_background_pid = state->background_pids[state->background_count - 1];
  else
    state->last_background_pid = 0;
}

static int shell_job_find_pid(const struct shell_state *state, pid_t pid)
{
  int i;

  if (state == 0 || pid < 0)
    return -1;
  for (i = 0; i < state->job_count; i++) {
    if (state->jobs[i].pid == pid)
      return i;
  }
  return -1;
}

static int shell_job_find_id(const struct shell_state *state, int id)
{
  int i;

  if (state == 0 || id <= 0)
    return -1;
  for (i = 0; i < state->job_count; i++) {
    if (state->jobs[i].id == id)
      return i;
  }
  return -1;
}

static void shell_bg_add(struct shell_state *state, pid_t pid, const char *command)
{
  int job_id = 0;
  char buf[32];

  if (state == 0 || pid < 0)
    return;
  if (state->background_count < SHELL_MAX_BG_PIDS)
    state->background_pids[state->background_count++] = pid;
  if (state->job_count < SHELL_MAX_BG_PIDS) {
    int index = state->job_count++;

    if (state->next_job_id <= 0)
      state->next_job_id = 1;
    state->jobs[index].id = state->next_job_id++;
    state->jobs[index].pid = pid;
    shell_copy_text(state->jobs[index].command,
                    sizeof(state->jobs[index].command),
                    command != 0 && command[0] != '\0' ? command : "job");
    job_id = state->jobs[index].id;
  }
  state->last_background_pid = pid;
  if (state->interactive != 0 && job_id > 0) {
    shell_write_text("[");
    shell_status_text(job_id, buf, sizeof(buf));
    shell_write_text(buf);
    shell_write_text("] ");
    shell_status_text((int)pid, buf, sizeof(buf));
    shell_write_text(buf);
    shell_write_text("\n");
  }
}

static void shell_write_job(struct shell_job *job)
{
  char buf[32];

  if (job == 0)
    return;

  shell_write_text("[");
  shell_status_text(job->id, buf, sizeof(buf));
  shell_write_text(buf);
  shell_write_text("] ");
  shell_status_text((int)job->pid, buf, sizeof(buf));
  shell_write_text(buf);
  shell_write_text(" ");
  shell_write_text(job->command);
  shell_write_text("\n");
}

static struct shell_job *shell_resolve_job(struct shell_state *state,
                                           struct shell_expanded_command *command)
{
  int index = -1;

  if (state == 0)
    return 0;

  if (command == 0 || command->argc <= 1) {
    if (state->last_background_pid > 0)
      index = shell_job_find_pid(state, state->last_background_pid);
  } else if (command->argv[1][0] == '%') {
    index = shell_job_find_id(state, atoi(command->argv[1] + 1));
  } else {
    index = shell_job_find_pid(state, (pid_t)atoi(command->argv[1]));
  }

  if (index < 0)
    return 0;
  return &state->jobs[index];
}

void shell_reap_background(struct shell_state *state)
{
  int status;
  pid_t pid;

  if (state == 0)
    return;

  while (1) {
    pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0)
      break;
    shell_bg_remove(state, pid);
  }
}

static int shell_write_fd_text(int fd, const char *text)
{
  if (text == 0)
    return 0;
  return (int)write(fd, text, strlen(text));
}

static int shell_write_text(const char *text)
{
  return shell_write_fd_text(STDOUT_FILENO, text);
}

static int shell_write_error_text(const char *text)
{
  return shell_write_fd_text(STDERR_FILENO, text);
}

static void shell_debug_audit(struct shell_expanded_command *command)
{
  char buf[SHELL_WORD_SIZE];
  int len = 0;
  int i;

  if (command == 0 || command->argc <= 1)
    return;
  if (strcmp(command->argv[1], "AUDIT") != 0)
    return;

  for (i = 1; i < command->argc && len < SHELL_WORD_SIZE - 1; i++) {
    int j;
    const char *text = command->argv[i];

    if (i > 1 && len < SHELL_WORD_SIZE - 1)
      buf[len++] = ' ';
    for (j = 0; text[j] != '\0' && len < SHELL_WORD_SIZE - 1; j++)
      buf[len++] = text[j];
  }
  if (len < SHELL_WORD_SIZE - 1)
    buf[len++] = '\n';
  buf[len] = '\0';
  debug_write(buf, (size_t)len);
}

static int shell_builtin_echo(struct shell_expanded_command *command)
{
  int i;

  for (i = 1; i < command->argc; i++) {
    if (i > 1)
      shell_write_text(" ");
    shell_write_text(command->argv[i]);
  }
  shell_write_text("\n");
  shell_debug_audit(command);
  return 0;
}

static int shell_builtin_cd(struct shell_state *state,
                            struct shell_expanded_command *command)
{
  const char *path = "/";

  if (state != 0) {
    const char *home = shell_var_get(state, "HOME");

    if (home != 0 && home[0] != '\0')
      path = home;
  }
  if (command->argc > 1)
    path = command->argv[1];
  if (chdir((char *)path) < 0) {
    shell_write_error_text("sh: cd failed\n");
    return 1;
  }
  return 0;
}

static int shell_builtin_export(struct shell_state *state,
                                struct shell_expanded_command *command)
{
  int i;

  for (i = 1; i < command->argc; i++) {
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VALUE_MAX];

    if (shell_parse_assignment(command->argv[i], name, sizeof(name),
                               value, sizeof(value)) == 0) {
      if (shell_var_set(state, name, value, 1) < 0)
        return 1;
      continue;
    }
    if (shell_var_set(state, command->argv[i],
                      shell_var_get(state, command->argv[i]), 1) < 0)
      return 1;
  }
  return 0;
}

static int shell_builtin_set(struct shell_state *state,
                             struct shell_expanded_command *command)
{
  int i;

  if (command->argc <= 1) {
    for (i = 0; i < state->var_count; i++) {
      shell_write_text(state->vars[i].name);
      shell_write_text("=");
      shell_write_text(state->vars[i].value);
      shell_write_text("\n");
    }
    return 0;
  }

  for (i = 1; i < command->argc; i++) {
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VALUE_MAX];

    if (shell_parse_assignment(command->argv[i], name, sizeof(name),
                               value, sizeof(value)) < 0)
      return 1;
    if (shell_var_set(state, name, value, -1) < 0)
      return 1;
  }
  return 0;
}

static int shell_builtin_wait(struct shell_state *state,
                              struct shell_expanded_command *command)
{
  int status = 0;
  int i;

  if (command->argc <= 1) {
    while (state->background_count > 0) {
      pid_t pid = state->background_pids[0];

      if (waitpid(pid, &status, 0) < 0) {
        shell_bg_remove(state, pid);
        continue;
      }
      shell_bg_remove(state, pid);
    }
    return status;
  }

  for (i = 1; i < command->argc; i++) {
    pid_t pid = (pid_t)atoi(command->argv[i]);

    if (waitpid(pid, &status, 0) < 0)
      return 1;
    shell_bg_remove(state, pid);
  }
  return status;
}

static int shell_builtin_jobs(struct shell_state *state)
{
  int i;

  if (state == 0)
    return 1;
  for (i = 0; i < state->job_count; i++)
    shell_write_job(&state->jobs[i]);
  return 0;
}

static int shell_builtin_fg(struct shell_state *state,
                            struct shell_expanded_command *command)
{
  struct shell_job *job;
  pid_t pid;
  int status = 0;

  job = shell_resolve_job(state, command);
  if (job == 0) {
    shell_write_error_text("sh: fg: job not found\n");
    return 1;
  }

  pid = job->pid;
  if (kill(pid, SIGCONT) < 0) {
    shell_bg_remove(state, pid);
    return 1;
  }
  set_foreground_pid(STDIN_FILENO, pid);
  if (waitpid(pid, &status, 0) < 0)
    status = 1;
  set_foreground_pid(STDIN_FILENO, 0);
  shell_bg_remove(state, pid);
  return status;
}

static int shell_builtin_bg(struct shell_state *state,
                            struct shell_expanded_command *command)
{
  struct shell_job *job;

  job = shell_resolve_job(state, command);
  if (job == 0) {
    shell_write_error_text("sh: bg: job not found\n");
    return 1;
  }
  if (kill(job->pid, SIGCONT) < 0) {
    shell_bg_remove(state, job->pid);
    return 1;
  }
  return 0;
}

static int shell_builtin_dot(struct shell_state *state,
                             struct shell_expanded_command *command)
{
  if (command->argc < 2)
    return 1;
  return shell_execute_file(state, command->argv[1],
                            command->argc - 2, &command->argv[2], 1);
}

static int shell_builtin_exit(struct shell_state *state,
                              struct shell_expanded_command *command)
{
  int status = state->last_status;

  if (command->argc > 1)
    status = atoi(command->argv[1]);
  state->exit_requested = 1;
  state->exit_status = status;
  return status;
}

static void shell_write_alias_line(const char *name, const char *value)
{
  shell_write_text("alias ");
  shell_write_text(name);
  shell_write_text("='");
  shell_write_text(value);
  shell_write_text("'\n");
}

static int shell_builtin_alias(struct shell_state *state,
                               struct shell_expanded_command *command)
{
  int i;
  int status = 0;

  if (state == 0)
    return 1;
  if (command->argc <= 1) {
    for (i = 0; i < state->alias_count; i++)
      shell_write_alias_line(state->aliases[i].name, state->aliases[i].value);
    return 0;
  }

  for (i = 1; i < command->argc; i++) {
    const char *arg = command->argv[i];
    const char *eq = strchr(arg, '=');

    if (eq != 0) {
      char name[SHELL_ALIAS_NAME_MAX];
      char value[SHELL_ALIAS_VALUE_MAX];
      int name_len = (int)(eq - arg);

      if (name_len <= 0 || name_len >= (int)sizeof(name)) {
        shell_write_error_text("sh: alias: invalid name\n");
        status = 1;
        continue;
      }
      memcpy(name, arg, (size_t)name_len);
      name[name_len] = '\0';
      shell_copy_text(value, sizeof(value), eq + 1);
      if (shell_alias_set(state, name, value) < 0) {
        shell_write_error_text("sh: alias: set failed\n");
        status = 1;
      }
      continue;
    }

    arg = shell_alias_get(state, command->argv[i]);
    if (arg == 0) {
      shell_write_error_text("sh: alias: not found\n");
      status = 1;
      continue;
    }
    shell_write_alias_line(command->argv[i], arg);
  }
  return status;
}

static int shell_builtin_unalias(struct shell_state *state,
                                 struct shell_expanded_command *command)
{
  int i;
  int status = 0;

  if (state == 0 || command->argc <= 1)
    return 1;
  if (command->argc == 2 && strcmp(command->argv[1], "-a") == 0) {
    shell_alias_clear(state);
    return 0;
  }

  for (i = 1; i < command->argc; i++) {
    if (shell_alias_unset(state, command->argv[i]) < 0) {
      shell_write_error_text("sh: unalias: not found\n");
      status = 1;
    }
  }
  return status;
}

static int shell_builtin_type(struct shell_state *state,
                              struct shell_expanded_command *command)
{
  int i;
  int status = 0;

  if (command->argc <= 1)
    return 1;

  for (i = 1; i < command->argc; i++) {
    const char *alias_value = 0;
    char path[SHELL_WORD_SIZE];
    enum shell_lookup_kind kind;

    path[0] = '\0';
    kind = shell_lookup_command(state, command->argv[i],
                                &alias_value, path, sizeof(path));
    if (kind == SHELL_LOOKUP_ALIAS) {
      shell_write_text(command->argv[i]);
      shell_write_text(" is alias for ");
      shell_write_text(alias_value);
      shell_write_text("\n");
      continue;
    }
    if (kind == SHELL_LOOKUP_BUILTIN) {
      shell_write_text(command->argv[i]);
      shell_write_text(" is shell builtin\n");
      continue;
    }
    if (kind == SHELL_LOOKUP_EXTERNAL) {
      shell_write_text(command->argv[i]);
      shell_write_text(" is ");
      shell_write_text(path);
      shell_write_text("\n");
      continue;
    }
    shell_write_error_text("sh: type: not found\n");
    status = 1;
  }
  return status;
}

static int shell_builtin_history(struct shell_state *state,
                                 struct shell_expanded_command *command)
{
  int i;

  if (state == 0 || command->argc > 1)
    return 1;

  for (i = 0; i < shell_history_count(state); i++) {
    char number[16];
    const char *entry = shell_history_get(state, i);

    shell_status_text(shell_history_entry_number(state, i),
                      number, sizeof(number));
    shell_write_text(number);
    shell_write_text("  ");
    shell_write_text(entry != 0 ? entry : "");
    shell_write_text("\n");
  }
  return 0;
}

static int shell_builtin_command(struct shell_state *state,
                                 struct shell_expanded_command *command)
{
  int i;
  int status = 0;

  if (command->argc < 3 || strcmp(command->argv[1], "-v") != 0) {
    shell_write_error_text("sh: command: only -v is supported\n");
    return 1;
  }

  for (i = 2; i < command->argc; i++) {
    const char *alias_value = 0;
    char path[SHELL_WORD_SIZE];
    enum shell_lookup_kind kind;

    path[0] = '\0';
    kind = shell_lookup_command(state, command->argv[i],
                                &alias_value, path, sizeof(path));
    if (kind == SHELL_LOOKUP_ALIAS) {
      shell_write_alias_line(command->argv[i], alias_value);
      continue;
    }
    if (kind == SHELL_LOOKUP_BUILTIN) {
      shell_write_text(command->argv[i]);
      shell_write_text("\n");
      continue;
    }
    if (kind == SHELL_LOOKUP_EXTERNAL) {
      shell_write_text(path);
      shell_write_text("\n");
      continue;
    }
    status = 1;
  }
  return status;
}

#ifndef TEST_BUILD
static int shell_build_dentry_path(ext3_dentry *dentry, char *buf, int cap)
{
  int pos;
  int name_len;

  if (dentry == 0 || buf == 0 || cap <= 1)
    return -1;
  if (dentry->d_parent == 0 ||
      (dentry->d_namelen == 1 && dentry->d_name[0] == '/')) {
    buf[0] = '/';
    buf[1] = '\0';
    return 1;
  }

  pos = shell_build_dentry_path(dentry->d_parent, buf, cap);
  if (pos < 0)
    return -1;
  if (pos > 1) {
    if (pos >= cap - 1)
      return -1;
    buf[pos++] = '/';
    buf[pos] = '\0';
  }

  name_len = dentry->d_namelen;
  if (name_len <= 0)
    return pos;
  if (pos + name_len >= cap)
    name_len = cap - pos - 1;
  if (name_len <= 0)
    return -1;

  memcpy(buf + pos, dentry->d_name, (size_t)name_len);
  pos += name_len;
  buf[pos] = '\0';
  return pos;
}
#endif

static int shell_test_path_exists(const char *path)
{
  int fd = open(path, O_RDONLY, 0);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static int test_is_nonempty_string(const char *s)
{
  return s != 0 && s[0] != '\0';
}

#ifdef TEST_BUILD
static int shell_test_dir_exists(const char *path)
{
  struct stat st;

  if (stat(path, &st) < 0)
    return 0;
  return S_ISDIR(st.st_mode);
}
#else
static int shell_test_dir_exists(const char *path)
{
  char cwd[SHELL_WORD_SIZE];
  ext3_dentry *dentry;

  if (path == 0 || path[0] == '\0')
    return 0;
  if (strcmp(path, "/") == 0)
    return 1;

  dentry = getdentry();
  if (dentry == 0 || shell_build_dentry_path(dentry, cwd, sizeof(cwd)) < 0)
    return 0;
  if (chdir((char *)path) != 0)
    return 0;
  chdir(cwd);
  return 1;
}
#endif

static int test_eval_primary(int argc, char **argv, int *pos)
{
  const char *tok;

  if (*pos >= argc)
    return 0;

  tok = argv[*pos];

  /* unary: ! expr */
  if (strcmp(tok, "!") == 0) {
    (*pos)++;
    return !test_eval_primary(argc, argv, pos);
  }

  /* -f FILE */
  if (strcmp(tok, "-f") == 0 || strcmp(tok, "-e") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return shell_test_path_exists(argv[*pos - 1]);
  }

  if (strcmp(tok, "-d") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return shell_test_dir_exists(argv[*pos - 1]);
  }

  if (strcmp(tok, "-n") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_is_nonempty_string(argv[*pos - 1]);
  }

  /* -z STRING */
  if (strcmp(tok, "-z") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 1;
    (*pos)++;
    return !test_is_nonempty_string(argv[*pos - 1]);
  }

  if (*pos + 2 < argc) {
    const char *op = argv[*pos + 1];

    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
      int r = strcmp(argv[*pos], argv[*pos + 2]) == 0;
      *pos += 3;
      return r;
    }
    if (strcmp(op, "!=") == 0) {
      int r = strcmp(argv[*pos], argv[*pos + 2]) != 0;
      *pos += 3;
      return r;
    }
  }

  (*pos)++;
  return test_is_nonempty_string(tok);
}

static int shell_builtin_test(struct shell_expanded_command *command)
{
  int is_bracket;
  int argc;
  int pos;
  int result;

  is_bracket = (strcmp(command->argv[0], "[") == 0);
  argc = command->argc;
  if (is_bracket) {
    if (argc < 2 || strcmp(command->argv[argc - 1], "]") != 0)
      return 2;
    argc--;
  }

  if (argc <= 1)
    return 1;

  pos = 1;
  result = test_eval_primary(argc, command->argv, &pos);
  if (pos != argc)
    return 2;
  return result ? 0 : 1;
}

static int shell_builtin_break(struct shell_state *state,
                               struct shell_expanded_command *command)
{
  if (command->argc > 1) {
    shell_write_error_text("sh: break: too many arguments\n");
    return 1;
  }
  if (state == 0 || state->loop_depth <= 0) {
    shell_write_error_text("sh: break: not in loop\n");
    return 1;
  }
  state->loop_control = SHELL_LOOP_BREAK;
  return 0;
}

static int shell_builtin_continue(struct shell_state *state,
                                  struct shell_expanded_command *command)
{
  if (command->argc > 1) {
    shell_write_error_text("sh: continue: too many arguments\n");
    return 1;
  }
  if (state == 0 || state->loop_depth <= 0) {
    shell_write_error_text("sh: continue: not in loop\n");
    return 1;
  }
  state->loop_control = SHELL_LOOP_CONTINUE;
  return 0;
}

static int shell_apply_assignments(struct shell_state *state,
                                   const struct shell_expanded_command *command)
{
  int i;

  for (i = 0; i < command->assignment_count; i++) {
    if (shell_var_set(state, command->assign_name[i],
                      command->assign_value[i], -1) < 0)
      return 1;
  }
  return 0;
}

static int shell_builtin_run(struct shell_state *state,
                             struct shell_expanded_command *command)
{
  const char *name;

  if (command->argc <= 0 || command->argv[0] == 0)
    return shell_apply_assignments(state, command);

  if (shell_apply_assignments(state, command) != 0)
    return 1;

  name = command->argv[0];
  if (strcmp(name, "cd") == 0)
    return shell_builtin_cd(state, command);
  if (strcmp(name, "exit") == 0)
    return shell_builtin_exit(state, command);
  if (strcmp(name, "export") == 0)
    return shell_builtin_export(state, command);
  if (strcmp(name, "set") == 0)
    return shell_builtin_set(state, command);
  if (strcmp(name, ".") == 0)
    return shell_builtin_dot(state, command);
  if (strcmp(name, "wait") == 0)
    return shell_builtin_wait(state, command);
  if (strcmp(name, "jobs") == 0)
    return shell_builtin_jobs(state);
  if (strcmp(name, "fg") == 0)
    return shell_builtin_fg(state, command);
  if (strcmp(name, "bg") == 0)
    return shell_builtin_bg(state, command);
  if (strcmp(name, "trap") == 0)
    return 0;
  if (strcmp(name, "break") == 0)
    return shell_builtin_break(state, command);
  if (strcmp(name, "continue") == 0)
    return shell_builtin_continue(state, command);
  if (strcmp(name, "echo") == 0)
    return shell_builtin_echo(command);
  if (strcmp(name, "true") == 0)
    return 0;
  if (strcmp(name, "false") == 0)
    return 1;
  if (strcmp(name, "[") == 0 || strcmp(name, "test") == 0)
    return shell_builtin_test(command);
  if (strcmp(name, "alias") == 0)
    return shell_builtin_alias(state, command);
  if (strcmp(name, "unalias") == 0)
    return shell_builtin_unalias(state, command);
  if (strcmp(name, "type") == 0)
    return shell_builtin_type(state, command);
  if (strcmp(name, "history") == 0)
    return shell_builtin_history(state, command);
  if (strcmp(name, "command") == 0)
    return shell_builtin_command(state, command);
  return -1;
}

static pid_t shell_spawn_script_fallback(struct shell_expanded_command *command,
                                         const char *path,
                                         char *const envp[])
{
  char *argv[SHELL_MAX_ARGS + 2];
  int i;

  argv[0] = "sh";
  argv[1] = (char *)(path != 0 ? path : command->argv[0]);
  for (i = 1; i < command->argc && i < SHELL_MAX_ARGS; i++)
    argv[i + 1] = command->argv[i];
  argv[i + 1] = 0;
  return execve("/usr/bin/sh", argv, envp);
}

static int shell_build_envp(struct shell_state *state,
                            char env_storage[SHELL_MAX_VARS][SHELL_VAR_NAME_MAX + SHELL_VAR_VALUE_MAX + 2],
                            char *envp[SHELL_MAX_VARS + 1])
{
  int count = 0;
  int i;

  if (state == 0 || env_storage == 0 || envp == 0)
    return -1;
  for (i = 0; i < state->var_count && count < SHELL_MAX_VARS; i++) {
    int name_len;
    int value_len;

    if (state->vars[i].exported == 0)
      continue;
    name_len = (int)strlen(state->vars[i].name);
    value_len = (int)strlen(state->vars[i].value);
    if (name_len + value_len + 2 >
        SHELL_VAR_NAME_MAX + SHELL_VAR_VALUE_MAX + 2)
      continue;
    memcpy(env_storage[count], state->vars[i].name, (size_t)name_len);
    env_storage[count][name_len] = '=';
    memcpy(env_storage[count] + name_len + 1, state->vars[i].value,
           (size_t)value_len);
    env_storage[count][name_len + value_len + 1] = '\0';
    envp[count] = env_storage[count];
    count++;
  }
  envp[count] = 0;
  return count;
}

static pid_t shell_spawn_external(struct shell_state *state,
                                  struct shell_expanded_command *command)
{
  pid_t pid;
  char path[SHELL_WORD_SIZE];
  char env_storage[SHELL_MAX_VARS][SHELL_VAR_NAME_MAX + SHELL_VAR_VALUE_MAX + 2];
  char *envp[SHELL_MAX_VARS + 1];
  int fd;

  if (shell_resolve_external_path(state, command->argv[0],
                                  path, sizeof(path)) == 0)
    return -1;
  if (shell_build_envp(state, env_storage, envp) < 0)
    return -1;

  pid = execve(path, command->argv, envp);
  if (pid >= 0)
    return pid;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  close(fd);
  return shell_spawn_script_fallback(command, path, envp);
}

static int shell_command_needs_builtin_parent(struct shell_expanded_command *command)
{
  if (command->argc <= 0)
    return 1;
  return shell_builtin_name(command->argv[0]);
}

static void shell_restore_saved_fds(int *saved_fds)
{
  int fd;

  if (saved_fds == 0)
    return;
  for (fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
    if (saved_fds[fd] < 0)
      continue;
    close(fd);
    dup(saved_fds[fd]);
    close(saved_fds[fd]);
    saved_fds[fd] = -1;
  }
}

static int shell_assign_fd(int target_fd, int source_fd, int *saved_fds)
{
  if (shell_save_fd_once(target_fd, saved_fds) < 0)
    return -1;
  return shell_dup_to_target(source_fd, target_fd);
}

static int shell_apply_redirections(struct shell_expanded_command *command,
                                    int *saved_fds)
{
  int i;

  if (command == 0)
    return -1;

  for (i = 0; i < command->redirection_count; i++) {
    struct shell_redirection *redirection = &command->redirections[i];
    int fd;
    int flags;

    if (redirection->fd < STDIN_FILENO || redirection->fd > STDERR_FILENO)
      return -1;
    if (redirection->type == SHELL_REDIR_DUP) {
      if (redirection->target_fd < STDIN_FILENO ||
          redirection->target_fd > STDERR_FILENO)
        return -1;
      if (shell_assign_fd(redirection->fd, redirection->target_fd, saved_fds) < 0)
        return -1;
      continue;
    }

    if (redirection->path == 0)
      return -1;
    if (redirection->type == SHELL_REDIR_INPUT) {
      flags = O_RDONLY;
    } else {
      flags = O_CREAT | O_WRONLY;
      if (redirection->type == SHELL_REDIR_APPEND)
        flags |= O_APPEND;
      else
        flags |= O_TRUNC;
    }

    fd = open(redirection->path, flags, 0644);
    if (fd < 0)
      return -1;
    if (shell_assign_fd(redirection->fd, fd, saved_fds) < 0) {
      close(fd);
      return -1;
    }
    close(fd);
  }

  return 0;
}

static int shell_run_parent_command(struct shell_state *state,
                                    struct shell_expanded_command *command)
{
  int saved_fds[3] = {-1, -1, -1};
  int status;

  if (shell_apply_redirections(command, saved_fds) < 0) {
    shell_restore_saved_fds(saved_fds);
    shell_write_error_text("sh: redirection failed\n");
    return 1;
  }

  status = shell_builtin_run(state, command);
  shell_restore_saved_fds(saved_fds);
  return status;
}

static int shell_wait_pipeline(struct shell_state *state,
                               pid_t *pids, int pid_count)
{
  int i;
  int status = 0;

  (void)state;
  if (pid_count <= 0)
    return 0;

  set_foreground_pid(STDIN_FILENO, pids[pid_count - 1]);
  for (i = 0; i < pid_count; i++) {
    if (waitpid(pids[i], &status, 0) < 0)
      status = 1;
  }
  set_foreground_pid(STDIN_FILENO, 0);
  return status;
}

static int shell_execute_pipeline(struct shell_state *state,
                                  const struct shell_pipeline *pipeline,
                                  int async)
{
  pid_t pids[SHELL_MAX_COMMANDS];
  int pid_count = 0;
  int prev_input_fd = -1;
  char job_text[SHELL_JOB_TEXT_MAX];
  int job_text_len = 0;
  int i;

  memset(job_text, 0, sizeof(job_text));

  for (i = 0; i < pipeline->command_count; i++) {
    struct shell_expanded_command *command;
    int pipefd[2] = {-1, -1};
    int saved_fds[3] = {-1, -1, -1};
    pid_t pid;

    command = (struct shell_expanded_command *)malloc(sizeof(*command));
    if (command == 0)
      return 1;

    if (shell_expand_command(state, &pipeline->commands[i], command) < 0) {
      free(command);
      return 1;
    }
    shell_describe_command(job_text, sizeof(job_text), &job_text_len,
                           command, i + 1 < pipeline->command_count);

    if (pipeline->command_count == 1 &&
        async == 0 &&
        shell_command_needs_builtin_parent(command) != 0) {
      int status = shell_run_parent_command(state, command);

      free(command);
      return status;
    }

    if (command->argc > 0 && shell_command_needs_builtin_parent(command) != 0) {
      shell_write_error_text("sh: builtin in pipeline/background is unsupported\n");
      free(command);
      return 1;
    }

    if (prev_input_fd >= 0) {
      if (shell_assign_fd(STDIN_FILENO, prev_input_fd, saved_fds) < 0) {
        close(prev_input_fd);
        free(command);
        return 1;
      }
      close(prev_input_fd);
      prev_input_fd = -1;
    }

    if (i + 1 < pipeline->command_count) {
      if (pipe(pipefd) < 0) {
        shell_restore_saved_fds(saved_fds);
        free(command);
        return 1;
      }
      if (shell_assign_fd(STDOUT_FILENO, pipefd[1], saved_fds) < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        shell_restore_saved_fds(saved_fds);
        free(command);
        return 1;
      }
      close(pipefd[1]);
    }

    if (shell_apply_redirections(command, saved_fds) < 0) {
      shell_restore_saved_fds(saved_fds);
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      shell_write_error_text("sh: redirection failed\n");
      free(command);
      return 1;
    }

    if (shell_apply_assignments(state, command) != 0) {
      shell_restore_saved_fds(saved_fds);
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      free(command);
      return 1;
    }

    if (command->argc == 0) {
      shell_restore_saved_fds(saved_fds);
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      free(command);
      continue;
    }

    pid = shell_spawn_external(state, command);
    shell_restore_saved_fds(saved_fds);
    if (pid < 0) {
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      shell_write_error_text("sh: command not found\n");
      free(command);
      return 127;
    }

    if (pipefd[0] >= 0)
      prev_input_fd = pipefd[0];
    pids[pid_count++] = pid;
    free(command);
  }

  if (prev_input_fd >= 0)
    close(prev_input_fd);

  if (async != 0) {
    if (pid_count > 0)
      shell_bg_add(state, pids[pid_count - 1], job_text);
    return 0;
  }
  return shell_wait_pipeline(state, pids, pid_count);
}

static int shell_execute_if_node(struct shell_state *state,
                                 const struct shell_program *program,
                                 const struct shell_if_node *if_node)
{
  int status;
  int i;

  status = shell_execute_list(state, program, if_node->cond_list_index);
  if (state->exit_requested != 0 || state->loop_control != SHELL_LOOP_NONE)
    return status;
  if (status == 0)
    return shell_execute_list(state, program, if_node->then_list_index);

  for (i = 0; i < if_node->elif_count; i++) {
    status = shell_execute_list(state, program, if_node->elifs[i].cond_list_index);
    if (state->exit_requested != 0 || state->loop_control != SHELL_LOOP_NONE)
      return status;
    if (status == 0)
      return shell_execute_list(state, program, if_node->elifs[i].body_list_index);
  }

  if (if_node->has_else != 0)
    return shell_execute_list(state, program, if_node->else_list_index);
  return 0;
}

static int shell_execute_for_node(struct shell_state *state,
                                  const struct shell_program *program,
                                  const struct shell_for_node *for_node)
{
  int status = 0;
  int i;

  state->loop_depth++;
  if (for_node->implicit_params != 0) {
    for (i = 0; i < state->param_count; i++) {
      if (shell_var_set(state, for_node->name, state->param_storage[i], -1) < 0) {
        status = 1;
        break;
      }
      status = shell_execute_list(state, program, for_node->body_list_index);
      if (state->exit_requested != 0)
        break;
      if (state->loop_control == SHELL_LOOP_BREAK) {
        state->loop_control = SHELL_LOOP_NONE;
        break;
      }
      if (state->loop_control == SHELL_LOOP_CONTINUE) {
        state->loop_control = SHELL_LOOP_NONE;
        continue;
      }
    }
  } else {
    for (i = 0; i < for_node->word_count; i++) {
      char value[SHELL_WORD_SIZE];

      if (shell_expand_word(state, for_node->words[i], value, sizeof(value)) < 0) {
        status = 1;
        break;
      }
      if (shell_var_set(state, for_node->name, value, -1) < 0) {
        status = 1;
        break;
      }
      status = shell_execute_list(state, program, for_node->body_list_index);
      if (state->exit_requested != 0)
        break;
      if (state->loop_control == SHELL_LOOP_BREAK) {
        state->loop_control = SHELL_LOOP_NONE;
        break;
      }
      if (state->loop_control == SHELL_LOOP_CONTINUE) {
        state->loop_control = SHELL_LOOP_NONE;
        continue;
      }
    }
  }
  state->loop_depth--;
  return status;
}

static int shell_execute_loop_node(struct shell_state *state,
                                   const struct shell_program *program,
                                   const struct shell_loop_node *loop_node,
                                   int is_until)
{
  int status = 0;

  state->loop_depth++;
  while (1) {
    status = shell_execute_list(state, program, loop_node->cond_list_index);
    if (state->exit_requested != 0)
      break;
    if (state->loop_control == SHELL_LOOP_BREAK) {
      state->loop_control = SHELL_LOOP_NONE;
      break;
    }
    if (state->loop_control == SHELL_LOOP_CONTINUE) {
      state->loop_control = SHELL_LOOP_NONE;
      continue;
    }
    if (is_until != 0) {
      if (status == 0)
        break;
    } else if (status != 0) {
      break;
    }

    status = shell_execute_list(state, program, loop_node->body_list_index);
    if (state->exit_requested != 0)
      break;
    if (state->loop_control == SHELL_LOOP_BREAK) {
      state->loop_control = SHELL_LOOP_NONE;
      break;
    }
    if (state->loop_control == SHELL_LOOP_CONTINUE) {
      state->loop_control = SHELL_LOOP_NONE;
      continue;
    }
  }
  state->loop_depth--;
  return status;
}

static int shell_execute_node(struct shell_state *state,
                              const struct shell_program *program,
                              int node_index, int async)
{
  const struct shell_node *node;

  if (program == 0 || node_index < 0 || node_index >= program->node_count)
    return 1;
  node = &program->nodes[node_index];

  if (node->type != SHELL_NODE_PIPELINE && async != 0) {
    shell_write_error_text("sh: background compound is unsupported\n");
    return 1;
  }

  if (node->type == SHELL_NODE_PIPELINE)
    return shell_execute_pipeline(state,
                                  &program->pipelines[node->data.pipeline_index],
                                  async);
  if (node->type == SHELL_NODE_IF)
    return shell_execute_if_node(state, program, &node->data.if_node);
  if (node->type == SHELL_NODE_FOR)
    return shell_execute_for_node(state, program, &node->data.for_node);
  if (node->type == SHELL_NODE_WHILE)
    return shell_execute_loop_node(state, program,
                                   &node->data.loop_node,
                                   0);
  if (node->type == SHELL_NODE_UNTIL)
    return shell_execute_loop_node(state, program,
                                   &node->data.loop_node,
                                   1);
  return 1;
}

static int shell_execute_list(struct shell_state *state,
                              const struct shell_program *program,
                              int list_index)
{
  const struct shell_list *list;
  enum shell_next_type prev_next = SHELL_NEXT_SEQ;
  int status = 0;
  int i;

  if (program == 0 || list_index < 0 || list_index >= program->list_count)
    return 1;
  list = &program->lists[list_index];

  for (i = 0; i < list->item_count; i++) {
    int should_run = 1;

    if (prev_next == SHELL_NEXT_AND && status != 0)
      should_run = 0;
    if (prev_next == SHELL_NEXT_OR && status == 0)
      should_run = 0;

    shell_reap_background(state);
    if (should_run != 0) {
      status = shell_execute_node(state, program,
                                  list->items[i].node_index,
                                  list->items[i].next_type == SHELL_NEXT_BACKGROUND);
    }
    prev_next = list->items[i].next_type;
    state->last_status = status;
    if (state->exit_requested != 0 || state->loop_control != SHELL_LOOP_NONE)
      break;
  }

  shell_reap_background(state);
  return status;
}

int shell_execute_program(struct shell_state *state,
                          const struct shell_program *program)
{
  if (state == 0 || program == 0)
    return 1;
  return shell_execute_list(state, program, program->root_list_index);
}

int shell_execute_string(struct shell_state *state, const char *text)
{
  return shell_execute_buffer(state, state != 0 ? state->script_name : "sh",
                              text, 0, 0, 1);
}

int shell_route_probe(const struct shell_state *state, const char *text)
{
  struct shell_program program;
  const struct shell_command *command = 0;
  const char *alias_value = 0;
  char path[SHELL_WORD_SIZE];
  int parse_status;
  enum shell_lookup_kind kind;

  if (text == 0 || text[0] == '\0')
    return SHELL_ROUTE_INVALID;

  parse_status = shell_parse_program(text, (int)strlen(text), &program);
  if (parse_status < 0)
    return SHELL_ROUTE_INVALID;
  if (shell_extract_single_command(&program, &command) < 0)
    return SHELL_ROUTE_COMPOUND;
  if (command == 0)
    return SHELL_ROUTE_INVALID;
  if (command->argc <= 0)
    return SHELL_ROUTE_ASSIGNMENTS;

  path[0] = '\0';
  kind = shell_lookup_command(state, command->argv[0],
                              &alias_value, path, sizeof(path));
  if (kind == SHELL_LOOKUP_ALIAS)
    return SHELL_ROUTE_ALIAS;
  if (kind == SHELL_LOOKUP_BUILTIN)
    return SHELL_ROUTE_BUILTIN;
  if (kind == SHELL_LOOKUP_EXTERNAL) {
    if (strchr(command->argv[0], '/') != 0)
      return SHELL_ROUTE_PATH;
    return SHELL_ROUTE_EXTERNAL;
  }
  return SHELL_ROUTE_UNKNOWN;
}
