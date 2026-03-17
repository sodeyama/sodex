/*
 * ask.c - Claude API client command
 *
 * Sends a prompt to the Claude Messages API via HTTPS SSE streaming
 * and prints the streamed response to the console.
 *
 * API key is read from /etc/claude.conf at runtime.
 *
 * Usage: ask <prompt>
 *   ask "What is the capital of Japan?"
 *   ask Tell me a joke
 *
 * The API key file (/etc/claude.conf) must contain a single line:
 *   sk-ant-api03-...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <entropy.h>
#include <agent/claude_adapter.h>
#include <agent/llm_provider.h>
#include <agent/claude_client.h>

#define API_KEY_PATH  "/etc/claude.conf"
#define API_KEY_MAX   256
#define PROMPT_MAX    2048

/* Read API key from /etc/claude.conf. Returns key length, or -1 on error. */
PRIVATE int read_api_key(char *buf, int cap)
{
    int fd;
    int n;
    int i;

    fd = open(API_KEY_PATH, 0, 0);
    if (fd < 0) {
        printf("ask: cannot open %s\n", API_KEY_PATH);
        printf("  Create .env.local with ANTHROPIC_API_KEY=sk-ant-...\n");
        printf("  then run: make inject-api-key\n");
        return -1;
    }

    n = read(fd, buf, cap - 1);
    close(fd);

    if (n <= 0) {
        printf("ask: %s is empty\n", API_KEY_PATH);
        return -1;
    }

    buf[n] = '\0';

    /* Strip trailing whitespace / newline */
    for (i = n - 1; i >= 0; i--) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')
            buf[i] = '\0';
        else
            break;
    }

    n = strlen(buf);
    if (n < 10) {
        printf("ask: API key too short (%d chars)\n", n);
        return -1;
    }

    return n;
}

/* Join argv[1..argc-1] into a single prompt string */
PRIVATE int build_prompt(int argc, char *argv[], char *buf, int cap)
{
    int pos = 0;
    int i;

    for (i = 1; i < argc; i++) {
        int len = strlen(argv[i]);
        if (pos + len + 1 >= cap)
            break;
        if (pos > 0)
            buf[pos++] = ' ';
        memcpy(buf + pos, argv[i], len);
        pos += len;
    }
    buf[pos] = '\0';
    return pos;
}

int main(int argc, char *argv[])
{
    char api_key[API_KEY_MAX];
    char prompt[PROMPT_MAX];
    struct claude_response resp;
    int ret;
    int i;

    if (argc < 2) {
        printf("Usage: ask <prompt>\n");
        printf("  ask What is the capital of Japan?\n");
        return 1;
    }

    /* Read API key */
    if (read_api_key(api_key, sizeof(api_key)) < 0)
        return 1;

    /* Build prompt from arguments */
    if (build_prompt(argc, argv, prompt, sizeof(prompt)) == 0) {
        printf("ask: empty prompt\n");
        return 1;
    }

    /* Initialize entropy + PRNG for TLS */
    entropy_init();
    entropy_collect_jitter(512);
    if (prng_init() < 0) {
        printf("ask: PRNG init failed\n");
        return 1;
    }

    debug_printf("[ASK] prompt: \"%s\"\n", prompt);
    printf("Asking Claude...\n\n");

    /* Send message via real Claude API */
    ret = claude_send_message_with_key(
        &provider_claude,
        prompt,
        api_key,
        &resp
    );

    if (ret != CLAUDE_OK) {
        printf("\nask: API error (code %d)\n", ret);
        debug_printf("[ASK] error: %d\n", ret);
        return 1;
    }

    /* Print response */
    for (i = 0; i < resp.block_count; i++) {
        if (resp.blocks[i].type == CLAUDE_CONTENT_TEXT) {
            write(1, resp.blocks[i].text.text,
                  resp.blocks[i].text.text_len);
        } else if (resp.blocks[i].type == CLAUDE_CONTENT_TOOL_USE) {
            printf("[tool_use: %s(%s)]\n",
                   resp.blocks[i].tool_use.name,
                   resp.blocks[i].tool_use.input_json);
        }
    }
    printf("\n");

    /* Print usage info */
    debug_printf("[ASK] stop=%d tokens=%d/%d\n",
                resp.stop_reason, resp.input_tokens, resp.output_tokens);

    return 0;
}
