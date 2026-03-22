#include <sx_parser.h>
#include <sx_lexer.h>
#include <malloc.h>
#include <string.h>

struct sx_parser_state {
  const char *text;
  int len;
  struct sx_token *tokens;
  int token_count;
  int index;
};

static int sx_parse_i32_text(const char *text, int *out_value)
{
  int sign = 1;
  int value = 0;
  int i = 0;

  if (text == 0 || out_value == 0 || text[0] == '\0')
    return -1;
  if (text[i] == '-') {
    sign = -1;
    i++;
  }
  if (text[i] == '\0')
    return -1;
  while (text[i] != '\0') {
    if (text[i] < '0' || text[i] > '9')
      return -1;
    value = value * 10 + (text[i] - '0');
    i++;
  }
  *out_value = value * sign;
  return 0;
}

static const struct sx_token *sx_current_token(struct sx_parser_state *state)
{
  if (state->index >= state->token_count)
    return &state->tokens[state->token_count - 1];
  return &state->tokens[state->index];
}

static const struct sx_token *sx_peek_token(struct sx_parser_state *state, int offset)
{
  int index = state->index + offset;

  if (index >= state->token_count)
    return &state->tokens[state->token_count - 1];
  return &state->tokens[index];
}

static const struct sx_token *sx_advance_token(struct sx_parser_state *state)
{
  const struct sx_token *token = sx_current_token(state);

  if (state->index < state->token_count)
    state->index++;
  return token;
}

static int sx_expect_token(struct sx_parser_state *state,
                           enum sx_token_kind kind,
                           struct sx_diagnostic *diag,
                           const char *message)
{
  const struct sx_token *token = sx_current_token(state);

  if (token->kind != kind) {
    sx_set_diagnostic(diag, token->span.offset, token->span.length,
                      token->span.line, token->span.column, message);
    return -1;
  }
  sx_advance_token(state);
  return 0;
}

static int sx_alloc_statement(struct sx_program *program,
                              struct sx_stmt **stmt,
                              struct sx_diagnostic *diag,
                              const struct sx_token *token)
{
  if (program->statement_count >= SX_MAX_STATEMENTS) {
    sx_set_diagnostic(diag, token->span.offset, token->span.length,
                      token->span.line, token->span.column,
                      "statement buffer is full");
    return -1;
  }
  *stmt = &program->statements[program->statement_count++];
  memset(*stmt, 0, sizeof(**stmt));
  (*stmt)->next_stmt_index = -1;
  return 0;
}

static int sx_alloc_block(struct sx_program *program,
                          int *block_index,
                          struct sx_diagnostic *diag,
                          const struct sx_token *token)
{
  struct sx_block *block;

  if (program->block_count >= SX_MAX_BLOCKS) {
    sx_set_diagnostic(diag, token->span.offset, token->span.length,
                      token->span.line, token->span.column,
                      "block buffer is full");
    return -1;
  }
  *block_index = program->block_count++;
  block = &program->blocks[*block_index];
  memset(block, 0, sizeof(*block));
  block->first_stmt_index = -1;
  return 0;
}

static int sx_alloc_expr(struct sx_program *program,
                         struct sx_expr **expr,
                         struct sx_diagnostic *diag,
                         const struct sx_token *token)
{
  if (program->expr_count >= SX_MAX_EXPRS) {
    sx_set_diagnostic(diag, token->span.offset, token->span.length,
                      token->span.line, token->span.column,
                      "expression buffer is full");
    return -1;
  }
  *expr = &program->exprs[program->expr_count++];
  memset(*expr, 0, sizeof(**expr));
  return 0;
}

static int sx_parse_atom(struct sx_parser_state *state,
                         struct sx_atom *atom,
                         struct sx_diagnostic *diag)
{
  const struct sx_token *token = sx_current_token(state);

  memset(atom, 0, sizeof(*atom));
  if (token->kind == SX_TOKEN_STRING) {
    atom->kind = SX_ATOM_STRING;
    atom->span = token->span;
    sx_copy_text(atom->text, sizeof(atom->text), token->text);
    sx_advance_token(state);
    return 0;
  }
  if (token->kind == SX_TOKEN_KEYWORD_TRUE ||
      token->kind == SX_TOKEN_KEYWORD_FALSE) {
    atom->kind = SX_ATOM_BOOL;
    atom->span = token->span;
    atom->bool_value = token->kind == SX_TOKEN_KEYWORD_TRUE;
    sx_advance_token(state);
    return 0;
  }
  if (token->kind == SX_TOKEN_INT) {
    atom->kind = SX_ATOM_I32;
    atom->span = token->span;
    sx_copy_text(atom->text, sizeof(atom->text), token->text);
    if (sx_parse_i32_text(token->text, &atom->int_value) < 0) {
      sx_set_diagnostic(diag, token->span.offset, token->span.length,
                        token->span.line, token->span.column,
                        "invalid integer literal");
      return -1;
    }
    sx_advance_token(state);
    return 0;
  }
  if (token->kind == SX_TOKEN_IDENTIFIER) {
    atom->kind = SX_ATOM_NAME;
    atom->span = token->span;
    sx_copy_text(atom->text, sizeof(atom->text), token->text);
    sx_advance_token(state);
    return 0;
  }

  sx_set_diagnostic(diag, token->span.offset, token->span.length,
                    token->span.line, token->span.column,
                    "expected atom");
  return -1;
}

static int sx_parse_expr(struct sx_parser_state *state,
                         struct sx_program *program,
                         struct sx_expr *expr,
                         struct sx_diagnostic *diag);

static int sx_parse_call_args(struct sx_parser_state *state,
                              struct sx_program *program,
                              struct sx_call_expr *call_expr,
                              struct sx_diagnostic *diag)
{
  call_expr->arg_count = 0;
  if (sx_current_token(state)->kind == SX_TOKEN_RPAREN)
    return 0;

  while (1) {
    if (call_expr->arg_count >= SX_CALL_MAX_ARGS) {
      const struct sx_token *token = sx_current_token(state);

      sx_set_diagnostic(diag, token->span.offset, token->span.length,
                        token->span.line, token->span.column,
                        "too many call arguments");
      return -1;
    }
    {
      struct sx_expr *arg_expr;
      int arg_expr_index;

      if (sx_alloc_expr(program, &arg_expr, diag, sx_current_token(state)) < 0)
        return -1;
      arg_expr_index = program->expr_count - 1;
      if (sx_parse_expr(state, program, arg_expr, diag) < 0)
        return -1;
      call_expr->args[call_expr->arg_count] = arg_expr_index;
    }
    call_expr->arg_count++;
    if (sx_current_token(state)->kind != SX_TOKEN_COMMA)
      break;
    sx_advance_token(state);
  }
  return 0;
}

static int sx_parse_expr(struct sx_parser_state *state,
                         struct sx_program *program,
                         struct sx_expr *expr,
                         struct sx_diagnostic *diag)
{
  const struct sx_token *token = sx_current_token(state);
  const struct sx_token *next = sx_peek_token(state, 1);

  memset(expr, 0, sizeof(*expr));
  if (token->kind == SX_TOKEN_IDENTIFIER && next->kind == SX_TOKEN_DOT) {
    const struct sx_token *member_token;

    expr->kind = SX_EXPR_CALL;
    expr->span = token->span;
    expr->data.call_expr.target_kind = SX_CALL_TARGET_NAMESPACE;
    sx_copy_text(expr->data.call_expr.target_name,
                 sizeof(expr->data.call_expr.target_name),
                 token->text);
    sx_advance_token(state);
    sx_advance_token(state);
    member_token = sx_current_token(state);
    if (sx_expect_token(state, SX_TOKEN_IDENTIFIER, diag,
                        "expected member name after '.'") < 0)
      return -1;
    sx_copy_text(expr->data.call_expr.member_name,
                 sizeof(expr->data.call_expr.member_name),
                 member_token->text);
    if (sx_expect_token(state, SX_TOKEN_LPAREN, diag,
                        "expected '(' after call target") < 0)
      return -1;
    if (sx_parse_call_args(state, program, &expr->data.call_expr, diag) < 0)
      return -1;
    if (sx_expect_token(state, SX_TOKEN_RPAREN, diag,
                        "expected ')' after call argument list") < 0)
      return -1;
    return 0;
  }
  if (token->kind == SX_TOKEN_IDENTIFIER && next->kind == SX_TOKEN_LPAREN) {
    expr->kind = SX_EXPR_CALL;
    expr->span = token->span;
    expr->data.call_expr.target_kind = SX_CALL_TARGET_FUNCTION;
    sx_copy_text(expr->data.call_expr.target_name,
                 sizeof(expr->data.call_expr.target_name),
                 token->text);
    expr->data.call_expr.member_name[0] = '\0';
    sx_advance_token(state);
    sx_advance_token(state);
    if (sx_parse_call_args(state, program, &expr->data.call_expr, diag) < 0)
      return -1;
    if (sx_expect_token(state, SX_TOKEN_RPAREN, diag,
                        "expected ')' after call argument list") < 0)
      return -1;
    return 0;
  }
  expr->kind = SX_EXPR_ATOM;
  if (sx_parse_atom(state, &expr->data.atom, diag) < 0)
    return -1;
  expr->span = expr->data.atom.span;
  return 0;
}

static int sx_parse_let_stmt(struct sx_parser_state *state,
                             struct sx_program *program,
                             struct sx_stmt *stmt,
                             struct sx_diagnostic *diag)
{
  const struct sx_token *start = sx_advance_token(state);
  const struct sx_token *name_token = sx_current_token(state);

  if (sx_expect_token(state, SX_TOKEN_IDENTIFIER, diag,
                      "expected binding name after let") < 0)
    return -1;
  if (sx_expect_token(state, SX_TOKEN_EQUAL, diag,
                      "expected '=' after binding name") < 0)
    return -1;

  stmt->kind = SX_STMT_LET;
  stmt->span = start->span;
  sx_copy_text(stmt->data.let_stmt.name,
               sizeof(stmt->data.let_stmt.name),
               name_token->text);
  if (sx_parse_expr(state, program, &stmt->data.let_stmt.value, diag) < 0)
    return -1;
  if (sx_expect_token(state, SX_TOKEN_SEMICOLON, diag,
                      "expected ';' after let statement") < 0)
    return -1;
  return 0;
}

static int sx_parse_call_stmt(struct sx_parser_state *state,
                              struct sx_program *program,
                              struct sx_stmt *stmt,
                              struct sx_diagnostic *diag)
{
  struct sx_expr expr;

  if (sx_parse_expr(state, program, &expr, diag) < 0)
    return -1;
  if (expr.kind != SX_EXPR_CALL) {
    sx_set_diagnostic(diag, expr.span.offset, expr.span.length,
                      expr.span.line, expr.span.column,
                      "expected call statement");
    return -1;
  }
  if (sx_expect_token(state, SX_TOKEN_SEMICOLON, diag,
                      "expected ';' after call statement") < 0)
    return -1;

  stmt->kind = SX_STMT_CALL;
  stmt->span = expr.span;
  stmt->data.call_stmt.call_expr = expr.data.call_expr;
  return 0;
}

static int sx_parse_statement(struct sx_parser_state *state,
                              struct sx_program *program,
                              struct sx_stmt *stmt,
                              struct sx_diagnostic *diag);

static int sx_parse_block_body(struct sx_parser_state *state,
                               struct sx_program *program,
                               int block_index,
                               enum sx_token_kind closing_kind,
                               struct sx_diagnostic *diag)
{
  int *next_slot = &program->blocks[block_index].first_stmt_index;

  while (sx_current_token(state)->kind != closing_kind &&
         sx_current_token(state)->kind != SX_TOKEN_EOF) {
    struct sx_stmt *stmt;
    int stmt_index;

    if (sx_alloc_statement(program, &stmt, diag, sx_current_token(state)) < 0)
      return -1;
    stmt_index = program->statement_count - 1;
    if (sx_parse_statement(state, program, stmt, diag) < 0)
      return -1;
    *next_slot = stmt_index;
    next_slot = &stmt->next_stmt_index;
    program->blocks[block_index].stmt_count++;
  }
  return 0;
}

static int sx_parse_braced_block(struct sx_parser_state *state,
                                 struct sx_program *program,
                                 int *block_index,
                                 struct sx_diagnostic *diag)
{
  const struct sx_token *start = sx_current_token(state);

  if (sx_expect_token(state, SX_TOKEN_LBRACE, diag,
                      "expected '{' to start block") < 0)
    return -1;
  if (sx_alloc_block(program, block_index, diag, start) < 0)
    return -1;
  if (sx_parse_block_body(state, program, *block_index, SX_TOKEN_RBRACE,
                          diag) < 0)
    return -1;
  if (sx_expect_token(state, SX_TOKEN_RBRACE, diag,
                      "expected '}' after block") < 0)
    return -1;
  return 0;
}

static int sx_parse_block_stmt(struct sx_parser_state *state,
                               struct sx_program *program,
                               struct sx_stmt *stmt,
                               struct sx_diagnostic *diag)
{
  stmt->kind = SX_STMT_BLOCK;
  stmt->span = sx_current_token(state)->span;
  return sx_parse_braced_block(state, program,
                               &stmt->data.block_stmt.block_index, diag);
}

static int sx_parse_if_stmt(struct sx_parser_state *state,
                            struct sx_program *program,
                            struct sx_stmt *stmt,
                            struct sx_diagnostic *diag)
{
  const struct sx_token *start = sx_advance_token(state);

  stmt->kind = SX_STMT_IF;
  stmt->span = start->span;
  stmt->data.if_stmt.else_block_index = -1;
  if (sx_expect_token(state, SX_TOKEN_LPAREN, diag,
                      "expected '(' after if") < 0)
    return -1;
  if (sx_parse_expr(state, program, &stmt->data.if_stmt.condition, diag) < 0)
    return -1;
  if (sx_expect_token(state, SX_TOKEN_RPAREN, diag,
                      "expected ')' after if condition") < 0)
    return -1;
  if (sx_parse_braced_block(state, program,
                            &stmt->data.if_stmt.then_block_index, diag) < 0)
    return -1;
  if (sx_current_token(state)->kind == SX_TOKEN_KEYWORD_ELSE) {
    sx_advance_token(state);
    if (sx_parse_braced_block(state, program,
                              &stmt->data.if_stmt.else_block_index, diag) < 0)
      return -1;
  }
  return 0;
}

static int sx_parse_while_stmt(struct sx_parser_state *state,
                               struct sx_program *program,
                               struct sx_stmt *stmt,
                               struct sx_diagnostic *diag)
{
  const struct sx_token *start = sx_advance_token(state);

  stmt->kind = SX_STMT_WHILE;
  stmt->span = start->span;
  if (sx_expect_token(state, SX_TOKEN_LPAREN, diag,
                      "expected '(' after while") < 0)
    return -1;
  if (sx_parse_expr(state, program, &stmt->data.while_stmt.condition, diag) < 0)
    return -1;
  if (sx_expect_token(state, SX_TOKEN_RPAREN, diag,
                      "expected ')' after while condition") < 0)
    return -1;
  return sx_parse_braced_block(state, program,
                               &stmt->data.while_stmt.body_block_index, diag);
}

static int sx_parse_return_stmt(struct sx_parser_state *state,
                                struct sx_program *program,
                                struct sx_stmt *stmt,
                                struct sx_diagnostic *diag)
{
  const struct sx_token *start = sx_advance_token(state);

  stmt->kind = SX_STMT_RETURN;
  stmt->span = start->span;
  stmt->data.return_stmt.has_value = 0;
  if (sx_current_token(state)->kind != SX_TOKEN_SEMICOLON) {
    if (sx_parse_expr(state, program, &stmt->data.return_stmt.value, diag) < 0)
      return -1;
    stmt->data.return_stmt.has_value = 1;
  }
  if (sx_expect_token(state, SX_TOKEN_SEMICOLON, diag,
                      "expected ';' after return statement") < 0)
    return -1;
  return 0;
}

static int sx_parse_statement(struct sx_parser_state *state,
                              struct sx_program *program,
                              struct sx_stmt *stmt,
                              struct sx_diagnostic *diag)
{
  const struct sx_token *token = sx_current_token(state);

  if (token->kind == SX_TOKEN_KEYWORD_LET)
    return sx_parse_let_stmt(state, program, stmt, diag);
  if (token->kind == SX_TOKEN_KEYWORD_IF)
    return sx_parse_if_stmt(state, program, stmt, diag);
  if (token->kind == SX_TOKEN_KEYWORD_WHILE)
    return sx_parse_while_stmt(state, program, stmt, diag);
  if (token->kind == SX_TOKEN_KEYWORD_RETURN)
    return sx_parse_return_stmt(state, program, stmt, diag);
  if (token->kind == SX_TOKEN_LBRACE)
    return sx_parse_block_stmt(state, program, stmt, diag);
  return sx_parse_call_stmt(state, program, stmt, diag);
}

static int sx_parse_function(struct sx_parser_state *state,
                             struct sx_program *program,
                             struct sx_diagnostic *diag)
{
  struct sx_function *fn;
  const struct sx_token *start = sx_advance_token(state);
  const struct sx_token *name_token = sx_current_token(state);
  int block_index;

  if (program->function_count >= SX_MAX_FUNCTIONS) {
    sx_set_diagnostic(diag, start->span.offset, start->span.length,
                      start->span.line, start->span.column,
                      "function buffer is full");
    return -1;
  }
  if (sx_expect_token(state, SX_TOKEN_IDENTIFIER, diag,
                      "expected function name after fn") < 0)
    return -1;
  if (sx_expect_token(state, SX_TOKEN_LPAREN, diag,
                      "expected '(' after function name") < 0)
    return -1;

  fn = &program->functions[program->function_count];
  memset(fn, 0, sizeof(*fn));
  fn->span = start->span;
  sx_copy_text(fn->name, sizeof(fn->name), name_token->text);

  if (sx_current_token(state)->kind != SX_TOKEN_RPAREN) {
    while (1) {
      const struct sx_token *param_token = sx_current_token(state);

      if (fn->param_count >= SX_PARAM_MAX) {
        sx_set_diagnostic(diag, param_token->span.offset, param_token->span.length,
                          param_token->span.line, param_token->span.column,
                          "too many function parameters");
        return -1;
      }
      if (sx_expect_token(state, SX_TOKEN_IDENTIFIER, diag,
                          "expected parameter name") < 0)
        return -1;
      sx_copy_text(fn->params[fn->param_count],
                   sizeof(fn->params[fn->param_count]),
                   param_token->text);
      fn->param_count++;
      if (sx_current_token(state)->kind != SX_TOKEN_COMMA)
        break;
      sx_advance_token(state);
    }
  }
  if (sx_expect_token(state, SX_TOKEN_RPAREN, diag,
                      "expected ')' after parameter list") < 0)
    return -1;
  if (sx_current_token(state)->kind == SX_TOKEN_ARROW) {
    const struct sx_token *type_token;

    sx_advance_token(state);
    type_token = sx_current_token(state);
    if (sx_expect_token(state, SX_TOKEN_IDENTIFIER, diag,
                        "expected return type after '->'") < 0)
      return -1;
    sx_copy_text(fn->return_type, sizeof(fn->return_type), type_token->text);
  }
  if (sx_parse_braced_block(state, program, &block_index, diag) < 0)
    return -1;
  fn->body_block_index = block_index;
  program->function_count++;
  return 0;
}

int sx_parse_program(const char *text, int len,
                     struct sx_program *program,
                     struct sx_diagnostic *diag)
{
  struct sx_parser_state state;
  struct sx_token *tokens = 0;
  int lex_status;

  sx_clear_diagnostic(diag);
  if (text == 0 || program == 0) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "invalid parser input");
    return -1;
  }

  memset(&state, 0, sizeof(state));
  memset(program, 0, sizeof(*program));
  program->top_level_block_index = -1;

  tokens = (struct sx_token *)malloc(sizeof(struct sx_token) * SX_MAX_TOKENS);
  if (tokens == 0) {
    sx_set_diagnostic(diag, 0, 0, 0, 0, "parser token allocation failed");
    return -1;
  }
  memset(tokens, 0, sizeof(struct sx_token) * SX_MAX_TOKENS);
  state.text = text;
  state.len = len;
  state.tokens = tokens;

  lex_status = sx_lex(text, len, state.tokens, SX_MAX_TOKENS, diag);
  if (lex_status < 0) {
    free(tokens);
    return -1;
  }
  state.token_count = lex_status;
  state.index = 0;

  if (sx_alloc_block(program, &program->top_level_block_index, diag,
                     sx_current_token(&state)) < 0) {
    free(tokens);
    return -1;
  }

  while (sx_current_token(&state)->kind != SX_TOKEN_EOF) {
    if (sx_current_token(&state)->kind == SX_TOKEN_KEYWORD_FN) {
      if (sx_parse_function(&state, program, diag) < 0) {
        free(tokens);
        return -1;
      }
    } else {
      struct sx_stmt *stmt;
      int stmt_index;
      struct sx_block *block = &program->blocks[program->top_level_block_index];
      int *next_slot;

      if (sx_alloc_statement(program, &stmt, diag, sx_current_token(&state)) < 0) {
        free(tokens);
        return -1;
      }
      stmt_index = program->statement_count - 1;
      if (sx_parse_statement(&state, program, stmt, diag) < 0) {
        free(tokens);
        return -1;
      }
      next_slot = &block->first_stmt_index;
      while (*next_slot >= 0)
        next_slot = &program->statements[*next_slot].next_stmt_index;
      *next_slot = stmt_index;
      block->stmt_count++;
    }
  }

  free(tokens);
  return 0;
}
