#include <eshell_parser.h>
#include <string.h>

enum eshell_token_type {
  ESHELL_TOKEN_WORD = 0,
  ESHELL_TOKEN_PIPE = 1,
  ESHELL_TOKEN_INPUT = 2,
  ESHELL_TOKEN_OUTPUT = 3,
  ESHELL_TOKEN_APPEND = 4
};

struct eshell_token {
  enum eshell_token_type type;
  char *text;
};

static int eshell_is_space(char ch);
static int eshell_is_operator(char ch);
static int eshell_push_token(struct eshell_token *tokens, int count,
                             enum eshell_token_type type, char *text);
static int eshell_tokenize(const char *line, int len, char *storage,
                           int storage_cap, struct eshell_token *tokens,
                           int *token_count);

static int eshell_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\0';
}

static int eshell_is_operator(char ch)
{
  return ch == '|' || ch == '<' || ch == '>';
}

static int eshell_push_token(struct eshell_token *tokens, int count,
                             enum eshell_token_type type, char *text)
{
  if (count >= ESHELL_MAX_TOKENS)
    return -1;

  tokens[count].type = type;
  tokens[count].text = text;
  return count + 1;
}

static int eshell_tokenize(const char *line, int len, char *storage,
                           int storage_cap, struct eshell_token *tokens,
                           int *token_count)
{
  const char *src;
  char *dst;
  const char *end;
  int count = 0;

  if (line == 0 || storage == 0 || tokens == 0 || token_count == 0)
    return -1;

  src = line;
  dst = storage;
  end = line + len;
  while (src < end && *src != '\0') {
    char *word_start;
    char quote = '\0';

    while (src < end && eshell_is_space(*src))
      src++;
    if (src >= end || *src == '\0')
      break;

    if (*src == '|') {
      count = eshell_push_token(tokens, count, ESHELL_TOKEN_PIPE, 0);
      if (count < 0)
        return -1;
      src++;
      continue;
    }
    if (*src == '<') {
      count = eshell_push_token(tokens, count, ESHELL_TOKEN_INPUT, 0);
      if (count < 0)
        return -1;
      src++;
      continue;
    }
    if (*src == '>') {
      if (src + 1 < end && src[1] == '>') {
        count = eshell_push_token(tokens, count, ESHELL_TOKEN_APPEND, 0);
        src += 2;
      } else {
        count = eshell_push_token(tokens, count, ESHELL_TOKEN_OUTPUT, 0);
        src++;
      }
      if (count < 0)
        return -1;
      continue;
    }

    word_start = dst;
    while (src < end && *src != '\0') {
      if (quote == '\0' && eshell_is_space(*src))
        break;
      if (quote == '\0' && eshell_is_operator(*src))
        break;
      if (*src == '\\') {
        src++;
        if (src >= end || *src == '\0')
          break;
        if (dst - storage >= storage_cap - 1)
          return -1;
        *dst++ = *src++;
        continue;
      }
      if (*src == '\'' || *src == '"') {
        if (quote == '\0') {
          quote = *src++;
          continue;
        }
        if (quote == *src) {
          quote = '\0';
          src++;
          continue;
        }
      }
      if (dst - storage >= storage_cap - 1)
        return -1;
      *dst++ = *src++;
    }

    if (quote != '\0')
      return -1;
    if (dst - storage >= storage_cap)
      return -1;
    *dst++ = '\0';
    count = eshell_push_token(tokens, count, ESHELL_TOKEN_WORD, word_start);
    if (count < 0)
      return -1;
  }

  *token_count = count;
  return 0;
}

int eshell_parse_line(char *line, int len, struct eshell_pipeline *pipeline)
{
  struct eshell_token tokens[ESHELL_MAX_TOKENS];
  int token_count = 0;
  int i;
  struct eshell_command *command;
  enum eshell_token_type pending = ESHELL_TOKEN_WORD;

  if (line == 0 || pipeline == 0)
    return -1;

  memset(pipeline, 0, sizeof(*pipeline));
  if (eshell_tokenize(line, len, pipeline->storage, ESHELL_STORAGE_SIZE,
                      tokens, &token_count) < 0)
    return -1;
  if (token_count == 0)
    return 0;

  pipeline->command_count = 1;
  command = &pipeline->commands[0];
  for (i = 0; i < token_count; i++) {
    struct eshell_token *token = &tokens[i];

    if (token->type == ESHELL_TOKEN_PIPE) {
      if (pending != ESHELL_TOKEN_WORD || command->argc <= 0)
        return -1;
      if (pipeline->command_count >= ESHELL_MAX_COMMANDS)
        return -1;
      command->argv[command->argc] = 0;
      command = &pipeline->commands[pipeline->command_count++];
      pending = ESHELL_TOKEN_WORD;
      continue;
    }

    if (token->type == ESHELL_TOKEN_INPUT ||
        token->type == ESHELL_TOKEN_OUTPUT ||
        token->type == ESHELL_TOKEN_APPEND) {
      if (pending != ESHELL_TOKEN_WORD)
        return -1;
      pending = token->type;
      continue;
    }

    if (pending == ESHELL_TOKEN_INPUT) {
      if (command->input_path != 0)
        return -1;
      command->input_path = token->text;
      pending = ESHELL_TOKEN_WORD;
      continue;
    }
    if (pending == ESHELL_TOKEN_OUTPUT || pending == ESHELL_TOKEN_APPEND) {
      if (command->output_path != 0)
        return -1;
      command->output_path = token->text;
      command->append_output = (pending == ESHELL_TOKEN_APPEND);
      pending = ESHELL_TOKEN_WORD;
      continue;
    }

    if (command->argc >= ESHELL_ARGV_MAX - 1)
      return -1;
    command->argv[command->argc++] = token->text;
  }

  if (pending != ESHELL_TOKEN_WORD || command->argc <= 0)
    return -1;

  for (i = 0; i < pipeline->command_count; i++) {
    pipeline->commands[i].argv[pipeline->commands[i].argc] = 0;
  }
  return pipeline->command_count;
}
