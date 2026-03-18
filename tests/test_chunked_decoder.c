/*
 * Host-side unit test for curl's chunked transfer encoding decoder.
 * Tests parse_chunk_size() and chunked_decode() logic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Stub out sodex-specific macros */
#define PRIVATE static
#define PUBLIC

/* Re-implement the chunked decoder functions inline for testing */
struct chunked_state {
    int in_chunk;
    int chunk_remain;
    int done;
};

static int parse_chunk_size(const char *buf, int len, int *chunk_size)
{
    int i = 0;
    int val = 0;
    char c;

    while (i < len) {
        c = buf[i];
        if (c >= '0' && c <= '9')
            val = val * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            val = val * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            val = val * 16 + (c - 'A' + 10);
        else
            break;
        i++;
    }
    if (i == 0)
        return 0;

    while (i < len && buf[i] != '\r')
        i++;

    if (i + 1 >= len)
        return 0;
    if (buf[i] == '\r' && buf[i + 1] == '\n') {
        *chunk_size = val;
        return i + 2;
    }
    return 0;
}

/* For testing, write decoded output to a buffer instead of fd */
static char g_output[65536];
static int g_output_len = 0;

/* Override write() for testing */
static ssize_t test_write(int fd, const void *buf, size_t count)
{
    (void)fd;
    if (g_output_len + (int)count > (int)sizeof(g_output))
        count = sizeof(g_output) - g_output_len;
    memcpy(g_output + g_output_len, buf, count);
    g_output_len += count;
    return count;
}

#define write test_write

static int chunked_decode(struct chunked_state *st, const char *buf, int len,
                          int out_fd)
{
    int pos = 0;
    int written = 0;

    while (pos < len && !st->done) {
        if (st->in_chunk) {
            int avail = len - pos;
            int take = (avail < st->chunk_remain) ? avail : st->chunk_remain;
            if (take > 0) {
                if (out_fd >= 0)
                    write(out_fd, buf + pos, take);
                else
                    write(1, buf + pos, take);
                written += take;
                pos += take;
                st->chunk_remain -= take;
            }
            if (st->chunk_remain == 0) {
                if (pos + 1 < len && buf[pos] == '\r' && buf[pos + 1] == '\n')
                    pos += 2;
                else if (pos < len && buf[pos] == '\r')
                    pos += 1;
                st->in_chunk = 0;
            }
        } else {
            int chunk_size = 0;
            int consumed = parse_chunk_size(buf + pos, len - pos, &chunk_size);
            if (consumed == 0)
                break;
            pos += consumed;
            if (chunk_size == 0) {
                st->done = 1;
                break;
            }
            st->chunk_remain = chunk_size;
            st->in_chunk = 1;
        }
    }
    return written;
}

#undef write

static int passed = 0;
static int failed = 0;

#define ASSERT_EQ(a, b, name) do { \
    if ((a) == (b)) { printf("  [PASS] %s\n", name); passed++; } \
    else { printf("  [FAIL] %s: expected %d, got %d\n", name, (int)(b), (int)(a)); failed++; } \
} while(0)

#define ASSERT_STREQ(a, b, len, name) do { \
    if (memcmp(a, b, len) == 0) { printf("  [PASS] %s\n", name); passed++; } \
    else { printf("  [FAIL] %s: string mismatch\n", name); failed++; } \
} while(0)

static void test_parse_chunk_size(void)
{
    int size, consumed;

    consumed = parse_chunk_size("f\r\n", 3, &size);
    ASSERT_EQ(consumed, 3, "parse_chunk_size: f");
    ASSERT_EQ(size, 15, "parse_chunk_size: f=15");

    consumed = parse_chunk_size("f36\r\n", 5, &size);
    ASSERT_EQ(consumed, 5, "parse_chunk_size: f36");
    ASSERT_EQ(size, 0xf36, "parse_chunk_size: f36=3894");

    consumed = parse_chunk_size("0\r\n", 3, &size);
    ASSERT_EQ(consumed, 3, "parse_chunk_size: 0");
    ASSERT_EQ(size, 0, "parse_chunk_size: 0=0");

    consumed = parse_chunk_size("FF\r\n", 4, &size);
    ASSERT_EQ(consumed, 4, "parse_chunk_size: FF");
    ASSERT_EQ(size, 255, "parse_chunk_size: FF=255");

    /* Incomplete */
    consumed = parse_chunk_size("f\r", 2, &size);
    ASSERT_EQ(consumed, 0, "parse_chunk_size: incomplete");

    /* With chunk extension */
    consumed = parse_chunk_size("a;ext=val\r\n", 11, &size);
    ASSERT_EQ(consumed, 11, "parse_chunk_size: with extension");
    ASSERT_EQ(size, 10, "parse_chunk_size: a=10");
}

static void test_simple_chunked(void)
{
    struct chunked_state st;
    memset(&st, 0, sizeof(st));
    g_output_len = 0;

    const char *input = "f\r\n<!DOCTYPE html>\r\n0\r\n\r\n";
    int len = strlen(input);
    int written = chunked_decode(&st, input, len, -1);

    ASSERT_EQ(written, 15, "simple_chunked: 15 bytes written");
    ASSERT_EQ(st.done, 1, "simple_chunked: done flag set");
    ASSERT_STREQ(g_output, "<!DOCTYPE html>", 15, "simple_chunked: correct output");
}

static void test_multi_chunk(void)
{
    struct chunked_state st;
    memset(&st, 0, sizeof(st));
    g_output_len = 0;

    const char *input = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
    int len = strlen(input);
    int written = chunked_decode(&st, input, len, -1);

    ASSERT_EQ(written, 11, "multi_chunk: 11 bytes");
    ASSERT_EQ(st.done, 1, "multi_chunk: done");
    ASSERT_STREQ(g_output, "Hello World", 11, "multi_chunk: correct");
}

static void test_split_across_calls(void)
{
    struct chunked_state st;
    memset(&st, 0, sizeof(st));
    g_output_len = 0;

    /* First call: chunk size + partial data */
    const char *part1 = "a\r\n0123456";
    chunked_decode(&st, part1, strlen(part1), -1);
    ASSERT_EQ(st.in_chunk, 1, "split: still in chunk after part1");
    ASSERT_EQ(st.chunk_remain, 3, "split: 3 bytes remain");

    /* Second call: rest of data + next chunk */
    const char *part2 = "789\r\n0\r\n\r\n";
    chunked_decode(&st, part2, strlen(part2), -1);
    ASSERT_EQ(st.done, 1, "split: done after part2");
    ASSERT_EQ(g_output_len, 10, "split: 10 bytes total");
    ASSERT_STREQ(g_output, "0123456789", 10, "split: correct output");
}

static void test_yahoo_pattern(void)
{
    /* Simulates the pattern from the issue: chunk sizes appearing in output */
    struct chunked_state st;
    memset(&st, 0, sizeof(st));
    g_output_len = 0;

    const char *input =
        "f\r\n<!DOCTYPE html>\r\n"
        "1a\r\n<html lang=\"ja\">ABCDEFGHIJ\r\n"
        "0\r\n\r\n";
    int len = strlen(input);
    int written = chunked_decode(&st, input, len, -1);

    /* Should NOT contain "f" or "1a" chunk markers */
    ASSERT_EQ(written, 15 + 26, "yahoo: correct byte count");
    ASSERT_EQ(st.done, 1, "yahoo: done");

    /* Verify no chunk size leaks into output */
    int has_marker = 0;
    if (g_output_len >= 2 && g_output[0] == 'f' && g_output[1] == '\r')
        has_marker = 1;
    ASSERT_EQ(has_marker, 0, "yahoo: no chunk markers in output");
}

int main(void)
{
    printf("=== Chunked Transfer Encoding Decoder Tests ===\n");
    test_parse_chunk_size();
    test_simple_chunked();
    test_multi_chunk();
    test_split_across_calls();
    test_yahoo_pattern();

    printf("\n--- Results: %d/%d passed ---\n", passed, passed + failed);
    if (failed > 0) {
        printf("=== SOME TESTS FAILED ===\n");
        return 1;
    }
    printf("=== ALL TESTS PASSED ===\n");
    return 0;
}
