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
        ASSERT_EQ(stmt2->data.let_stmt.value.kind, SX_EXPR_ATOM);
        ASSERT_EQ(stmt2->data.let_stmt.value.data.atom.kind, SX_ATOM_I32);
        ASSERT_EQ(stmt2->data.let_stmt.value.data.atom.int_value, -7);

        ASSERT_EQ(stmt3->kind, SX_STMT_BLOCK);
        ASSERT_EQ(program.blocks[stmt3->data.block_stmt.block_index].stmt_count, 1);

        ASSERT_EQ(stmt4->kind, SX_STMT_CALL);
        ASSERT_EQ(stmt4->data.call_stmt.call_expr.target_kind, SX_CALL_TARGET_NAMESPACE);
        ASSERT_STR_EQ(stmt4->data.call_stmt.call_expr.target_name, "io");
        ASSERT_STR_EQ(stmt4->data.call_stmt.call_expr.member_name, "println");
        ASSERT_STR_EQ(stmt4->data.call_stmt.call_expr.args[0].text, "name");
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
    ASSERT_EQ(stmt1->data.let_stmt.value.data.call_expr.args[0].kind, SX_ATOM_NAME);
    ASSERT_STR_EQ(stmt1->data.let_stmt.value.data.call_expr.args[0].text, "flag");
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
    RUN_TEST(parse_requires_semicolon);

    TEST_REPORT();
}
