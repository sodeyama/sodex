#ifndef _USR_SX_RUNTIME_H
#define _USR_SX_RUNTIME_H

#include <sx_parser.h>

enum sx_value_kind {
  SX_VALUE_NONE = 0,
  SX_VALUE_STRING = 1,
  SX_VALUE_BOOL = 2,
  SX_VALUE_I32 = 3
};

struct sx_value {
  enum sx_value_kind kind;
  char text[SX_TEXT_MAX];
  int bool_value;
  int int_value;
};

struct sx_binding {
  char name[SX_NAME_MAX];
  struct sx_value value;
  int scope_depth;
};

typedef int (*sx_output_fn)(void *ctx, const char *text, int len);

struct sx_runtime {
  struct sx_binding bindings[SX_MAX_BINDINGS];
  int binding_count;
  int scope_depth;
  int call_depth;
  char call_stack[SX_MAX_CALL_DEPTH][SX_NAME_MAX];
  int error_call_depth;
  char error_call_stack[SX_MAX_CALL_DEPTH][SX_NAME_MAX];
  int inside_function;
  sx_output_fn output;
  void *output_ctx;
};

void sx_runtime_init(struct sx_runtime *runtime);
void sx_runtime_set_output(struct sx_runtime *runtime,
                           sx_output_fn output, void *ctx);
int sx_runtime_check_program(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             struct sx_diagnostic *diag);
int sx_runtime_execute_program(struct sx_runtime *runtime,
                               const struct sx_program *program,
                               struct sx_diagnostic *diag);
int sx_runtime_format_stack_trace(const struct sx_runtime *runtime,
                                  char *buf, int cap);

#endif
