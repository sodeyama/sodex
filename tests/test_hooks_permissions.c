/*
 * test_hooks_permissions.c - Hook system, permission policy, and audit tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent/hooks.h"
#include "agent/permissions.h"
#include "agent/audit.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        failed++; \
        return; \
    } \
} while (0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while (0)

static int perm_check_json(const struct permission_policy *policy,
                           const char *tool_name, const char *input_json)
{
    return perm_check_tool(policy, tool_name,
                           input_json, (int)strlen(input_json));
}

/* ---- Mock hook handlers ---- */

static int hook_pass(const struct hook_context *ctx,
                     struct hook_response *response)
{
    (void)ctx;
    response->decision = HOOK_CONTINUE;
    return 0;
}

static int hook_block(const struct hook_context *ctx,
                      struct hook_response *response)
{
    (void)ctx;
    response->decision = HOOK_BLOCK;
    snprintf(response->message, sizeof(response->message),
             "blocked by test hook");
    return 0;
}

static int hook_error(const struct hook_context *ctx,
                      struct hook_response *response)
{
    (void)ctx;
    (void)response;
    return -1; /* handler error, should be skipped */
}

/* ---- Hook tests ---- */

static void test_hooks_init(void)
{
    struct hook_context ctx;
    struct hook_response resp;

    TEST_START("hooks_init");
    hooks_init();

    memset(&ctx, 0, sizeof(ctx));
    ctx.event = HOOK_PRE_TOOL_USE;
    ASSERT(hooks_fire(&ctx, &resp) == 0, "fire with no handlers");
    ASSERT(resp.decision == HOOK_CONTINUE, "default continue");
    TEST_PASS("hooks_init");
}

static void test_hooks_register_and_fire(void)
{
    struct hook_context ctx;
    struct hook_response resp;

    TEST_START("hooks_register_and_fire");
    hooks_init();

    ASSERT(hooks_register(HOOK_PRE_TOOL_USE, hook_pass) == 0,
           "register pass");
    memset(&ctx, 0, sizeof(ctx));
    ctx.event = HOOK_PRE_TOOL_USE;
    ASSERT(hooks_fire(&ctx, &resp) == 0, "fire pass");
    ASSERT(resp.decision == HOOK_CONTINUE, "continue");
    TEST_PASS("hooks_register_and_fire");
}

static void test_hooks_block(void)
{
    struct hook_context ctx;
    struct hook_response resp;

    TEST_START("hooks_block");
    hooks_init();

    ASSERT(hooks_register(HOOK_PRE_TOOL_USE, hook_pass) == 0,
           "register pass");
    ASSERT(hooks_register(HOOK_PRE_TOOL_USE, hook_block) == 0,
           "register block");

    memset(&ctx, 0, sizeof(ctx));
    ctx.event = HOOK_PRE_TOOL_USE;
    ASSERT(hooks_fire(&ctx, &resp) == 1, "fire blocked");
    ASSERT(resp.decision == HOOK_BLOCK, "block decision");
    ASSERT(strcmp(resp.message, "blocked by test hook") == 0,
           "block message");
    TEST_PASS("hooks_block");
}

static void test_hooks_error_handler_skipped(void)
{
    struct hook_context ctx;
    struct hook_response resp;

    TEST_START("hooks_error_handler_skipped");
    hooks_init();

    ASSERT(hooks_register(HOOK_PRE_TOOL_USE, hook_error) == 0,
           "register error");
    ASSERT(hooks_register(HOOK_PRE_TOOL_USE, hook_pass) == 0,
           "register pass");

    memset(&ctx, 0, sizeof(ctx));
    ctx.event = HOOK_PRE_TOOL_USE;
    ASSERT(hooks_fire(&ctx, &resp) == 0, "fire after error");
    ASSERT(resp.decision == HOOK_CONTINUE, "continue after error");
    TEST_PASS("hooks_error_handler_skipped");
}

static void test_hooks_different_events(void)
{
    struct hook_context ctx;
    struct hook_response resp;

    TEST_START("hooks_different_events");
    hooks_init();

    ASSERT(hooks_register(HOOK_PRE_TOOL_USE, hook_block) == 0,
           "register pre block");

    /* POST event should not be blocked */
    memset(&ctx, 0, sizeof(ctx));
    ctx.event = HOOK_POST_TOOL_USE;
    ASSERT(hooks_fire(&ctx, &resp) == 0, "fire post");
    ASSERT(resp.decision == HOOK_CONTINUE, "post continues");

    /* PRE event should be blocked */
    ctx.event = HOOK_PRE_TOOL_USE;
    ASSERT(hooks_fire(&ctx, &resp) == 1, "fire pre blocked");
    ASSERT(resp.decision == HOOK_BLOCK, "pre blocked");
    TEST_PASS("hooks_different_events");
}

/* ---- Permission tests ---- */

static void test_perm_standard_allows_read(void)
{
    struct permission_policy policy;

    TEST_START("perm_standard_allows_read");
    perm_set_default(&policy);

    ASSERT(perm_check_json(&policy, "read_file",
                            "{\"path\":\"/etc/test\"}") == 1,
           "read allowed");
    ASSERT(perm_check_json(&policy, "list_dir",
                            "{\"path\":\"/\"}") == 1,
           "list_dir allowed");
    ASSERT(perm_check_json(&policy, "get_system_info",
                            "{}") == 1,
           "system_info allowed");
    TEST_PASS("perm_standard_allows_read");
}

static void test_perm_standard_denies_protected_write(void)
{
    struct permission_policy policy;

    TEST_START("perm_standard_denies_protected_write");
    perm_set_default(&policy);

    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/boot/test\"}") == 0,
           "/boot write denied");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/etc/agent/conf\"}") == 0,
           "/etc/agent write denied");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/tmp/ok.txt\"}") == 1,
           "/tmp write allowed");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/var/agent/out.txt\"}") == 1,
           "/var/agent write allowed");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/home/user/out.txt\"}") == 1,
           "/home/user write allowed");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/home/out.txt\"}") == 0,
           "/home write denied by default");
    ASSERT(perm_check_json(&policy, "rename_path",
                            "{\"from\":\"/home/user/a.txt\","
                            "\"to\":\"/home/user/b.txt\"}") == 1,
           "rename /home/user allowed");
    ASSERT(perm_check_json(&policy, "rename_path",
                            "{\"from\":\"/home/user/a.txt\","
                            "\"to\":\"/boot/b.txt\"}") == 0,
           "rename into /boot denied");
    TEST_PASS("perm_standard_denies_protected_write");
}

static void test_perm_standard_normalizes_paths(void)
{
    struct permission_policy policy;

    TEST_START("perm_standard_normalizes_paths");
    perm_set_default(&policy);

    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/tmp/work/../ok.txt\"}") == 1,
           "normalized /tmp path allowed");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/tmp/../../boot/x\"}") == 1,
           "invalid normalized path is left to tool validation");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/tmp/../boot/x\"}") == 0,
           "normalized /boot path denied");
    TEST_PASS("perm_standard_normalizes_paths");
}

static void test_perm_standard_denies_dangerous_commands(void)
{
    struct permission_policy policy;

    TEST_START("perm_standard_denies_dangerous_commands");
    perm_set_default(&policy);

    ASSERT(perm_check_json(&policy, "run_command",
                            "{\"command\":\"rm -rf /\"}") == 0,
           "rm -rf denied");
    ASSERT(perm_check_json(&policy, "run_command",
                            "{\"command\":\"dd if=/dev/zero of=/dev/sda\"}") == 0,
           "dd denied");
    ASSERT(perm_check_json(&policy, "run_command",
                            "{\"command\":\"mkfs.ext3 /dev/sda\"}") == 0,
           "mkfs denied");
    ASSERT(perm_check_json(&policy, "run_command",
                            "{\"command\":\"ls /tmp\"}") == 1,
           "ls allowed");
    TEST_PASS("perm_standard_denies_dangerous_commands");
}

static void test_perm_strict_only_reads(void)
{
    struct permission_policy policy;

    TEST_START("perm_strict_only_reads");
    perm_init(&policy);
    policy.mode = PERM_STRICT;

    ASSERT(perm_check_json(&policy, "read_file", "{}") == 1,
           "read allowed in strict");
    ASSERT(perm_check_json(&policy, "list_dir", "{}") == 1,
           "list_dir allowed in strict");
    ASSERT(perm_check_json(&policy, "get_system_info", "{}") == 1,
           "system_info allowed in strict");
    ASSERT(perm_check_json(&policy, "write_file", "{}") == 0,
           "write denied in strict");
    ASSERT(perm_check_json(&policy, "rename_path", "{}") == 0,
           "rename denied in strict");
    ASSERT(perm_check_json(&policy, "run_command", "{}") == 0,
           "run_command denied in strict");
    TEST_PASS("perm_strict_only_reads");
}

static void test_perm_permissive_allows_all(void)
{
    struct permission_policy policy;

    TEST_START("perm_permissive_allows_all");
    perm_init(&policy);
    policy.mode = PERM_PERMISSIVE;

    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/boot/kernel\"}") == 1,
           "write /boot in permissive");
    ASSERT(perm_check_json(&policy, "run_command",
                            "{\"command\":\"rm -rf /\"}") == 1,
           "rm -rf in permissive");
    TEST_PASS("perm_permissive_allows_all");
}

static void test_perm_load_policy_prefix_rules(void)
{
    struct permission_policy policy;
    FILE *fp;

    TEST_START("perm_load_policy_prefix_rules");
    system("mkdir -p /tmp/agent_test_audit 2>/dev/null");
    fp = fopen("/tmp/agent_test_audit/permissions.conf", "w");
    ASSERT(fp != NULL, "open temp policy");
    fputs("mode=standard\n", fp);
    fputs("read_allow=/tmp/\n", fp);
    fputs("read_deny=/tmp/private/\n", fp);
    fputs("write_allow=/tmp/work/\n", fp);
    fputs("write_deny=/tmp/work/blocked/\n", fp);
    fclose(fp);

    ASSERT(perm_load_policy(&policy, "/tmp/agent_test_audit/permissions.conf") == 0,
           "load policy");
    ASSERT(perm_check_json(&policy, "read_file",
                            "{\"path\":\"/tmp/file.txt\"}") == 1,
           "read allow works");
    ASSERT(perm_check_json(&policy, "read_file",
                            "{\"path\":\"/tmp/private/secret.txt\"}") == 0,
           "read deny works");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/tmp/work/out.txt\"}") == 1,
           "write allow works");
    ASSERT(perm_check_json(&policy, "write_file",
                            "{\"path\":\"/tmp/work/blocked/out.txt\"}") == 0,
           "write deny works");
    TEST_PASS("perm_load_policy_prefix_rules");
}

/* ---- Audit tests ---- */

static void reset_audit(void)
{
    system("mkdir -p /tmp/agent_test_audit 2>/dev/null");
    system("rm -f " AUDIT_LOG_PATH);
}

static void test_audit_write_and_read(void)
{
    struct audit_entry entry;
    struct audit_entry entries[5];
    int count = 0;

    TEST_START("audit_write_and_read");
    reset_audit();
    audit_init();

    memset(&entry, 0, sizeof(entry));
    entry.timestamp = 100;
    strcpy(entry.session_id, "sess01");
    entry.step = 1;
    strcpy(entry.tool_name, "read_file");
    strcpy(entry.action, "execute");
    strcpy(entry.detail, "/tmp/test.txt");
    ASSERT(audit_log(&entry) == 0, "log entry 1");

    entry.timestamp = 200;
    entry.step = 2;
    strcpy(entry.tool_name, "write_file");
    strcpy(entry.action, "blocked");
    strcpy(entry.detail, "/boot/kernel");
    ASSERT(audit_log(&entry) == 0, "log entry 2");

    ASSERT(audit_read_last(entries, 5, &count) == 0, "read");
    ASSERT(count == 2, "2 entries");
    ASSERT(strcmp(entries[0].tool_name, "read_file") == 0, "first tool");
    ASSERT(strcmp(entries[1].action, "blocked") == 0, "second action");
    ASSERT(entries[1].step == 2, "second step");
    TEST_PASS("audit_write_and_read");
}

static void test_audit_rotate(void)
{
    struct audit_entry entry;
    int i;

    TEST_START("audit_rotate");
    reset_audit();
    audit_init();

    /* Write enough entries to exceed 512 bytes */
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.session_id, "sess01");
    strcpy(entry.tool_name, "read_file");
    strcpy(entry.action, "execute");
    strcpy(entry.detail, "long detail text for padding");

    for (i = 0; i < 20; i++) {
        entry.timestamp = i;
        entry.step = i;
        audit_log(&entry);
    }

    ASSERT(audit_rotate(512) == 1, "rotation happened");

    {
        struct audit_entry entries[20];
        int count = 0;
        audit_read_last(entries, 20, &count);
        ASSERT(count > 0 && count < 20, "reduced entries");
    }
    TEST_PASS("audit_rotate");
}

int main(void)
{
    printf("=== hooks, permissions, and audit tests ===\n\n");

    /* Hook tests */
    test_hooks_init();
    test_hooks_register_and_fire();
    test_hooks_block();
    test_hooks_error_handler_skipped();
    test_hooks_different_events();

    /* Permission tests */
    test_perm_standard_allows_read();
    test_perm_standard_denies_protected_write();
    test_perm_standard_normalizes_paths();
    test_perm_standard_denies_dangerous_commands();
    test_perm_strict_only_reads();
    test_perm_permissive_allows_all();
    test_perm_load_policy_prefix_rules();

    /* Audit tests */
    test_audit_write_and_read();
    test_audit_rotate();

    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
