/*
 * agent_loop.c - Autonomous agent loop implementation
 *
 * Core loop: prompt -> Claude API -> tool execution -> repeat until done.
 * Uses claude_send_conversation() for multi-turn conversations with
 * tool definitions, and tool_dispatch() for executing tool calls.
 */

#include <agent/agent.h>
#include <agent/tool_dispatch.h>
#include <agent/tool_handlers.h>
#include <agent/claude_client.h>
#include <agent/llm_provider.h>
#include <agent/memory_store.h>
#include <agent/tool_registry.h>
#include <agent/hooks.h>
#include <agent/permissions.h>
#include <agent/audit.h>
#include <json.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#include <stdlib.h>  /* for open/read/close */
#include <fs.h>      /* for O_RDONLY */
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

#ifndef TEST_BUILD
#define AGENT_CLAUDE_PATH_MAX   PATHNAME_MAX
#define AGENT_INSTRUCTION_PATH  AGENT_USER_CLAUDE_PATH
#define AGENT_PROJECT_CLAUDE    "CLAUDE.md"
#define AGENT_MEMORY_DIR        "/var/agent/memory"
#define AGENT_GLOBAL_MEMORY     AGENT_MEMORY_DIR "/global.md"
#endif

/* Default system prompt (hardcoded fallback) */
static const char DEFAULT_SYSTEM_PROMPT[] =
    "You are Sodex OS system agent. Sodex is a custom i486 OS kernel. "
    "You can use tools to read/write files, list directories, get system info, "
    "and run commands. Be concise and helpful.";

static agent_event_fn s_event_callback = (agent_event_fn)0;
static void *s_event_userdata = (void *)0;
static const char FRESH_LOOKUP_PROMPT_PREFIX[] =
    "これは時間変動する可能性が高い依頼です。"
    "推測で終えず、少なくとも1回は tool を使って確認してください。"
    "URL が未確定なら run_command で websearch、"
    "URL が確定しているなら fetch_url を優先し、"
    "source URL を含めて回答してください。\n\n"
    "ユーザー依頼:\n";

PRIVATE void emit_agent_event(const struct agent_event *event)
{
    if (!event || !s_event_callback)
        return;
    s_event_callback(event, s_event_userdata);
}

static int text_contains_any(const char *text, const char *const *needles)
{
    int i;

    if (!text || !needles)
        return 0;
    for (i = 0; needles[i] != 0; i++) {
        if (strstr(text, needles[i]) != 0)
            return 1;
    }
    return 0;
}

static int prompt_needs_fresh_lookup(const char *user_prompt)
{
    static const char *ja_needles[] = {
        "天気", "気温", "予報", "最新", "現在", "今日",
        "ニュース", "株価", "為替", "速報", 0
    };
    static const char *en_needles[] = {
        "weather", "forecast", "latest", "current",
        "today", "news", "stock", "price", 0
    };

    if (!user_prompt || !*user_prompt)
        return 0;
    if (text_contains_any(user_prompt, ja_needles))
        return 1;
    return text_contains_any(user_prompt, en_needles);
}

static const char *build_effective_user_prompt(const char *user_prompt,
                                               char *buf, int cap)
{
    int prefix_len;
    int prompt_len;

    if (!user_prompt)
        return user_prompt;
    if (!prompt_needs_fresh_lookup(user_prompt))
        return user_prompt;
    if (!buf || cap <= 1)
        return user_prompt;

    prefix_len = strlen(FRESH_LOOKUP_PROMPT_PREFIX);
    prompt_len = strlen(user_prompt);
    if (prefix_len + prompt_len + 1 >= cap)
        return user_prompt;

    memcpy(buf, FRESH_LOOKUP_PROMPT_PREFIX, (size_t)prefix_len);
    memcpy(buf + prefix_len, user_prompt, (size_t)prompt_len);
    buf[prefix_len + prompt_len] = '\0';
    return buf;
}

#ifndef TEST_BUILD
static int prompt_append_chunk(struct agent_config *config,
                               const char *text, int text_len)
{
    int remaining;

    if (!config || !text)
        return 0;
    if (text_len < 0)
        text_len = strlen(text);

    remaining = AGENT_MAX_SYSTEM_PROMPT - config->system_prompt_len - 1;
    if (remaining <= 0)
        return 0;
    if (text_len > remaining) {
        text_len = remaining;
        /* Back up to avoid splitting a multi-byte UTF-8 sequence */
        while (text_len > 0 &&
               ((unsigned char)text[text_len] & 0xC0) == 0x80)
            text_len--;
    }

    memcpy(config->system_prompt + config->system_prompt_len, text, text_len);
    config->system_prompt_len += text_len;
    config->system_prompt[config->system_prompt_len] = '\0';
    return text_len;
}

static int build_dentry_path(ext3_dentry *dentry, char *buf, int cap)
{
    int pos;
    int name_len;

    if (!dentry || !buf || cap <= 1)
        return -1;

    if (dentry->d_parent == 0 ||
        (dentry->d_namelen == 1 && dentry->d_name[0] == '/')) {
        buf[0] = '/';
        buf[1] = '\0';
        return 1;
    }

    pos = build_dentry_path(dentry->d_parent, buf, cap);
    if (pos < 0)
        return -1;

    if (pos > 1) {
        if (pos >= cap - 1)
            return -1;
        buf[pos++] = '/';
        buf[pos] = '\0';
    }

    name_len = dentry->d_namelen;
    if (name_len <= 0)
        return pos;
    if (pos + name_len >= cap)
        name_len = cap - pos - 1;
    if (name_len <= 0)
        return -1;

    memcpy(buf + pos, dentry->d_name, name_len);
    pos += name_len;
    buf[pos] = '\0';
    return pos;
}

static int build_current_path(char *buf, int cap)
{
    ext3_dentry *dentry;

    if (!buf || cap <= 1)
        return -1;

    dentry = (ext3_dentry *)getdentry();
    if (!dentry)
        return -1;
    return build_dentry_path(dentry, buf, cap);
}

static int build_project_claude_path(char *buf, int cap)
{
    static char cwd[AGENT_CLAUDE_PATH_MAX];
    int cwd_len;
    int file_len;

    if (!buf || cap <= 1)
        return -1;

    cwd_len = build_current_path(cwd, sizeof(cwd));
    if (cwd_len < 0)
        return -1;

    file_len = strlen(AGENT_PROJECT_CLAUDE);
    if (strcmp(cwd, "/") == 0) {
        if (file_len + 2 > cap)
            return -1;
        buf[0] = '/';
        memcpy(buf + 1, AGENT_PROJECT_CLAUDE, file_len);
        buf[file_len + 1] = '\0';
        return file_len + 1;
    }

    if (cwd_len + file_len + 2 > cap)
        return -1;

    memcpy(buf, cwd, cwd_len);
    buf[cwd_len] = '/';
    memcpy(buf + cwd_len + 1, AGENT_PROJECT_CLAUDE, file_len);
    buf[cwd_len + file_len + 1] = '\0';
    return cwd_len + file_len + 1;
}

static unsigned int hash_path(const char *path)
{
    unsigned int hash = 5381U;

    if (!path)
        return 0U;
    while (*path) {
        hash = ((hash << 5) + hash) ^ (unsigned int)(unsigned char)(*path);
        path++;
    }
    return hash;
}

static int path_join(char *dst, int cap, const char *dir, const char *name)
{
    int dir_len;
    int name_len;

    if (!dst || !dir || !name || cap <= 1)
        return -1;

    dir_len = strlen(dir);
    name_len = strlen(name);
    if (strcmp(dir, "/") == 0) {
        if (name_len + 2 > cap)
            return -1;
        dst[0] = '/';
        memcpy(dst + 1, name, name_len);
        dst[name_len + 1] = '\0';
        return name_len + 1;
    }

    if (dir_len + name_len + 2 > cap)
        return -1;
    memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, name, name_len);
    dst[dir_len + name_len + 1] = '\0';
    return dir_len + name_len + 1;
}

static int trim_to_parent(char *path)
{
    int len;

    if (!path)
        return -1;
    len = strlen(path);
    if (len <= 1)
        return -1;

    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    while (len > 1 && path[len - 1] != '/') {
        path[len - 1] = '\0';
        len--;
    }
    if (len > 1)
        path[len - 1] = '\0';
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
    return 0;
}

static int build_workspace_memory_path(char *buf, int cap)
{
    static char cwd[AGENT_CLAUDE_PATH_MAX];
    unsigned int hash;

    if (!buf || cap <= 1)
        return -1;
    if (build_current_path(cwd, sizeof(cwd)) < 0)
        return -1;

    hash = hash_path(cwd);
    return snprintf(buf, cap, "%s/%08x.md", AGENT_MEMORY_DIR, hash);
}

static int read_text_file(const char *path, char *buf, int cap)
{
    int fd;
    int n;

    if (!path || !buf || cap <= 1)
        return -1;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    n = read(fd, buf, cap - 1);
    close(fd);
    if (n <= 0)
        return -1;

    while (n > 0 &&
           (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        n--;

    buf[n] = '\0';
    return n;
}

static void append_instruction_file(struct agent_config *config,
                                    const char *path,
                                    const char *label)
{
    static char header[160];
    static char body[AGENT_MAX_SYSTEM_PROMPT];
    int body_len;
    int header_len;
    int appended;

    body_len = read_text_file(path, body, sizeof(body));
    if (body_len <= 0)
        return;

    header_len = snprintf(header, sizeof(header), "\n\n== %s ==\n", label);
    if (header_len > 0)
        prompt_append_chunk(config, header, header_len);

    appended = prompt_append_chunk(config, body, body_len);
    debug_printf("[AGENT] loaded %s: %d bytes\n", path, body_len);
    if (appended < body_len) {
        debug_printf("[AGENT] truncated %s to %d/%d bytes\n",
                     path, appended, body_len);
    }
}

static void append_instruction_files(struct agent_config *config)
{
    static char project_path[AGENT_CLAUDE_PATH_MAX];

    append_instruction_file(config,
                            AGENT_INSTRUCTION_PATH,
                            "User Scope Instructions (/etc/CLAUDE.md)");

    if (build_project_claude_path(project_path, sizeof(project_path)) >= 0) {
        append_instruction_file(config,
                                project_path,
                                "Project Scope Instructions (./CLAUDE.md)");
    }
}

static void append_parent_memory_files(struct agent_config *config)
{
    static const char *names[] = {
        "AGENTS.md",
        "AGENTS.local.md",
        "CLAUDE.local.md",
        0
    };
    static char scan[AGENT_CLAUDE_PATH_MAX];
    int is_first = 1;

    if (build_current_path(scan, sizeof(scan)) < 0)
        return;

    for (;;) {
        int idx;

        for (idx = 0; names[idx] != 0; idx++) {
            static char path[AGENT_CLAUDE_PATH_MAX];

            if (path_join(path, sizeof(path), scan, names[idx]) >= 0)
                append_instruction_file(config, path, names[idx]);
        }

        if (!is_first) {
            static char path[AGENT_CLAUDE_PATH_MAX];

            if (path_join(path, sizeof(path), scan, "CLAUDE.md") >= 0)
                append_instruction_file(config, path, "CLAUDE.md");
        }

        if (strcmp(scan, "/") == 0)
            break;
        if (trim_to_parent(scan) < 0)
            break;
        is_first = 0;
    }
}

static void append_memory_files(struct agent_config *config)
{
    static char workspace_path[AGENT_CLAUDE_PATH_MAX];

    append_instruction_file(config,
                            AGENT_GLOBAL_MEMORY,
                            "Global Memory");
    append_parent_memory_files(config);
    if (build_workspace_memory_path(workspace_path, sizeof(workspace_path)) >= 0)
        append_instruction_file(config, workspace_path, "Workspace Memory");
}
#endif

void agent_set_event_callback(agent_event_fn callback, void *userdata)
{
    s_event_callback = callback;
    s_event_userdata = userdata;
}

void agent_config_init(struct agent_config *config)
{
    memset(config, 0, sizeof(*config));
    config->model = "claude-sonnet-4-20250514";
    config->max_steps = AGENT_DEFAULT_MAX_STEPS;
    config->max_tokens_per_turn = AGENT_DEFAULT_MAX_TOKENS;
    config->terminal_tool = (const char *)0;
    config->api_key = (const char *)0;
    config->provider = (const struct llm_provider *)0;

    /* Copy default system prompt */
    {
        int len = strlen(DEFAULT_SYSTEM_PROMPT);
        if (len >= AGENT_MAX_SYSTEM_PROMPT)
            len = AGENT_MAX_SYSTEM_PROMPT - 1;
        memcpy(config->system_prompt, DEFAULT_SYSTEM_PROMPT, len);
        config->system_prompt[len] = '\0';
        config->system_prompt_len = len;
    }
}

int agent_load_config(struct agent_config *config)
{
#ifndef TEST_BUILD
    int loaded = 0;
    int fd;

    /* Try to read /etc/agent/system_prompt.txt */
    fd = open("/etc/agent/system_prompt.txt", O_RDONLY, 0);
    if (fd >= 0) {
        int n = read(fd, config->system_prompt, AGENT_MAX_SYSTEM_PROMPT - 1);
        close(fd);
        if (n > 0) {
            config->system_prompt[n] = '\0';
            config->system_prompt_len = n;
            debug_printf("[AGENT] loaded system prompt: %d bytes\n", n);
            loaded = 1;
        }
    }
    if (!loaded)
        debug_printf("[AGENT] using default system prompt\n");

    append_instruction_files(config);
    append_memory_files(config);
#endif
    return 0;
}

/* Extract final text from the last response */
static void extract_final_text(const struct claude_response *resp,
                                struct agent_result *result)
{
    int i;
    result->final_text[0] = '\0';
    result->final_text_len = 0;

    for (i = 0; i < resp->block_count; i++) {
        if (resp->blocks[i].type == CLAUDE_CONTENT_TEXT) {
            int len = resp->blocks[i].text.text_len;
            if (len >= AGENT_MAX_RESPONSE)
                len = AGENT_MAX_RESPONSE - 1;
            memcpy(result->final_text, resp->blocks[i].text.text, len);
            result->final_text[len] = '\0';
            result->final_text_len = len;
            break;  /* Take the first text block */
        }
    }
}

/* Fill common result fields from state */
static void fill_result(struct agent_result *result,
                         const struct agent_state *state,
                         enum agent_stop_condition reason,
                         int steps)
{
    result->stop_reason = reason;
    result->steps_executed = steps;
    result->total_input_tokens = state->conv.total_input_tokens;
    result->total_output_tokens = state->conv.total_output_tokens;
    result->total_tool_calls = state->total_tool_executions;
}

/* Send the current conversation to Claude via the existing API */
static int send_conversation(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp)
{
    int tc = tool_count();
    const struct llm_provider *prov;

    prov = config->provider ? config->provider : &provider_claude;

    claude_response_init(resp);
    return claude_send_conversation_with_key(prov, &state->conv,
                                              (tc > 0) ? 1 : 0,
                                              config->api_key, resp);
}

int agent_step(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp)
{
    claude_response_init(resp);
    return send_conversation(config, state, resp);
}

void agent_state_init(struct agent_state *state,
                       const struct agent_config *config)
{
    if (!state)
        return;

    memset(state, 0, sizeof(*state));
    if (config)
        conv_init(&state->conv, config->system_prompt);
    else
        conv_init(&state->conv, DEFAULT_SYSTEM_PROMPT);
}

/* AT-98: Builtin hook: block writes to protected paths */
static int builtin_path_protection(const struct hook_context *ctx,
                                    struct hook_response *response)
{
    if (!ctx || !response)
        return -1;

    /* Only check write_file tool */
    if (!ctx->tool_name || strcmp(ctx->tool_name, "write_file") != 0)
        return 0;

    if (ctx->tool_input_json && ctx->tool_input_len > 0) {
        /* Check for protected paths: /boot/ and /etc/agent/ */
        if (strstr(ctx->tool_input_json, "\"/boot/") ||
            strstr(ctx->tool_input_json, "\"/etc/agent/")) {
            response->decision = HOOK_BLOCK;
            snprintf(response->message, sizeof(response->message),
                     "write to protected path is not allowed");
            return 0;
        }
    }
    return 0;
}

static void agent_register_builtin_hooks(void)
{
    hooks_register(HOOK_PRE_TOOL_USE, builtin_path_protection);
}

static int agent_run_loop(
    const struct agent_config *config,
    struct agent_state *state,
    struct agent_result *result)
{
    /* 応答は大きいので static に逃がす */
    static struct claude_response resp;
    int step;
    int ret;

    if (!config || !state || !result)
        return -1;

    memset(result, 0, sizeof(*result));
    tool_init();
    hooks_init();
    agent_register_builtin_hooks();

    /* Main loop */
    for (step = 0; step < config->max_steps; step++) {
        struct agent_event event;

        state->current_step = step;
        debug_printf("[AGENT] step %d/%d\n", step + 1, config->max_steps);

        memset(&event, 0, sizeof(event));
        event.type = AGENT_EVENT_STEP_START;
        event.step = step + 1;
        emit_agent_event(&event);

        /* Brief delay between steps to let TCP/TLS state settle */
        if (step > 0) {
            volatile int delay;
            for (delay = 0; delay < 500000; delay++)
                ;
        }

        /* Send conversation to Claude */
        ret = agent_step(config, state, &resp);
        if (ret != 0) {
            debug_printf("[AGENT] API error: %d\n", ret);
            fill_result(result, state, AGENT_STOP_ERROR, step + 1);
            debug_printf("[AGENT] === Agent Run End (error) ===\n");
            return -1;
        }

        state->total_api_calls++;
        conv_update_tokens(&state->conv, &resp);

        /* Add assistant response to conversation */
        conv_add_assistant_response(&state->conv, &resp);

        /* Check stop reason */
        switch (resp.stop_reason) {
        case CLAUDE_STOP_END_TURN:
            /* Natural completion */
            extract_final_text(&resp, result);
            fill_result(result, state, AGENT_STOP_END_TURN, step + 1);
            debug_printf("[AGENT] completed: %d steps, %d tokens, %d tool calls\n",
                        step + 1,
                        state->conv.total_input_tokens +
                            state->conv.total_output_tokens,
                        state->total_tool_executions);
            agent_print_summary(result);
            debug_printf("[AGENT] === Agent Run End ===\n");
            return 0;

        case CLAUDE_STOP_TOOL_USE: {
            /* Execute all tool_use blocks */
            static struct tool_dispatch_result tool_results[CLAUDE_MAX_BLOCKS];
            int tool_count_exec = 0;
            int i;

            for (i = 0; i < resp.block_count; i++) {
                if (resp.blocks[i].type != CLAUDE_CONTENT_TOOL_USE)
                    continue;

                /* Check terminal tool */
                if (config->terminal_tool &&
                    strcmp(resp.blocks[i].tool_use.name,
                           config->terminal_tool) == 0) {
                    debug_printf("[AGENT] terminal tool '%s' called\n",
                                config->terminal_tool);
                    extract_final_text(&resp, result);
                    fill_result(result, state,
                                AGENT_STOP_SPECIFIC_TOOL, step + 1);
                    agent_print_summary(result);
                    debug_printf("[AGENT] === Agent Run End ===\n");
                    return 0;
                }

                debug_printf("[AGENT] executing tool: %s\n",
                            resp.blocks[i].tool_use.name);

                memset(&event, 0, sizeof(event));
                event.type = AGENT_EVENT_TOOL_START;
                event.step = step + 1;
                event.tool_name = resp.blocks[i].tool_use.name;
                event.tool_input_json = resp.blocks[i].tool_use.input_json;
                event.tool_input_len = resp.blocks[i].tool_use.input_json_len;
                emit_agent_event(&event);

                /* AT-97: Permission check */
                {
                    static struct permission_policy s_policy;
                    static int s_policy_init = 0;
                    if (!s_policy_init) {
                        perm_set_default(&s_policy);
                        s_policy_init = 1;
                    }
                    if (!perm_check_tool(&s_policy,
                                         resp.blocks[i].tool_use.name,
                                         resp.blocks[i].tool_use.input_json,
                                         resp.blocks[i].tool_use.input_json_len)) {
                        /* Tool blocked by permissions */
                        debug_printf("[AGENT] tool '%s' blocked by permissions\n",
                                    resp.blocks[i].tool_use.name);
                        snprintf(tool_results[tool_count_exec].result_json, TOOL_RESULT_BUF,
                                 "{\"error\":\"permission denied: tool '%s' is not allowed for this input\"}",
                                 resp.blocks[i].tool_use.name);
                        tool_results[tool_count_exec].result_len =
                            strlen(tool_results[tool_count_exec].result_json);
                        strncpy(tool_results[tool_count_exec].tool_use_id,
                                resp.blocks[i].tool_use.id, 63);
                        tool_results[tool_count_exec].is_error = 1;
                        state->total_errors++;
                        memset(&event, 0, sizeof(event));
                        event.type = AGENT_EVENT_TOOL_FINISH;
                        event.step = step + 1;
                        event.tool_name = resp.blocks[i].tool_use.name;
                        event.tool_input_json = resp.blocks[i].tool_use.input_json;
                        event.tool_input_len =
                            resp.blocks[i].tool_use.input_json_len;
                        event.tool_result_json =
                            tool_results[tool_count_exec].result_json;
                        event.tool_result_len =
                            tool_results[tool_count_exec].result_len;
                        event.tool_is_error =
                            tool_results[tool_count_exec].is_error;
                        emit_agent_event(&event);
                        tool_count_exec++;
                        if (tool_count_exec >= CLAUDE_MAX_BLOCKS) break;
                        continue;
                    }
                }

                /* AT-97: Pre-tool-use hook */
                {
                    struct hook_context hctx;
                    struct hook_response hresp;
                    memset(&hctx, 0, sizeof(hctx));
                    hctx.event = HOOK_PRE_TOOL_USE;
                    hctx.tool_name = resp.blocks[i].tool_use.name;
                    hctx.tool_input_json = resp.blocks[i].tool_use.input_json;
                    hctx.tool_input_len = resp.blocks[i].tool_use.input_json_len;
                    hctx.step_number = step;
                    if (hooks_fire(&hctx, &hresp) > 0) {
                        /* Blocked by hook */
                        debug_printf("[AGENT] tool '%s' blocked by hook: %s\n",
                                    resp.blocks[i].tool_use.name, hresp.message);
                        snprintf(tool_results[tool_count_exec].result_json, TOOL_RESULT_BUF,
                                 "{\"error\":\"blocked by policy: %s\"}",
                                 hresp.message[0] ? hresp.message : "operation not permitted");
                        tool_results[tool_count_exec].result_len =
                            strlen(tool_results[tool_count_exec].result_json);
                        strncpy(tool_results[tool_count_exec].tool_use_id,
                                resp.blocks[i].tool_use.id, 63);
                        tool_results[tool_count_exec].is_error = 1;
                        state->total_errors++;
                        memset(&event, 0, sizeof(event));
                        event.type = AGENT_EVENT_TOOL_FINISH;
                        event.step = step + 1;
                        event.tool_name = resp.blocks[i].tool_use.name;
                        event.tool_input_json = resp.blocks[i].tool_use.input_json;
                        event.tool_input_len =
                            resp.blocks[i].tool_use.input_json_len;
                        event.tool_result_json =
                            tool_results[tool_count_exec].result_json;
                        event.tool_result_len =
                            tool_results[tool_count_exec].result_len;
                        event.tool_is_error =
                            tool_results[tool_count_exec].is_error;
                        emit_agent_event(&event);
                        tool_count_exec++;
                        if (tool_count_exec >= CLAUDE_MAX_BLOCKS) break;
                        continue;
                    }
                }

                tool_dispatch(&resp.blocks[i].tool_use,
                              &tool_results[tool_count_exec]);

                if (tool_results[tool_count_exec].is_error) {
                    state->total_errors++;
                    debug_printf("[AGENT] tool error: %.80s\n",
                                tool_results[tool_count_exec].result_json);
                }

                memset(&event, 0, sizeof(event));
                event.type = AGENT_EVENT_TOOL_FINISH;
                event.step = step + 1;
                event.tool_name = resp.blocks[i].tool_use.name;
                event.tool_input_json = resp.blocks[i].tool_use.input_json;
                event.tool_input_len = resp.blocks[i].tool_use.input_json_len;
                event.tool_result_json = tool_results[tool_count_exec].result_json;
                event.tool_result_len = tool_results[tool_count_exec].result_len;
                event.tool_is_error = tool_results[tool_count_exec].is_error;
                emit_agent_event(&event);

                state->total_tool_executions++;
                tool_count_exec++;

                /* AT-97: Audit log */
                {
                    struct audit_entry ae;
                    memset(&ae, 0, sizeof(ae));
                    ae.step = step;
                    strncpy(ae.tool_name, resp.blocks[i].tool_use.name, 63);
                    if (tool_results[tool_count_exec - 1].is_error)
                        strncpy(ae.action, "error", 15);
                    else
                        strncpy(ae.action, "execute", 15);
                    audit_log(&ae);
                }

                debug_printf("[AGENT] tool result: %d bytes, is_error=%d\n",
                            tool_results[tool_count_exec - 1].result_len,
                            tool_results[tool_count_exec - 1].is_error);

                if (tool_count_exec >= CLAUDE_MAX_BLOCKS)
                    break;
            }

            /* Add tool results to conversation */
            if (tool_count_exec > 0) {
                conv_add_tool_results(&state->conv,
                                       tool_results, tool_count_exec);
            }

            /* Check token limits */
            if (conv_check_tokens(&state->conv) == 2) {
                debug_printf("[AGENT] token limit reached\n");
                fill_result(result, state,
                            AGENT_STOP_TOKEN_LIMIT, step + 1);
                agent_print_summary(result);
                debug_printf("[AGENT] === Agent Run End ===\n");
                return -1;
            }

            break;  /* Continue to next step */
        }

        case CLAUDE_STOP_MAX_TOKENS:
            debug_printf("[AGENT] max_tokens in response, continuing...\n");
            /* The response was truncated; loop again to get more */
            break;

        default:
            debug_printf("[AGENT] unexpected stop_reason: %d\n",
                        resp.stop_reason);
            break;
        }
    }

    /* Max steps reached */
    debug_printf("[AGENT] max steps (%d) reached\n", config->max_steps);
    fill_result(result, state, AGENT_STOP_MAX_STEPS, config->max_steps);

    /* Try to extract any text from the last response */
    extract_final_text(&resp, result);

    agent_print_summary(result);
    debug_printf("[AGENT] === Agent Run End ===\n");
    return 0;
}

int agent_run_turn(
    const struct agent_config *config,
    struct agent_state *state,
    const char *user_prompt,
    struct agent_result *result)
{
    static char effective_prompt_buf[CONV_TEXT_BUF];
    const char *effective_prompt;

    if (!config || !state || !user_prompt || !result)
        return -1;
    effective_prompt = build_effective_user_prompt(user_prompt,
                                                   effective_prompt_buf,
                                                   sizeof(effective_prompt_buf));
    tool_init();
    if (conv_add_user_text(&state->conv, effective_prompt) < 0)
        return -1;

    debug_printf("[AGENT] === Agent Turn Start ===\n");
    debug_printf("[AGENT] model=%s, max_steps=%d\n",
                config->model, config->max_steps);
    debug_printf("[AGENT] system_prompt=%d bytes, tools=%d registered\n",
                state->conv.system_prompt_len, tool_count());
    debug_printf("[AGENT] prompt: %.80s%s\n", effective_prompt,
                strlen(effective_prompt) > 80 ? "..." : "");
    return agent_run_loop(config, state, result);
}

int agent_run(
    const struct agent_config *config,
    const char *initial_prompt,
    struct agent_result *result)
{
    static struct agent_state state;

    if (!config || !initial_prompt || !result)
        return -1;

    agent_state_init(&state, config);
    return agent_run_turn(config, &state, initial_prompt, result);
}

void agent_print_summary(const struct agent_result *result)
{
    const char *stop_str;

    if (!result)
        return;

    switch (result->stop_reason) {
    case AGENT_STOP_END_TURN:      stop_str = "end_turn"; break;
    case AGENT_STOP_MAX_STEPS:     stop_str = "max_steps"; break;
    case AGENT_STOP_SPECIFIC_TOOL: stop_str = "terminal_tool"; break;
    case AGENT_STOP_ERROR:         stop_str = "error"; break;
    case AGENT_STOP_TOKEN_LIMIT:   stop_str = "token_limit"; break;
    default:                       stop_str = "unknown"; break;
    }

    debug_printf("[AGENT] Summary: stop=%s, steps=%d, "
                "tokens=%d/%d, tools=%d\n",
                stop_str,
                result->steps_executed,
                result->total_input_tokens,
                result->total_output_tokens,
                result->total_tool_calls);
}
