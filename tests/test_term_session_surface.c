#include "test_framework.h"
#include <agent/term_session_surface.h>
#include <string.h>
#include <stdio.h>

TEST(drawer_formats_status_and_recent_block) {
    struct term_session_surface surface;
    struct session_meta meta;
    char out[TERM_SESSION_SURFACE_LINE_MAX];

    memset(&meta, 0, sizeof(meta));
    strcpy(meta.id, "12345678deadbeef");
    strcpy(meta.name, "main");
    strcpy(meta.cwd, "/repo/demo");
    strcpy(meta.model, "mock-model");
    meta.turn_count = 4;
    meta.total_tokens = 95000;

    term_session_surface_init(&surface);
    term_session_surface_set_session(&surface, &meta, meta.cwd, meta.total_tokens);
    term_session_surface_set_permission_mode(&surface, PERM_STRICT);
    term_session_surface_set_route(&surface, "agent");
    term_session_surface_set_recent_command(&surface,
                                            "ls /missing",
                                            1,
                                            "",
                                            "not found",
                                            7);
    term_session_surface_set_transcript(&surface,
                                        "原因を見て",
                                        "直前の shell を確認します");
    term_session_surface_set_audit(&surface, "execute run_command ok");

    ASSERT(term_session_surface_format_drawer(&surface, out, sizeof(out)) > 0);
    ASSERT(strstr(out, "session=12345678deadbeef") != NULL);
    ASSERT(strstr(out, "cwd=/repo/demo") != NULL);
    ASSERT(strstr(out, "perm=strict") != NULL);
    ASSERT(strstr(out, "route=agent") != NULL);
    ASSERT(strstr(out, "recent=ls /missing exit=1") != NULL);
    ASSERT(strstr(out, "user: 原因を見て") != NULL);
    ASSERT(strstr(out, "assistant: 直前の shell を確認します") != NULL);
    ASSERT(strstr(out, "audit: execute run_command ok") != NULL);
}

TEST(prompt_bridge_includes_recent_error_tail) {
    struct term_session_surface surface;
    char out[TERM_SESSION_SURFACE_PROMPT_MAX];

    term_session_surface_init(&surface);
    term_session_surface_set_recent_command(&surface,
                                            "cat missing.txt",
                                            1,
                                            "",
                                            "missing.txt: not found",
                                            1);

    ASSERT(term_session_surface_build_prompt(&surface,
                                             "この失敗を直して",
                                             out, sizeof(out)) > 0);
    ASSERT(strstr(out, "Recent shell context:") != NULL);
    ASSERT(strstr(out, "command: cat missing.txt") != NULL);
    ASSERT(strstr(out, "exit_status: 1") != NULL);
    ASSERT(strstr(out, "stderr_tail: missing.txt: not found") != NULL);
    ASSERT(strstr(out, "User request:\nこの失敗を直して") != NULL);
}

TEST(transient_drawer_is_consumed_after_render) {
    struct term_session_surface surface;
    struct session_meta meta;

    memset(&meta, 0, sizeof(meta));
    strcpy(meta.id, "feedbeef");
    strcpy(meta.cwd, "/tmp");

    term_session_surface_init(&surface);
    ASSERT(term_session_surface_should_render(&surface) == 0);
    term_session_surface_set_session(&surface, &meta, meta.cwd, 0);
    ASSERT(term_session_surface_should_render(&surface) == 1);
    term_session_surface_after_render(&surface);
    ASSERT(term_session_surface_should_render(&surface) == 0);
    term_session_surface_set_drawer_mode(&surface, TERM_SESSION_DRAWER_PINNED);
    ASSERT(term_session_surface_should_render(&surface) == 1);
}

int main(void)
{
    printf("=== term session surface tests ===\n");

    RUN_TEST(drawer_formats_status_and_recent_block);
    RUN_TEST(prompt_bridge_includes_recent_error_tail);
    RUN_TEST(transient_drawer_is_consumed_after_render);

    TEST_REPORT();
}
