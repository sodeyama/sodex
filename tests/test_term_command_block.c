/*
 * test_term_command_block.c - command proposal block tests
 */

#include <stdio.h>
#include <string.h>
#include "agent/term_command_block.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    failed++; \
    return; \
  } \
} while (0)

#define TEST_START(name) printf("  [TEST] %s ... ", name)
#define TEST_PASS() do { printf("PASS\n"); passed++; } while (0)

static void test_classify_read_write_network(void)
{
  TEST_START("classify_read_write_network");
  ASSERT(term_command_block_classify("find specs -name '*.md'") ==
         TERM_COMMAND_CLASS_READ_ONLY,
         "find should be read-only");
  ASSERT(term_command_block_classify("echo hi > /tmp/x") ==
         TERM_COMMAND_CLASS_WRITE,
         "redirect should be write");
  ASSERT(term_command_block_classify("curl http://example.com") ==
         TERM_COMMAND_CLASS_NETWORK,
         "curl should be network");
  TEST_PASS();
}

static void test_pending_block_formats_actions(void)
{
  struct term_command_block block;
  char out[512];

  TEST_START("pending_block_formats_actions");
  term_command_block_init(&block);
  term_command_block_set_proposal(&block, "grep -R agent specs");
  ASSERT(term_command_block_format(&block, out, sizeof(out)) > 0,
         "format should succeed");
  ASSERT(strstr(out, "proposal=pending") != 0,
         "pending state should be rendered");
  ASSERT(strstr(out, "/approve once") != 0,
         "approval actions should be rendered");
  TEST_PASS();
}

static void test_done_block_keeps_summary(void)
{
  struct term_command_block block;
  char out[512];

  TEST_START("done_block_keeps_summary");
  term_command_block_init(&block);
  term_command_block_set_proposal(&block, "find README.md");
  term_command_block_mark_done(&block, 0, "approved exit=0 README.md");
  ASSERT(term_command_block_format(&block, out, sizeof(out)) > 0,
         "format should succeed");
  ASSERT(strstr(out, "proposal=done") != 0,
         "done state should be rendered");
  ASSERT(strstr(out, "approved exit=0") != 0,
         "summary should be rendered");
  TEST_PASS();
}

int main(void)
{
  printf("=== term command block tests ===\n");
  test_classify_read_write_network();
  test_pending_block_formats_actions();
  test_done_block_keeps_summary();
  printf("\n--- Results: %d/%d passed ---\n", passed, passed + failed);
  return failed ? 1 : 0;
}
