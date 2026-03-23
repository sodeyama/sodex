#include "test_framework.h"
#include <sx_parser.h>
#include <string.h>

static const struct sx_stmt *block_stmt_at(const struct sx_program *program,
                                           int block_index,
                                           int ordinal)
{
    int stmt_index;

    stmt_index = program->blocks[block_index].first_stmt_index;
    while (ordinal > 0 && stmt_index >= 0) {
        stmt_index = program->statements[stmt_index].next_stmt_index;
        ordinal--;
    }
    if (stmt_index < 0)
        return NULL;
    return &program->statements[stmt_index];
}

static const struct sx_expr *call_arg_expr_at(const struct sx_program *program,
                                              const struct sx_call_expr *call_expr,
                                              int ordinal)
{
    int expr_index;

    if (ordinal < 0 || ordinal >= call_expr->arg_count)
        return NULL;
    expr_index = call_expr->args[ordinal];
    if (expr_index < 0 || expr_index >= program->expr_count)
        return NULL;
    return &program->exprs[expr_index];
}

static const struct sx_expr *expr_at_index(const struct sx_program *program,
                                           int expr_index)
{
    if (expr_index < 0 || expr_index >= program->expr_count)
        return NULL;
    return &program->exprs[expr_index];
}

static const struct sx_expr *unary_operand_expr_at(const struct sx_program *program,
                                                   const struct sx_expr *expr)
{
    if (expr == NULL || expr->kind != SX_EXPR_UNARY)
        return NULL;
    return expr_at_index(program, expr->data.unary_expr.operand_expr_index);
}

static const struct sx_expr *binary_left_expr_at(const struct sx_program *program,
                                                 const struct sx_expr *expr)
{
    if (expr == NULL || expr->kind != SX_EXPR_BINARY)
        return NULL;
    return expr_at_index(program, expr->data.binary_expr.left_expr_index);
}

static const struct sx_expr *binary_right_expr_at(const struct sx_program *program,
                                                  const struct sx_expr *expr)
{
    if (expr == NULL || expr->kind != SX_EXPR_BINARY)
        return NULL;
    return expr_at_index(program, expr->data.binary_expr.right_expr_index);
}

TEST(parse_function_blocks_and_control_flow) {
    const char *text =
        "fn choose(flag) -> str {\n"
        "  if (flag) {\n"
        "    return \"YES\";\n"
        "  } else {\n"
        "    while (false) {\n"
        "      return \"LOOP\";\n"
        "    }\n"
        "    return \"NO\";\n"
        "  }\n"
        "}\n"
        "let flag = true;\n"
        "let name = choose(flag);\n"
        "let code = -7;\n"
        "{\n"
        "  let shadow = \"INNER\";\n"
        "}\n"
        "io.println(name);\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    const struct sx_function *fn;
    const struct sx_stmt *stmt0;
    const struct sx_stmt *stmt1;
    const struct sx_stmt *stmt2;
    const struct sx_stmt *stmt3;
    const struct sx_stmt *if_stmt;
    const struct sx_stmt *else_stmt0;
    const struct sx_stmt *else_stmt1;
    const struct sx_expr *arg0;

    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);
    ASSERT_EQ(program.function_count, 1);
    ASSERT_EQ(program.top_level_block_index >= 0, 1);
    ASSERT_EQ(program.blocks[program.top_level_block_index].stmt_count, 5);

    fn = &program.functions[0];
    ASSERT_STR_EQ(fn->name, "choose");
    ASSERT_STR_EQ(fn->return_type, "str");
    ASSERT_EQ(fn->param_count, 1);
    ASSERT_STR_EQ(fn->params[0], "flag");

    if_stmt = block_stmt_at(&program, fn->body_block_index, 0);
    ASSERT_EQ(if_stmt != NULL, 1);
    ASSERT_EQ(if_stmt->kind, SX_STMT_IF);
    ASSERT_EQ(if_stmt->data.if_stmt.condition.kind, SX_EXPR_ATOM);
    ASSERT_EQ(if_stmt->data.if_stmt.condition.data.atom.kind, SX_ATOM_NAME);
    ASSERT_STR_EQ(if_stmt->data.if_stmt.condition.data.atom.text, "flag");

    ASSERT_EQ(program.blocks[if_stmt->data.if_stmt.then_block_index].stmt_count, 1);
    ASSERT_EQ(program.blocks[if_stmt->data.if_stmt.else_block_index].stmt_count, 2);

    else_stmt0 = block_stmt_at(&program, if_stmt->data.if_stmt.else_block_index, 0);
    else_stmt1 = block_stmt_at(&program, if_stmt->data.if_stmt.else_block_index, 1);
    ASSERT_EQ(else_stmt0->kind, SX_STMT_WHILE);
    ASSERT_EQ(else_stmt1->kind, SX_STMT_RETURN);

    stmt0 = block_stmt_at(&program, program.top_level_block_index, 0);
    stmt1 = block_stmt_at(&program, program.top_level_block_index, 1);
    stmt2 = block_stmt_at(&program, program.top_level_block_index, 2);
    stmt3 = block_stmt_at(&program, program.top_level_block_index, 3);
    {
        const struct sx_stmt *stmt4 =
            block_stmt_at(&program, program.top_level_block_index, 4);

        ASSERT_EQ(stmt2->kind, SX_STMT_LET);
        ASSERT_STR_EQ(stmt2->data.let_stmt.name, "code");
        ASSERT_EQ(stmt2->data.let_stmt.value.kind, SX_EXPR_UNARY);
        ASSERT_EQ(stmt2->data.let_stmt.value.data.unary_expr.op, SX_UNARY_NEGATE);
        arg0 = unary_operand_expr_at(&program, &stmt2->data.let_stmt.value);
        ASSERT_EQ(arg0 != NULL, 1);
        ASSERT_EQ(arg0->kind, SX_EXPR_ATOM);
        ASSERT_EQ(arg0->data.atom.kind, SX_ATOM_I32);
        ASSERT_EQ(arg0->data.atom.int_value, 7);

        ASSERT_EQ(stmt3->kind, SX_STMT_BLOCK);
        ASSERT_EQ(program.blocks[stmt3->data.block_stmt.block_index].stmt_count, 1);

        ASSERT_EQ(stmt4->kind, SX_STMT_CALL);
        ASSERT_EQ(stmt4->data.call_stmt.call_expr.target_kind, SX_CALL_TARGET_NAMESPACE);
        ASSERT_STR_EQ(stmt4->data.call_stmt.call_expr.target_name, "io");
        ASSERT_STR_EQ(stmt4->data.call_stmt.call_expr.member_name, "println");
        arg0 = call_arg_expr_at(&program, &stmt4->data.call_stmt.call_expr, 0);
        ASSERT_EQ(arg0 != NULL, 1);
        ASSERT_EQ(arg0->kind, SX_EXPR_ATOM);
        ASSERT_EQ(arg0->data.atom.kind, SX_ATOM_NAME);
        ASSERT_STR_EQ(arg0->data.atom.text, "name");
    }

    ASSERT_EQ(stmt0->kind, SX_STMT_LET);
    ASSERT_STR_EQ(stmt0->data.let_stmt.name, "flag");
    ASSERT_EQ(stmt0->data.let_stmt.value.kind, SX_EXPR_ATOM);
    ASSERT_EQ(stmt0->data.let_stmt.value.data.atom.kind, SX_ATOM_BOOL);
    ASSERT_EQ(stmt0->data.let_stmt.value.data.atom.bool_value, 1);

    ASSERT_EQ(stmt1->kind, SX_STMT_LET);
    ASSERT_EQ(stmt1->data.let_stmt.value.kind, SX_EXPR_CALL);
    ASSERT_EQ(stmt1->data.let_stmt.value.data.call_expr.target_kind, SX_CALL_TARGET_FUNCTION);
    ASSERT_STR_EQ(stmt1->data.let_stmt.value.data.call_expr.target_name, "choose");
    ASSERT_EQ(stmt1->data.let_stmt.value.data.call_expr.arg_count, 1);
    arg0 = call_arg_expr_at(&program, &stmt1->data.let_stmt.value.data.call_expr, 0);
    ASSERT_EQ(arg0 != NULL, 1);
    ASSERT_EQ(arg0->kind, SX_EXPR_ATOM);
    ASSERT_EQ(arg0->data.atom.kind, SX_ATOM_NAME);
    ASSERT_STR_EQ(arg0->data.atom.text, "flag");
}

TEST(parse_nested_call_arguments) {
    const char *text =
        "let name = text.trim(text.concat(\"  sx\", \"  \"));\n"
        "io.println(text.trim(name));\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    const struct sx_stmt *stmt0;
    const struct sx_stmt *stmt1;
    const struct sx_expr *trim_arg;
    const struct sx_expr *concat_arg0;
    const struct sx_expr *concat_arg1;
    const struct sx_expr *print_arg;

    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);

    stmt0 = block_stmt_at(&program, program.top_level_block_index, 0);
    stmt1 = block_stmt_at(&program, program.top_level_block_index, 1);
    ASSERT_EQ(stmt0 != NULL, 1);
    ASSERT_EQ(stmt1 != NULL, 1);

    ASSERT_EQ(stmt0->kind, SX_STMT_LET);
    ASSERT_EQ(stmt0->data.let_stmt.value.kind, SX_EXPR_CALL);
    ASSERT_STR_EQ(stmt0->data.let_stmt.value.data.call_expr.target_name, "text");
    ASSERT_STR_EQ(stmt0->data.let_stmt.value.data.call_expr.member_name, "trim");

    trim_arg = call_arg_expr_at(&program, &stmt0->data.let_stmt.value.data.call_expr, 0);
    ASSERT_EQ(trim_arg != NULL, 1);
    ASSERT_EQ(trim_arg->kind, SX_EXPR_CALL);
    ASSERT_STR_EQ(trim_arg->data.call_expr.target_name, "text");
    ASSERT_STR_EQ(trim_arg->data.call_expr.member_name, "concat");

    concat_arg0 = call_arg_expr_at(&program, &trim_arg->data.call_expr, 0);
    concat_arg1 = call_arg_expr_at(&program, &trim_arg->data.call_expr, 1);
    ASSERT_EQ(concat_arg0 != NULL, 1);
    ASSERT_EQ(concat_arg1 != NULL, 1);
    ASSERT_EQ(concat_arg0->kind, SX_EXPR_ATOM);
    ASSERT_EQ(concat_arg0->data.atom.kind, SX_ATOM_STRING);
    ASSERT_STR_EQ(concat_arg0->data.atom.text, "  sx");
    ASSERT_EQ(concat_arg1->kind, SX_EXPR_ATOM);
    ASSERT_EQ(concat_arg1->data.atom.kind, SX_ATOM_STRING);
    ASSERT_STR_EQ(concat_arg1->data.atom.text, "  ");

    ASSERT_EQ(stmt1->kind, SX_STMT_CALL);
    print_arg = call_arg_expr_at(&program, &stmt1->data.call_stmt.call_expr, 0);
    ASSERT_EQ(print_arg != NULL, 1);
    ASSERT_EQ(print_arg->kind, SX_EXPR_CALL);
    ASSERT_STR_EQ(print_arg->data.call_expr.target_name, "text");
    ASSERT_STR_EQ(print_arg->data.call_expr.member_name, "trim");
}

TEST(parse_operators_assignment_and_for) {
    const char *text =
        "let code = -7;\n"
        "let ok = !false && (1 + 2 * 3 == 7);\n"
        "for (let i = 0; i < 3; i = i + 1) {\n"
        "  if (i == 1) {\n"
        "    continue;\n"
        "  }\n"
        "  break;\n"
        "}\n";
    struct sx_program program;
    struct sx_diagnostic diag;
    const struct sx_stmt *stmt0;
    const struct sx_stmt *stmt1;
    const struct sx_stmt *stmt2;
    const struct sx_stmt *init_stmt;
    const struct sx_stmt *step_stmt;
    const struct sx_stmt *body_stmt0;
    const struct sx_stmt *body_stmt1;
    const struct sx_expr *left;
    const struct sx_expr *right;
    const struct sx_expr *mul_expr;

    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), 0);

    stmt0 = block_stmt_at(&program, program.top_level_block_index, 0);
    stmt1 = block_stmt_at(&program, program.top_level_block_index, 1);
    stmt2 = block_stmt_at(&program, program.top_level_block_index, 2);
    ASSERT_EQ(stmt0 != NULL, 1);
    ASSERT_EQ(stmt1 != NULL, 1);
    ASSERT_EQ(stmt2 != NULL, 1);

    ASSERT_EQ(stmt0->kind, SX_STMT_LET);
    ASSERT_EQ(stmt0->data.let_stmt.value.kind, SX_EXPR_UNARY);
    ASSERT_EQ(stmt0->data.let_stmt.value.data.unary_expr.op, SX_UNARY_NEGATE);

    ASSERT_EQ(stmt1->kind, SX_STMT_LET);
    ASSERT_EQ(stmt1->data.let_stmt.value.kind, SX_EXPR_BINARY);
    ASSERT_EQ(stmt1->data.let_stmt.value.data.binary_expr.op, SX_BINARY_AND);
    left = binary_left_expr_at(&program, &stmt1->data.let_stmt.value);
    right = binary_right_expr_at(&program, &stmt1->data.let_stmt.value);
    ASSERT_EQ(left != NULL, 1);
    ASSERT_EQ(right != NULL, 1);
    ASSERT_EQ(left->kind, SX_EXPR_UNARY);
    ASSERT_EQ(left->data.unary_expr.op, SX_UNARY_NOT);
    ASSERT_EQ(right->kind, SX_EXPR_BINARY);
    ASSERT_EQ(right->data.binary_expr.op, SX_BINARY_EQ);
    left = binary_left_expr_at(&program, right);
    ASSERT_EQ(left != NULL, 1);
    ASSERT_EQ(left->kind, SX_EXPR_BINARY);
    ASSERT_EQ(left->data.binary_expr.op, SX_BINARY_ADD);
    mul_expr = binary_right_expr_at(&program, left);
    ASSERT_EQ(mul_expr != NULL, 1);
    ASSERT_EQ(mul_expr->kind, SX_EXPR_BINARY);
    ASSERT_EQ(mul_expr->data.binary_expr.op, SX_BINARY_MUL);

    ASSERT_EQ(stmt2->kind, SX_STMT_FOR);
    ASSERT_EQ(stmt2->data.for_stmt.has_condition, 1);
    init_stmt = &program.statements[stmt2->data.for_stmt.init_stmt_index];
    step_stmt = &program.statements[stmt2->data.for_stmt.step_stmt_index];
    ASSERT_EQ(init_stmt->kind, SX_STMT_LET);
    ASSERT_STR_EQ(init_stmt->data.let_stmt.name, "i");
    ASSERT_EQ(stmt2->data.for_stmt.condition.kind, SX_EXPR_BINARY);
    ASSERT_EQ(stmt2->data.for_stmt.condition.data.binary_expr.op, SX_BINARY_LT);
    ASSERT_EQ(step_stmt->kind, SX_STMT_ASSIGN);
    ASSERT_STR_EQ(step_stmt->data.assign_stmt.name, "i");
    ASSERT_EQ(step_stmt->data.assign_stmt.value.kind, SX_EXPR_BINARY);
    ASSERT_EQ(step_stmt->data.assign_stmt.value.data.binary_expr.op, SX_BINARY_ADD);

    body_stmt0 = block_stmt_at(&program, stmt2->data.for_stmt.body_block_index, 0);
    body_stmt1 = block_stmt_at(&program, stmt2->data.for_stmt.body_block_index, 1);
    ASSERT_EQ(body_stmt0 != NULL, 1);
    ASSERT_EQ(body_stmt1 != NULL, 1);
    ASSERT_EQ(body_stmt0->kind, SX_STMT_IF);
    ASSERT_EQ(body_stmt1->kind, SX_STMT_BREAK);
    body_stmt0 = block_stmt_at(&program, body_stmt0->data.if_stmt.then_block_index, 0);
    ASSERT_EQ(body_stmt0 != NULL, 1);
    ASSERT_EQ(body_stmt0->kind, SX_STMT_CONTINUE);
}

TEST(parse_requires_semicolon) {
    const char *text = "let name = \"oops\"\nio.println(name);\n";
    struct sx_program program;
    struct sx_diagnostic diag;

    ASSERT_EQ(sx_parse_program(text, (int)strlen(text), &program, &diag), -1);
    ASSERT_STR_EQ(diag.message, "expected ';' after let statement");
}

int main(void)
{
    printf("=== sx parser tests ===\n");

    RUN_TEST(parse_function_blocks_and_control_flow);
    RUN_TEST(parse_nested_call_arguments);
    RUN_TEST(parse_operators_assignment_and_for);
    RUN_TEST(parse_requires_semicolon);

    TEST_REPORT();
}
