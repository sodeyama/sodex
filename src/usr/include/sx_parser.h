#ifndef _USR_SX_PARSER_H
#define _USR_SX_PARSER_H

#include <sx_common.h>

#define SX_CALL_MAX_ARGS 8
#define SX_PARAM_MAX 4

enum sx_atom_kind {
  SX_ATOM_STRING = 0,
  SX_ATOM_NAME = 1,
  SX_ATOM_BOOL = 2,
  SX_ATOM_I32 = 3
};

struct sx_atom {
  enum sx_atom_kind kind;
  struct sx_source_span span;
  char text[SX_TEXT_MAX];
  int bool_value;
  int int_value;
};

enum sx_expr_kind {
  SX_EXPR_ATOM = 0,
  SX_EXPR_CALL = 1,
  SX_EXPR_UNARY = 2,
  SX_EXPR_BINARY = 3,
  SX_EXPR_LIST = 4,
  SX_EXPR_MAP = 5
};

enum sx_unary_op {
  SX_UNARY_NOT = 0,
  SX_UNARY_NEGATE = 1
};

enum sx_binary_op {
  SX_BINARY_ADD = 0,
  SX_BINARY_SUB = 1,
  SX_BINARY_MUL = 2,
  SX_BINARY_DIV = 3,
  SX_BINARY_MOD = 4,
  SX_BINARY_LT = 5,
  SX_BINARY_LE = 6,
  SX_BINARY_GT = 7,
  SX_BINARY_GE = 8,
  SX_BINARY_EQ = 9,
  SX_BINARY_NE = 10,
  SX_BINARY_AND = 11,
  SX_BINARY_OR = 12
};

enum sx_call_target_kind {
  SX_CALL_TARGET_NAMESPACE = 0,
  SX_CALL_TARGET_FUNCTION = 1
};

struct sx_call_expr {
  enum sx_call_target_kind target_kind;
  char target_name[SX_NAME_MAX];
  char member_name[SX_NAME_MAX];
  int args[SX_CALL_MAX_ARGS];
  int arg_count;
};

struct sx_unary_expr {
  enum sx_unary_op op;
  int operand_expr_index;
};

struct sx_binary_expr {
  enum sx_binary_op op;
  int left_expr_index;
  int right_expr_index;
};

struct sx_list_expr {
  int item_start_index;
  int item_count;
};

struct sx_map_literal_item {
  char key[SX_TEXT_MAX];
  int value_expr_index;
};

struct sx_map_expr {
  int item_start_index;
  int item_count;
};

struct sx_expr {
  enum sx_expr_kind kind;
  struct sx_source_span span;
  union {
    struct sx_atom atom;
    struct sx_call_expr call_expr;
    struct sx_unary_expr unary_expr;
    struct sx_binary_expr binary_expr;
    struct sx_list_expr list_expr;
    struct sx_map_expr map_expr;
  } data;
};

struct sx_let_stmt {
  char name[SX_NAME_MAX];
  struct sx_expr value;
};

struct sx_call_stmt {
  struct sx_call_expr call_expr;
};

struct sx_assign_stmt {
  char name[SX_NAME_MAX];
  struct sx_expr value;
};

struct sx_block_stmt {
  int block_index;
};

struct sx_if_stmt {
  struct sx_expr condition;
  int then_block_index;
  int else_block_index;
};

struct sx_while_stmt {
  struct sx_expr condition;
  int body_block_index;
};

struct sx_for_stmt {
  int init_stmt_index;
  int has_condition;
  struct sx_expr condition;
  int step_stmt_index;
  int body_block_index;
};

struct sx_return_stmt {
  struct sx_expr value;
  int has_value;
};

enum sx_stmt_kind {
  SX_STMT_LET = 0,
  SX_STMT_CALL = 1,
  SX_STMT_ASSIGN = 2,
  SX_STMT_BLOCK = 3,
  SX_STMT_IF = 4,
  SX_STMT_WHILE = 5,
  SX_STMT_FOR = 6,
  SX_STMT_RETURN = 7,
  SX_STMT_BREAK = 8,
  SX_STMT_CONTINUE = 9
};

struct sx_stmt {
  enum sx_stmt_kind kind;
  struct sx_source_span span;
  int next_stmt_index;
  union {
    struct sx_let_stmt let_stmt;
    struct sx_call_stmt call_stmt;
    struct sx_assign_stmt assign_stmt;
    struct sx_block_stmt block_stmt;
    struct sx_if_stmt if_stmt;
    struct sx_while_stmt while_stmt;
    struct sx_for_stmt for_stmt;
    struct sx_return_stmt return_stmt;
  } data;
};

struct sx_block {
  int first_stmt_index;
  int stmt_count;
};

struct sx_function {
  struct sx_source_span span;
  char name[SX_NAME_MAX];
  char return_type[SX_NAME_MAX];
  char params[SX_PARAM_MAX][SX_NAME_MAX];
  int param_count;
  int body_block_index;
};

struct sx_program {
  struct sx_function functions[SX_MAX_FUNCTIONS];
  int function_count;
  struct sx_expr exprs[SX_MAX_EXPRS];
  int expr_count;
  int list_literal_items[SX_MAX_LIST_LITERAL_ITEMS];
  int list_literal_item_count;
  struct sx_map_literal_item map_literal_items[SX_MAX_MAP_LITERAL_ITEMS];
  int map_literal_item_count;
  struct sx_block blocks[SX_MAX_BLOCKS];
  int block_count;
  struct sx_stmt statements[SX_MAX_STATEMENTS];
  int statement_count;
  int top_level_block_index;
};

int sx_parse_program(const char *text, int len,
                     struct sx_program *program,
                     struct sx_diagnostic *diag);

#endif
