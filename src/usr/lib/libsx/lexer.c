#include <sx_lexer.h>
#include <string.h>

static int sx_is_ident_start(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    return 1;
  if (ch >= 'A' && ch <= 'Z')
    return 1;
  if (ch == '_')
    return 1;
  return 0;
}

static int sx_is_ident_continue(char ch)
{
  if (sx_is_ident_start(ch))
    return 1;
  if (ch >= '0' && ch <= '9')
    return 1;
  return 0;
}

static int sx_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int sx_emit_token(struct sx_token *tokens, int max_tokens,
                         int *count, enum sx_token_kind kind,
                         int offset, int length, int line, int column,
                         const char *text, int text_len,
                         struct sx_diagnostic *diag)
{
  struct sx_token *token;

  if (*count >= max_tokens) {
    sx_set_diagnostic(diag, offset, length, line, column,
                      "token buffer is full");
    return -1;
  }
  token = &tokens[*count];
  token->kind = kind;
  token->span.offset = offset;
  token->span.length = length;
  token->span.line = line;
  token->span.column = column;
  if (text == 0)
    token->text[0] = '\0';
  else
    sx_copy_slice(token->text, sizeof(token->text), text, text_len);
  (*count)++;
  return 0;
}

int sx_lex(const char *text, int len,
           struct sx_token *tokens, int max_tokens,
           struct sx_diagnostic *diag)
{
  int pos = 0;
  int line = 1;
  int column = 1;
  int count = 0;

  sx_clear_diagnostic(diag);
  if (text == 0 || tokens == 0 || max_tokens <= 0) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid lexer input");
    return -1;
  }

  while (pos < len) {
    char ch = text[pos];

    if (ch == ' ' || ch == '\t' || ch == '\r') {
      pos++;
      column++;
      continue;
    }
    if (ch == '\n') {
      pos++;
      line++;
      column = 1;
      continue;
    }
    if (ch == '/' && pos + 1 < len && text[pos + 1] == '/') {
      pos += 2;
      column += 2;
      while (pos < len && text[pos] != '\n') {
        pos++;
        column++;
      }
      continue;
    }
    if (sx_is_ident_start(ch)) {
      int start = pos;
      int start_column = column;

      pos++;
      column++;
      while (pos < len && sx_is_ident_continue(text[pos])) {
        pos++;
        column++;
      }
      if (pos - start == 2 && strncmp(text + start, "fn", 2) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_FN,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 2 && strncmp(text + start, "if", 2) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_IF,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 3 && strncmp(text + start, "let", 3) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_LET,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 4 && strncmp(text + start, "else", 4) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_ELSE,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 3 && strncmp(text + start, "for", 3) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_FOR,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 5 && strncmp(text + start, "while", 5) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_WHILE,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 5 && strncmp(text + start, "break", 5) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_BREAK,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 8 &&
                 strncmp(text + start, "continue", 8) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count,
                          SX_TOKEN_KEYWORD_CONTINUE,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 6 && strncmp(text + start, "return", 6) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_RETURN,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 4 && strncmp(text + start, "true", 4) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_TRUE,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else if (pos - start == 5 && strncmp(text + start, "false", 5) == 0) {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_KEYWORD_FALSE,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      } else {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_IDENTIFIER,
                          start, pos - start, line, start_column,
                          text + start, pos - start, diag) < 0)
          return -1;
      }
      continue;
    }
    if (sx_is_digit(ch)) {
      int start = pos;
      int start_column = column;

      pos++;
      column++;
      while (pos < len && sx_is_digit(text[pos])) {
        pos++;
        column++;
      }
      if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_INT,
                        start, pos - start, line, start_column,
                        text + start, pos - start, diag) < 0)
        return -1;
      continue;
    }
    if (ch == '"') {
      int start = pos;
      int start_column = column;
      char value[SX_TEXT_MAX];
      int value_len = 0;

      pos++;
      column++;
      while (pos < len) {
        ch = text[pos];
        if (ch == '"') {
          pos++;
          column++;
          if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_STRING,
                            start, pos - start, line, start_column,
                            value, value_len, diag) < 0)
            return -1;
          break;
        }
        if (ch == '\\') {
          char escaped;

          pos++;
          column++;
          if (pos >= len) {
            sx_set_diagnostic(diag, start, 1, line, start_column,
                              "unterminated string literal");
            return -1;
          }
          ch = text[pos];
          if (ch == 'n')
            escaped = '\n';
          else if (ch == 't')
            escaped = '\t';
          else if (ch == '"')
            escaped = '"';
          else if (ch == '\\')
            escaped = '\\';
          else {
            sx_set_diagnostic(diag, pos, 1, line, column,
                              "unsupported escape sequence");
            return -1;
          }
          if (value_len >= SX_TEXT_MAX - 1) {
            sx_set_diagnostic(diag, start, pos - start, line, start_column,
                              "string literal is too long");
            return -1;
          }
          value[value_len++] = escaped;
          pos++;
          column++;
          continue;
        }
        if (ch == '\n') {
          sx_set_diagnostic(diag, start, 1, line, start_column,
                            "unterminated string literal");
          return -1;
        }
        if (value_len >= SX_TEXT_MAX - 1) {
          sx_set_diagnostic(diag, start, pos - start, line, start_column,
                            "string literal is too long");
          return -1;
        }
        value[value_len++] = ch;
        pos++;
        column++;
      }
      if (pos >= len && (count == 0 || tokens[count - 1].kind != SX_TOKEN_STRING ||
                         tokens[count - 1].span.offset != start)) {
        sx_set_diagnostic(diag, start, 1, line, start_column,
                          "unterminated string literal");
        return -1;
      }
      continue;
    }

    {
      enum sx_token_kind kind;

      if (ch == '-' && pos + 1 < len && text[pos + 1] == '>') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_ARROW,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '=' && pos + 1 < len && text[pos + 1] == '=') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_EQUAL_EQUAL,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '!' && pos + 1 < len && text[pos + 1] == '=') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_BANG_EQUAL,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '<' && pos + 1 < len && text[pos + 1] == '=') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_LESS_EQUAL,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '>' && pos + 1 < len && text[pos + 1] == '=') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_GREATER_EQUAL,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '&' && pos + 1 < len && text[pos + 1] == '&') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_AND_AND,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '|' && pos + 1 < len && text[pos + 1] == '|') {
        if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_OR_OR,
                          pos, 2, line, column, text + pos, 2, diag) < 0)
          return -1;
        pos += 2;
        column += 2;
        continue;
      }
      if (ch == '.')
        kind = SX_TOKEN_DOT;
      else if (ch == '+')
        kind = SX_TOKEN_PLUS;
      else if (ch == '-')
        kind = SX_TOKEN_MINUS;
      else if (ch == '*')
        kind = SX_TOKEN_STAR;
      else if (ch == '/')
        kind = SX_TOKEN_SLASH;
      else if (ch == '%')
        kind = SX_TOKEN_PERCENT;
      else if (ch == '!')
        kind = SX_TOKEN_BANG;
      else if (ch == '(')
        kind = SX_TOKEN_LPAREN;
      else if (ch == ')')
        kind = SX_TOKEN_RPAREN;
      else if (ch == '{')
        kind = SX_TOKEN_LBRACE;
      else if (ch == '}')
        kind = SX_TOKEN_RBRACE;
      else if (ch == '[')
        kind = SX_TOKEN_LBRACKET;
      else if (ch == ']')
        kind = SX_TOKEN_RBRACKET;
      else if (ch == ';')
        kind = SX_TOKEN_SEMICOLON;
      else if (ch == ':')
        kind = SX_TOKEN_COLON;
      else if (ch == '=')
        kind = SX_TOKEN_EQUAL;
      else if (ch == '<')
        kind = SX_TOKEN_LESS;
      else if (ch == '>')
        kind = SX_TOKEN_GREATER;
      else if (ch == ',')
        kind = SX_TOKEN_COMMA;
      else {
        sx_set_diagnostic(diag, pos, 1, line, column,
                          "unexpected character");
        return -1;
      }
      if (sx_emit_token(tokens, max_tokens, &count, kind,
                        pos, 1, line, column, &text[pos], 1, diag) < 0)
        return -1;
      pos++;
      column++;
    }
  }

  if (sx_emit_token(tokens, max_tokens, &count, SX_TOKEN_EOF,
                    pos, 0, line, column, "", 0, diag) < 0)
    return -1;
  return count;
}
