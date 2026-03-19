#include <shell.h>
#include <string.h>

enum shell_token_type {
  SHELL_TOKEN_NONE = 0,
  SHELL_TOKEN_WORD = 1,
  SHELL_TOKEN_PIPE = 2,
  SHELL_TOKEN_INPUT = 3,
  SHELL_TOKEN_OUTPUT = 4,
  SHELL_TOKEN_APPEND = 5,
  SHELL_TOKEN_DUP = 6,
  SHELL_TOKEN_AND = 7,
  SHELL_TOKEN_OR = 8,
  SHELL_TOKEN_AMP = 9,
  SHELL_TOKEN_SEMI = 10,
  SHELL_TOKEN_NEWLINE = 11
};

struct shell_token {
  enum shell_token_type type;
  char *text;
  int fd;
  int target_fd;
};

static int shell_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r';
}

static int shell_name_start(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    return 1;
  if (ch >= 'A' && ch <= 'Z')
    return 1;
  return ch == '_';
}

static int shell_name_char(char ch)
{
  if (shell_name_start(ch))
    return 1;
  return ch >= '0' && ch <= '9';
}

static int shell_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int shell_supported_fd(int fd)
{
  return fd >= 0 && fd <= 2;
}

static int shell_assignment_word(const char *text)
{
  int i;

  if (text == 0 || shell_name_start(text[0]) == 0)
    return 0;

  for (i = 1; text[i] != '\0'; i++) {
    if (text[i] == '=')
      return 1;
    if (shell_name_char(text[i]) == 0)
      return 0;
  }
  return 0;
}

static int shell_push_token(struct shell_token *tokens, int count,
                            enum shell_token_type type, char *text,
                            int fd, int target_fd)
{
  if (count >= SHELL_MAX_TOKENS)
    return -1;
  tokens[count].type = type;
  tokens[count].text = text;
  tokens[count].fd = fd;
  tokens[count].target_fd = target_fd;
  return count + 1;
}

static int shell_push_redirection(struct shell_command *command,
                                  enum shell_redirection_type type,
                                  int fd, int target_fd, char *path)
{
  struct shell_redirection *redirection;

  if (command == 0)
    return -1;
  if (command->redirection_count >= SHELL_MAX_REDIRECTIONS)
    return -1;

  redirection = &command->redirections[command->redirection_count++];
  redirection->type = type;
  redirection->fd = fd;
  redirection->target_fd = target_fd;
  redirection->path = path;
  return 0;
}

static int shell_tokenize_redirection(const char **src_ptr, const char *end,
                                      struct shell_token *tokens, int count)
{
  const char *src = *src_ptr;
  const char *p = src;
  enum shell_token_type token_type;
  int fd = 0;
  int has_fd = 0;
  int target_fd = -1;

  while (p < end && shell_digit(*p)) {
    has_fd = 1;
    fd = fd * 10 + (*p - '0');
    p++;
  }
  if (p >= end)
    return count;
  if (*p != '<' && *p != '>')
    return count;

  if (*p == '<') {
    if (has_fd == 0)
      fd = 0;
    if (shell_supported_fd(fd) == 0)
      return -1;
    token_type = SHELL_TOKEN_INPUT;
    p++;
  } else {
    if (has_fd == 0)
      fd = 1;
    if (shell_supported_fd(fd) == 0)
      return -1;
    p++;
    if (p < end && *p == '>') {
      token_type = SHELL_TOKEN_APPEND;
      p++;
    } else if (p < end && *p == '&') {
      token_type = SHELL_TOKEN_DUP;
      p++;
      if (p >= end || shell_digit(*p) == 0)
        return -1;
      target_fd = 0;
      while (p < end && shell_digit(*p)) {
        target_fd = target_fd * 10 + (*p - '0');
        p++;
      }
      if (shell_supported_fd(target_fd) == 0)
        return -1;
    } else {
      token_type = SHELL_TOKEN_OUTPUT;
    }
  }

  count = shell_push_token(tokens, count, token_type, 0, fd, target_fd);
  if (count < 0)
    return -1;
  *src_ptr = p;
  return count;
}

static int shell_tokenize(const char *text, int len,
                          char *storage, int storage_cap,
                          struct shell_token *tokens, int *token_count)
{
  const char *src;
  const char *end;
  char *dst;
  int count = 0;

  if (text == 0 || storage == 0 || tokens == 0 || token_count == 0)
    return -1;

  src = text;
  end = text + len;
  dst = storage;
  while (src < end && *src != '\0') {
    char *word_start;
    char quote = '\0';
    int next_count;

    while (src < end && shell_space(*src))
      src++;
    if (src >= end || *src == '\0')
      break;

    if (*src == '\n') {
      count = shell_push_token(tokens, count, SHELL_TOKEN_NEWLINE, 0, -1, -1);
      if (count < 0)
        return -1;
      src++;
      continue;
    }
    if (*src == ';') {
      count = shell_push_token(tokens, count, SHELL_TOKEN_SEMI, 0, -1, -1);
      if (count < 0)
        return -1;
      src++;
      continue;
    }
    if (*src == '&') {
      if (src + 1 < end && src[1] == '&') {
        count = shell_push_token(tokens, count, SHELL_TOKEN_AND, 0, -1, -1);
        src += 2;
      } else {
        count = shell_push_token(tokens, count, SHELL_TOKEN_AMP, 0, -1, -1);
        src++;
      }
      if (count < 0)
        return -1;
      continue;
    }
    if (*src == '|') {
      if (src + 1 < end && src[1] == '|') {
        count = shell_push_token(tokens, count, SHELL_TOKEN_OR, 0, -1, -1);
        src += 2;
      } else {
        count = shell_push_token(tokens, count, SHELL_TOKEN_PIPE, 0, -1, -1);
        src++;
      }
      if (count < 0)
        return -1;
      continue;
    }

    next_count = shell_tokenize_redirection(&src, end, tokens, count);
    if (next_count < 0)
      return -1;
    if (next_count != count) {
      count = next_count;
      continue;
    }

    if (*src == '#') {
      while (src < end && *src != '\n' && *src != '\0')
        src++;
      continue;
    }

    word_start = dst;
    while (src < end && *src != '\0') {
      if (quote == '\0') {
        if (shell_space(*src) || *src == '\n' || *src == ';' ||
            *src == '&' || *src == '|' || *src == '<' || *src == '>')
          break;
        if (*src == '#' && word_start == dst)
          break;
      }

      if (*src == '\\' && src + 1 < end) {
        if (dst - storage >= storage_cap - 2)
          return -1;
        *dst++ = *src++;
        *dst++ = *src++;
        continue;
      }

      if (*src == '\'' || *src == '"') {
        if (quote == '\0')
          quote = *src;
        else if (quote == *src)
          quote = '\0';
      }

      if (dst - storage >= storage_cap - 1)
        return -1;
      *dst++ = *src++;
    }

    if (quote != '\0')
      return -1;
    if (dst == word_start) {
      if (src < end && *src == '#') {
        while (src < end && *src != '\n' && *src != '\0')
          src++;
      }
      continue;
    }
    *dst++ = '\0';
    count = shell_push_token(tokens, count, SHELL_TOKEN_WORD, word_start, -1, -1);
    if (count < 0)
      return -1;
  }

  *token_count = count;
  return 0;
}

static int shell_push_pipeline(struct shell_program *program,
                               struct shell_pipeline **pipeline)
{
  if (program->pipeline_count >= SHELL_MAX_PIPELINES)
    return -1;
  *pipeline = &program->pipelines[program->pipeline_count++];
  memset(*pipeline, 0, sizeof(**pipeline));
  (*pipeline)->command_count = 1;
  return 0;
}

static int shell_current_command(struct shell_pipeline *pipeline,
                                 struct shell_command **command)
{
  if (pipeline == 0 || pipeline->command_count <= 0)
    return -1;
  *command = &pipeline->commands[pipeline->command_count - 1];
  return 0;
}

static int shell_command_has_content(const struct shell_command *command)
{
  if (command == 0)
    return 0;
  return command->argc > 0 ||
         command->assignment_count > 0 ||
         command->redirection_count > 0;
}

int shell_parse_program(const char *text, int len, struct shell_program *program)
{
  struct shell_token tokens[SHELL_MAX_TOKENS];
  struct shell_pipeline *pipeline = 0;
  struct shell_command *command = 0;
  enum shell_token_type pending_redir = SHELL_TOKEN_NONE;
  int pending_fd = -1;
  int token_count = 0;
  int i;

  if (text == 0 || program == 0)
    return -1;

  memset(program, 0, sizeof(*program));
  if (shell_tokenize(text, len, program->storage, sizeof(program->storage),
                     tokens, &token_count) < 0)
    return -1;
  if (token_count == 0)
    return 0;

  for (i = 0; i < token_count; i++) {
    struct shell_token *token = &tokens[i];

    if (token->type == SHELL_TOKEN_NEWLINE || token->type == SHELL_TOKEN_SEMI) {
      if (pending_redir != SHELL_TOKEN_NONE)
        return -1;
      if (pipeline == 0)
        continue;
      if (shell_current_command(pipeline, &command) < 0)
        return -1;
      if (shell_command_has_content(command) == 0)
        return -1;
      pipeline->next_type = SHELL_NEXT_SEQ;
      pipeline = 0;
      command = 0;
      continue;
    }

    if (token->type == SHELL_TOKEN_AND ||
        token->type == SHELL_TOKEN_OR ||
        token->type == SHELL_TOKEN_AMP) {
      if (pending_redir != SHELL_TOKEN_NONE || pipeline == 0)
        return -1;
      if (shell_current_command(pipeline, &command) < 0)
        return -1;
      if (shell_command_has_content(command) == 0)
        return -1;
      if (token->type == SHELL_TOKEN_AND)
        pipeline->next_type = SHELL_NEXT_AND;
      else if (token->type == SHELL_TOKEN_OR)
        pipeline->next_type = SHELL_NEXT_OR;
      else
        pipeline->next_type = SHELL_NEXT_BACKGROUND;
      pipeline = 0;
      command = 0;
      continue;
    }

    if (pipeline == 0) {
      if (shell_push_pipeline(program, &pipeline) < 0)
        return -1;
      command = &pipeline->commands[0];
    }

    if (token->type == SHELL_TOKEN_PIPE) {
      if (pending_redir != SHELL_TOKEN_NONE)
        return -1;
      if (shell_command_has_content(command) == 0 || command->argc <= 0)
        return -1;
      if (pipeline->command_count >= SHELL_MAX_COMMANDS)
        return -1;
      command = &pipeline->commands[pipeline->command_count++];
      continue;
    }

    if (token->type == SHELL_TOKEN_INPUT ||
        token->type == SHELL_TOKEN_OUTPUT ||
        token->type == SHELL_TOKEN_APPEND) {
      if (pending_redir != SHELL_TOKEN_NONE)
        return -1;
      pending_redir = token->type;
      pending_fd = token->fd;
      continue;
    }

    if (token->type == SHELL_TOKEN_DUP) {
      if (shell_push_redirection(command, SHELL_REDIR_DUP,
                                 token->fd, token->target_fd, 0) < 0)
        return -1;
      continue;
    }

    if (token->type != SHELL_TOKEN_WORD)
      return -1;

    if (pending_redir == SHELL_TOKEN_INPUT) {
      if (shell_push_redirection(command, SHELL_REDIR_INPUT,
                                 pending_fd, -1, token->text) < 0)
        return -1;
      pending_redir = SHELL_TOKEN_NONE;
      pending_fd = -1;
      continue;
    }
    if (pending_redir == SHELL_TOKEN_OUTPUT || pending_redir == SHELL_TOKEN_APPEND) {
      if (shell_push_redirection(command,
                                 pending_redir == SHELL_TOKEN_APPEND ?
                                 SHELL_REDIR_APPEND : SHELL_REDIR_OUTPUT,
                                 pending_fd, -1, token->text) < 0)
        return -1;
      pending_redir = SHELL_TOKEN_NONE;
      pending_fd = -1;
      continue;
    }

    if (command->argc == 0 && shell_assignment_word(token->text) != 0) {
      if (command->assignment_count >= SHELL_MAX_ASSIGNMENTS)
        return -1;
      command->assignments[command->assignment_count++] = token->text;
      continue;
    }

    if (command->argc >= SHELL_MAX_ARGS - 1)
      return -1;
    command->argv[command->argc++] = token->text;
    command->argv[command->argc] = 0;
  }

  if (pending_redir != SHELL_TOKEN_NONE)
    return -1;
  if (pipeline != 0) {
    if (shell_current_command(pipeline, &command) < 0)
      return -1;
    if (shell_command_has_content(command) == 0)
      return -1;
    pipeline->next_type = SHELL_NEXT_END;
  }

  return program->pipeline_count;
}
