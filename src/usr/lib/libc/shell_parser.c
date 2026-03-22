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

struct shell_parser_state {
  struct shell_token tokens[SHELL_MAX_TOKENS];
  int token_count;
  int pos;
  struct shell_program *program;
};

static int shell_parse_list(struct shell_parser_state *state,
                            const char **stop_words, int stop_count,
                            int *list_index_out);

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
    return SHELL_PARSE_ERROR;
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
    return SHELL_PARSE_ERROR;
  if (command->redirection_count >= SHELL_MAX_REDIRECTIONS)
    return SHELL_PARSE_ERROR;

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
  const char *src;
  const char *p;
  enum shell_token_type token_type;
  int fd = 0;
  int has_fd = 0;
  int target_fd = -1;

  src = *src_ptr;
  p = src;
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
      return SHELL_PARSE_ERROR;
    token_type = SHELL_TOKEN_INPUT;
    p++;
  } else {
    if (has_fd == 0)
      fd = 1;
    if (shell_supported_fd(fd) == 0)
      return SHELL_PARSE_ERROR;
    p++;
    if (p < end && *p == '>') {
      token_type = SHELL_TOKEN_APPEND;
      p++;
    } else if (p < end && *p == '&') {
      token_type = SHELL_TOKEN_DUP;
      p++;
      if (p >= end || shell_digit(*p) == 0)
        return SHELL_PARSE_ERROR;
      target_fd = 0;
      while (p < end && shell_digit(*p)) {
        target_fd = target_fd * 10 + (*p - '0');
        p++;
      }
      if (shell_supported_fd(target_fd) == 0)
        return SHELL_PARSE_ERROR;
    } else {
      token_type = SHELL_TOKEN_OUTPUT;
    }
  }

  count = shell_push_token(tokens, count, token_type, 0, fd, target_fd);
  if (count < 0)
    return count;
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
    return SHELL_PARSE_ERROR;

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
        return count;
      src++;
      continue;
    }
    if (*src == ';') {
      count = shell_push_token(tokens, count, SHELL_TOKEN_SEMI, 0, -1, -1);
      if (count < 0)
        return count;
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
        return count;
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
        return count;
      continue;
    }

    next_count = shell_tokenize_redirection(&src, end, tokens, count);
    if (next_count < 0)
      return next_count;
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
          return SHELL_PARSE_ERROR;
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
        return SHELL_PARSE_ERROR;
      *dst++ = *src++;
    }

    if (quote != '\0')
      return SHELL_PARSE_INCOMPLETE;
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
      return count;
  }

  *token_count = count;
  return 0;
}

static int shell_parser_at_end(struct shell_parser_state *state)
{
  return state->pos >= state->token_count;
}

static struct shell_token *shell_parser_peek(struct shell_parser_state *state)
{
  if (shell_parser_at_end(state) != 0)
    return 0;
  return &state->tokens[state->pos];
}

static int shell_parser_peek_type(struct shell_parser_state *state)
{
  struct shell_token *token = shell_parser_peek(state);

  if (token == 0)
    return SHELL_TOKEN_NONE;
  return token->type;
}

static int shell_word_equals(struct shell_token *token, const char *text)
{
  if (token == 0 || token->type != SHELL_TOKEN_WORD || text == 0)
    return 0;
  return strcmp(token->text, text) == 0;
}

static int shell_stop_word(struct shell_parser_state *state,
                           const char **stop_words, int stop_count)
{
  struct shell_token *token;
  int i;

  if (stop_words == 0 || stop_count <= 0)
    return 0;

  token = shell_parser_peek(state);
  if (token == 0 || token->type != SHELL_TOKEN_WORD)
    return 0;

  for (i = 0; i < stop_count; i++) {
    if (strcmp(token->text, stop_words[i]) == 0)
      return 1;
  }
  return 0;
}

static void shell_skip_separators(struct shell_parser_state *state)
{
  while (shell_parser_at_end(state) == 0) {
    int type = shell_parser_peek_type(state);

    if (type != SHELL_TOKEN_NEWLINE && type != SHELL_TOKEN_SEMI)
      break;
    state->pos++;
  }
}

static int shell_add_pipeline(struct shell_program *program)
{
  int index;

  if (program->pipeline_count >= SHELL_MAX_PIPELINES)
    return SHELL_PARSE_ERROR;
  index = program->pipeline_count++;
  memset(&program->pipelines[index], 0, sizeof(program->pipelines[index]));
  return index;
}

static int shell_add_list(struct shell_program *program)
{
  int index;

  if (program->list_count >= SHELL_MAX_LISTS)
    return SHELL_PARSE_ERROR;
  index = program->list_count++;
  memset(&program->lists[index], 0, sizeof(program->lists[index]));
  return index;
}

static int shell_add_node(struct shell_program *program,
                          enum shell_node_type type,
                          int *node_index_out)
{
  int index;

  if (program->node_count >= SHELL_MAX_NODES)
    return SHELL_PARSE_ERROR;
  index = program->node_count++;
  memset(&program->nodes[index], 0, sizeof(program->nodes[index]));
  program->nodes[index].type = type;
  *node_index_out = index;
  return 0;
}

static int shell_expect_word(struct shell_parser_state *state,
                             const char *text)
{
  struct shell_token *token = shell_parser_peek(state);

  if (token == 0)
    return SHELL_PARSE_INCOMPLETE;
  if (shell_word_equals(token, text) == 0)
    return SHELL_PARSE_ERROR;
  state->pos++;
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

static int shell_parse_simple_command(struct shell_parser_state *state,
                                      struct shell_command *command,
                                      const char **stop_words, int stop_count)
{
  enum shell_token_type pending_redir = SHELL_TOKEN_NONE;
  int pending_fd = -1;

  while (shell_parser_at_end(state) == 0) {
    struct shell_token *token = shell_parser_peek(state);

    if (token->type == SHELL_TOKEN_WORD) {
      if (command->argc == 0 &&
          command->assignment_count == 0 &&
          command->redirection_count == 0 &&
          shell_stop_word(state, stop_words, stop_count) != 0) {
        break;
      }

      state->pos++;
      if (pending_redir == SHELL_TOKEN_INPUT) {
        if (shell_push_redirection(command, SHELL_REDIR_INPUT,
                                   pending_fd, -1, token->text) < 0)
          return SHELL_PARSE_ERROR;
        pending_redir = SHELL_TOKEN_NONE;
        pending_fd = -1;
        continue;
      }
      if (pending_redir == SHELL_TOKEN_OUTPUT ||
          pending_redir == SHELL_TOKEN_APPEND) {
        if (shell_push_redirection(command,
                                   pending_redir == SHELL_TOKEN_APPEND ?
                                   SHELL_REDIR_APPEND : SHELL_REDIR_OUTPUT,
                                   pending_fd, -1, token->text) < 0)
          return SHELL_PARSE_ERROR;
        pending_redir = SHELL_TOKEN_NONE;
        pending_fd = -1;
        continue;
      }
      if (command->argc == 0 && shell_assignment_word(token->text) != 0) {
        if (command->assignment_count >= SHELL_MAX_ASSIGNMENTS)
          return SHELL_PARSE_ERROR;
        command->assignments[command->assignment_count++] = token->text;
        continue;
      }
      if (command->argc >= SHELL_MAX_ARGS - 1)
        return SHELL_PARSE_ERROR;
      command->argv[command->argc++] = token->text;
      command->argv[command->argc] = 0;
      continue;
    }

    if (token->type == SHELL_TOKEN_INPUT ||
        token->type == SHELL_TOKEN_OUTPUT ||
        token->type == SHELL_TOKEN_APPEND) {
      if (pending_redir != SHELL_TOKEN_NONE)
        return SHELL_PARSE_ERROR;
      pending_redir = token->type;
      pending_fd = token->fd;
      state->pos++;
      continue;
    }

    if (token->type == SHELL_TOKEN_DUP) {
      if (pending_redir != SHELL_TOKEN_NONE)
        return SHELL_PARSE_ERROR;
      if (shell_push_redirection(command, SHELL_REDIR_DUP,
                                 token->fd, token->target_fd, 0) < 0)
        return SHELL_PARSE_ERROR;
      state->pos++;
      continue;
    }

    break;
  }

  if (pending_redir != SHELL_TOKEN_NONE) {
    if (shell_parser_at_end(state) != 0)
      return SHELL_PARSE_INCOMPLETE;
    return SHELL_PARSE_ERROR;
  }
  return 0;
}

static int shell_parse_pipeline_node(struct shell_parser_state *state,
                                     const char **stop_words, int stop_count,
                                     int *node_index_out)
{
  struct shell_pipeline *pipeline;
  struct shell_command *command;
  int pipeline_index;
  int node_index;
  int rc;

  pipeline_index = shell_add_pipeline(state->program);
  if (pipeline_index < 0)
    return pipeline_index;
  pipeline = &state->program->pipelines[pipeline_index];
  pipeline->command_count = 1;
  command = &pipeline->commands[0];
  memset(command, 0, sizeof(*command));

  while (1) {
    rc = shell_parse_simple_command(state, command, stop_words, stop_count);
    if (rc < 0)
      return rc;
    if (shell_command_has_content(command) == 0)
      return SHELL_PARSE_ERROR;

    if (shell_parser_peek_type(state) != SHELL_TOKEN_PIPE)
      break;
    if (command->argc <= 0)
      return SHELL_PARSE_ERROR;

    state->pos++;
    if (shell_parser_at_end(state) != 0)
      return SHELL_PARSE_INCOMPLETE;
    if (pipeline->command_count >= SHELL_MAX_COMMANDS)
      return SHELL_PARSE_ERROR;

    command = &pipeline->commands[pipeline->command_count++];
    memset(command, 0, sizeof(*command));
  }

  if (shell_add_node(state->program, SHELL_NODE_PIPELINE, &node_index) < 0)
    return SHELL_PARSE_ERROR;
  state->program->nodes[node_index].data.pipeline_index = pipeline_index;
  *node_index_out = node_index;
  return 0;
}

static int shell_parse_if_node(struct shell_parser_state *state,
                               int *node_index_out)
{
  static const char *then_stop[] = {"then"};
  static const char *then_body_stop[] = {"elif", "else", "fi"};
  static const char *fi_stop[] = {"fi"};
  struct shell_if_node *if_node;
  int node_index;
  int rc;

  rc = shell_expect_word(state, "if");
  if (rc < 0)
    return rc;

  if (shell_add_node(state->program, SHELL_NODE_IF, &node_index) < 0)
    return SHELL_PARSE_ERROR;
  if_node = &state->program->nodes[node_index].data.if_node;

  rc = shell_parse_list(state, then_stop, 1, &if_node->cond_list_index);
  if (rc < 0)
    return rc;
  rc = shell_expect_word(state, "then");
  if (rc < 0)
    return rc;
  rc = shell_parse_list(state, then_body_stop, 3, &if_node->then_list_index);
  if (rc < 0)
    return rc;

  while (shell_word_equals(shell_parser_peek(state), "elif") != 0) {
    struct shell_if_clause *clause;

    if (if_node->elif_count >= SHELL_MAX_IF_ELIFS)
      return SHELL_PARSE_ERROR;
    state->pos++;
    clause = &if_node->elifs[if_node->elif_count++];
    rc = shell_parse_list(state, then_stop, 1, &clause->cond_list_index);
    if (rc < 0)
      return rc;
    rc = shell_expect_word(state, "then");
    if (rc < 0)
      return rc;
    rc = shell_parse_list(state, then_body_stop, 3, &clause->body_list_index);
    if (rc < 0)
      return rc;
  }

  if (shell_word_equals(shell_parser_peek(state), "else") != 0) {
    state->pos++;
    if_node->has_else = 1;
    rc = shell_parse_list(state, fi_stop, 1, &if_node->else_list_index);
    if (rc < 0)
      return rc;
  }

  rc = shell_expect_word(state, "fi");
  if (rc < 0)
    return rc;
  *node_index_out = node_index;
  return 0;
}

static int shell_parse_for_node(struct shell_parser_state *state,
                                int *node_index_out)
{
  static const char *done_stop[] = {"done"};
  struct shell_for_node *for_node;
  struct shell_token *token;
  int node_index;
  int saw_in = 0;
  int rc;

  rc = shell_expect_word(state, "for");
  if (rc < 0)
    return rc;

  token = shell_parser_peek(state);
  if (token == 0)
    return SHELL_PARSE_INCOMPLETE;
  if (token->type != SHELL_TOKEN_WORD)
    return SHELL_PARSE_ERROR;

  if (shell_add_node(state->program, SHELL_NODE_FOR, &node_index) < 0)
    return SHELL_PARSE_ERROR;
  for_node = &state->program->nodes[node_index].data.for_node;
  for_node->name = token->text;
  state->pos++;

  token = shell_parser_peek(state);
  if (shell_word_equals(token, "in") != 0) {
    saw_in = 1;
    state->pos++;
    while (shell_parser_at_end(state) == 0) {
      token = shell_parser_peek(state);
      if (token->type == SHELL_TOKEN_WORD) {
        if (for_node->word_count >= SHELL_MAX_FOR_WORDS)
          return SHELL_PARSE_ERROR;
        for_node->words[for_node->word_count++] = token->text;
        state->pos++;
        continue;
      }
      if (token->type == SHELL_TOKEN_NEWLINE || token->type == SHELL_TOKEN_SEMI)
        break;
      return SHELL_PARSE_ERROR;
    }
  } else {
    for_node->implicit_params = 1;
  }

  if (shell_parser_at_end(state) != 0)
    return SHELL_PARSE_INCOMPLETE;
  if (saw_in != 0 ||
      shell_parser_peek_type(state) == SHELL_TOKEN_NEWLINE ||
      shell_parser_peek_type(state) == SHELL_TOKEN_SEMI) {
    shell_skip_separators(state);
  }

  rc = shell_expect_word(state, "do");
  if (rc < 0)
    return rc;
  rc = shell_parse_list(state, done_stop, 1, &for_node->body_list_index);
  if (rc < 0)
    return rc;
  rc = shell_expect_word(state, "done");
  if (rc < 0)
    return rc;
  *node_index_out = node_index;
  return 0;
}

static int shell_parse_loop_node(struct shell_parser_state *state,
                                 enum shell_node_type type,
                                 int *node_index_out)
{
  static const char *do_stop[] = {"do"};
  static const char *done_stop[] = {"done"};
  struct shell_loop_node *loop_node;
  const char *keyword;
  int node_index;
  int rc;

  keyword = type == SHELL_NODE_WHILE ? "while" : "until";
  rc = shell_expect_word(state, keyword);
  if (rc < 0)
    return rc;

  if (shell_add_node(state->program, type, &node_index) < 0)
    return SHELL_PARSE_ERROR;
  loop_node = &state->program->nodes[node_index].data.loop_node;

  rc = shell_parse_list(state, do_stop, 1, &loop_node->cond_list_index);
  if (rc < 0)
    return rc;
  rc = shell_expect_word(state, "do");
  if (rc < 0)
    return rc;
  rc = shell_parse_list(state, done_stop, 1, &loop_node->body_list_index);
  if (rc < 0)
    return rc;
  rc = shell_expect_word(state, "done");
  if (rc < 0)
    return rc;
  *node_index_out = node_index;
  return 0;
}

static int shell_parse_node(struct shell_parser_state *state,
                            const char **stop_words, int stop_count,
                            int *node_index_out)
{
  struct shell_token *token = shell_parser_peek(state);

  if (token == 0)
    return SHELL_PARSE_INCOMPLETE;
  if (token->type == SHELL_TOKEN_WORD) {
    if (strcmp(token->text, "if") == 0)
      return shell_parse_if_node(state, node_index_out);
    if (strcmp(token->text, "for") == 0)
      return shell_parse_for_node(state, node_index_out);
    if (strcmp(token->text, "while") == 0)
      return shell_parse_loop_node(state, SHELL_NODE_WHILE, node_index_out);
    if (strcmp(token->text, "until") == 0)
      return shell_parse_loop_node(state, SHELL_NODE_UNTIL, node_index_out);
  }
  return shell_parse_pipeline_node(state, stop_words, stop_count, node_index_out);
}

static int shell_parse_list(struct shell_parser_state *state,
                            const char **stop_words, int stop_count,
                            int *list_index_out)
{
  struct shell_list *list;
  int list_index;

  list_index = shell_add_list(state->program);
  if (list_index < 0)
    return list_index;
  list = &state->program->lists[list_index];

  shell_skip_separators(state);
  while (shell_parser_at_end(state) == 0 &&
         shell_stop_word(state, stop_words, stop_count) == 0) {
    struct shell_list_item *item;
    int node_index;
    int rc;
    int type;

    if (list->item_count >= SHELL_MAX_LIST_ITEMS)
      return SHELL_PARSE_ERROR;

    rc = shell_parse_node(state, stop_words, stop_count, &node_index);
    if (rc < 0)
      return rc;

    item = &list->items[list->item_count++];
    item->node_index = node_index;
    item->next_type = SHELL_NEXT_END;

    if (shell_parser_at_end(state) != 0)
      break;

    type = shell_parser_peek_type(state);
    if (type == SHELL_TOKEN_AND || type == SHELL_TOKEN_OR ||
        type == SHELL_TOKEN_AMP) {
      if (type == SHELL_TOKEN_AND)
        item->next_type = SHELL_NEXT_AND;
      else if (type == SHELL_TOKEN_OR)
        item->next_type = SHELL_NEXT_OR;
      else
        item->next_type = SHELL_NEXT_BACKGROUND;
      state->pos++;

      if (item->next_type == SHELL_NEXT_BACKGROUND) {
        shell_skip_separators(state);
        if (shell_parser_at_end(state) != 0 ||
            shell_stop_word(state, stop_words, stop_count) != 0)
          break;
        continue;
      }

      shell_skip_separators(state);
      if (shell_parser_at_end(state) != 0)
        return SHELL_PARSE_INCOMPLETE;
      if (shell_stop_word(state, stop_words, stop_count) != 0)
        return SHELL_PARSE_INCOMPLETE;
      continue;
    }

    if (type == SHELL_TOKEN_NEWLINE || type == SHELL_TOKEN_SEMI) {
      shell_skip_separators(state);
      if (shell_parser_at_end(state) != 0 ||
          shell_stop_word(state, stop_words, stop_count) != 0)
        break;
      item->next_type = SHELL_NEXT_SEQ;
      continue;
    }

    break;
  }

  *list_index_out = list_index;
  return 0;
}

int shell_parse_program(const char *text, int len, struct shell_program *program)
{
  struct shell_parser_state state;
  int rc;

  if (text == 0 || program == 0)
    return SHELL_PARSE_ERROR;

  memset(program, 0, sizeof(*program));
  memset(&state, 0, sizeof(state));
  state.program = program;

  rc = shell_tokenize(text, len, program->storage, sizeof(program->storage),
                      state.tokens, &state.token_count);
  if (rc < 0)
    return rc;

  rc = shell_parse_list(&state, 0, 0, &program->root_list_index);
  if (rc < 0)
    return rc;
  shell_skip_separators(&state);
  if (shell_parser_at_end(&state) == 0)
    return SHELL_PARSE_ERROR;
  return program->lists[program->root_list_index].item_count;
}
