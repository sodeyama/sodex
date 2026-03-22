#ifndef _USR_SX_LEXER_H
#define _USR_SX_LEXER_H

#include <sx_common.h>

enum sx_token_kind {
  SX_TOKEN_EOF = 0,
  SX_TOKEN_IDENTIFIER,
  SX_TOKEN_STRING,
  SX_TOKEN_INT,
  SX_TOKEN_DOT,
  SX_TOKEN_LPAREN,
  SX_TOKEN_RPAREN,
  SX_TOKEN_LBRACE,
  SX_TOKEN_RBRACE,
  SX_TOKEN_SEMICOLON,
  SX_TOKEN_EQUAL,
  SX_TOKEN_COMMA,
  SX_TOKEN_ARROW,
  SX_TOKEN_KEYWORD_LET,
  SX_TOKEN_KEYWORD_FN,
  SX_TOKEN_KEYWORD_IF,
  SX_TOKEN_KEYWORD_ELSE,
  SX_TOKEN_KEYWORD_WHILE,
  SX_TOKEN_KEYWORD_RETURN,
  SX_TOKEN_KEYWORD_TRUE,
  SX_TOKEN_KEYWORD_FALSE
};

struct sx_token {
  enum sx_token_kind kind;
  struct sx_source_span span;
  char text[SX_TEXT_MAX];
};

int sx_lex(const char *text, int len,
           struct sx_token *tokens, int max_tokens,
           struct sx_diagnostic *diag);

#endif
