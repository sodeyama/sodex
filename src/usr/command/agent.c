/*
 * agent.c - Interactive agent CLI
 *
 * 既定は REPL とし、-p / run / --continue / --resume を提供する。
 * セッションは /var/agent/sessions に保存し、workspace memory は
 * /var/agent/memory/<cwd-hash>.md に保存する。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <entropy.h>
#include <fs.h>
#include <json.h>
#include <agent/agent.h>
#include <agent/claude_client.h>
#include <agent/memory_store.h>
#include <agent/tool_handlers.h>
#include <agent/session.h>

#define API_KEY_PATH       "/etc/claude.conf"
#define API_KEY_MAX        256
#define PROMPT_MAX         2048
#define AGENT_PATH_MAX     PATHNAME_MAX
#define REPL_PROMPT_MAX    384
#define COMPACT_KEEP_TURNS 8
#define COMPACT_SUMMARY_MAX 1024

enum agent_cli_mode {
    AGENT_MODE_REPL = 0,
    AGENT_MODE_ONESHOT,
    AGENT_MODE_RUN,
    AGENT_MODE_CONTINUE,
    AGENT_MODE_RESUME,
    AGENT_MODE_SESSIONS,
    AGENT_MODE_MEMORY,
};

static struct agent_config s_config;
static struct agent_result s_result;
static struct agent_state s_state;
static struct session_meta s_session;
static char s_api_key[API_KEY_MAX];
static char s_input_line[PROMPT_MAX];
static int s_streamed_chars = 0;

static void repl_stream_text(const char *text, int text_len, void *userdata)
{
    (void)userdata;

    if (!text || text_len <= 0)
        return;
    write(STDOUT_FILENO, text, (size_t)text_len);
    s_streamed_chars += text_len;
}

static int read_api_key(char *buf, int cap)
{
    int fd;
    int n;
    int i;

    fd = open(API_KEY_PATH, O_RDONLY, 0);
    if (fd < 0) {
        printf("agent: cannot open %s\n", API_KEY_PATH);
        printf("  Create .env.local with ANTHROPIC_API_KEY=sk-ant-...\n");
        printf("  then run: make inject-api-key\n");
        return -1;
    }

    n = read(fd, buf, (size_t)(cap - 1));
    close(fd);
    if (n <= 0) {
        printf("agent: %s is empty\n", API_KEY_PATH);
        return -1;
    }

    buf[n] = '\0';
    for (i = n - 1; i >= 0; i--) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')
            buf[i] = '\0';
        else
            break;
    }

    n = strlen(buf);
    if (n < 10) {
        printf("agent: API key too short (%d chars)\n", n);
        return -1;
    }
    return n;
}

static int build_prompt(int argc, char *argv[], int start,
                        char *buf, int cap)
{
    int pos = 0;
    int i;

    for (i = start; i < argc; i++) {
        int len = strlen(argv[i]);

        if (pos + len + 1 >= cap)
            break;
        if (pos > 0)
            buf[pos++] = ' ';
        memcpy(buf + pos, argv[i], (size_t)len);
        pos += len;
    }
    buf[pos] = '\0';
    return pos;
}

static void print_usage(void)
{
    printf("Usage:\n");
    printf("  agent\n");
    printf("  agent \"質問\"\n");
    printf("  agent -p \"1 回だけ実行\"\n");
    printf("  agent run \"自律タスク\"\n");
    printf("  agent --continue\n");
    printf("  agent --resume [session-id]\n");
    printf("  agent sessions [--delete <session-id>]\n");
    printf("  agent memory\n");
    printf("  agent memory add \"メモ\"\n");
    printf("\nOptions:\n");
    printf("  -s <N>   Max steps\n");
    printf("  -p       単発モード\n");
}

static int safe_copy(char *dst, int cap, const char *src)
{
    int len;

    if (!dst || cap <= 0)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    len = strlen(src);
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    return len;
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

    memcpy(buf + pos, dentry->d_name, (size_t)name_len);
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

static int read_text_file(const char *path, char *buf, int cap)
{
    int fd;
    int n;

    if (!path || !buf || cap <= 1)
        return -1;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    n = read(fd, buf, (size_t)(cap - 1));
    close(fd);
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    while (n > 0 &&
           (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return n;
}

static void ensure_memory_dirs(void)
{
#ifndef TEST_BUILD
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
#endif
    mkdir(AGENT_MEMORY_DIR, 0755);
}

static int prepare_agent_config(int custom_steps)
{
    if (read_api_key(s_api_key, sizeof(s_api_key)) < 0)
        return -1;

    agent_config_init(&s_config);
    agent_load_config(&s_config);
    s_config.api_key = s_api_key;

    if (custom_steps > 0)
        s_config.max_steps = custom_steps;
    return 0;
}

static int read_line_stdin(char *buf, int cap)
{
    int n;

    if (!buf || cap <= 1)
        return -1;

    n = read(STDIN_FILENO, buf, (size_t)(cap - 1));
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    while (n > 0 &&
           (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == '\0')) {
        buf[--n] = '\0';
    }
    return n;
}

static void print_result(const struct agent_result *result)
{
    if (!result)
        return;

    if (result->final_text_len > 0) {
        write(STDOUT_FILENO, result->final_text, (size_t)result->final_text_len);
        if (result->final_text[result->final_text_len - 1] != '\n')
            printf("\n");
        return;
    }

    switch (result->stop_reason) {
    case AGENT_STOP_MAX_STEPS:
        printf("[stopped: max_steps]\n");
        break;
    case AGENT_STOP_TOKEN_LIMIT:
        printf("[stopped: token_limit]\n");
        break;
    case AGENT_STOP_ERROR:
        printf("[stopped: error]\n");
        break;
    default:
        break;
    }
}

static int persist_new_turns(const char *session_id,
                             const struct agent_state *state,
                             int start_turn,
                             const struct agent_result *result)
{
    int i;
    int last_assistant = -1;

    if (!session_id || !state || !result)
        return -1;

    for (i = start_turn; i < state->conv.turn_count; i++) {
        if (state->conv.turns[i].role &&
            strcmp(state->conv.turns[i].role, "assistant") == 0) {
            last_assistant = i;
        }
    }

    for (i = start_turn; i < state->conv.turn_count; i++) {
        int in_tok = 0;
        int out_tok = 0;

        if (i == last_assistant) {
            in_tok = result->total_input_tokens;
            out_tok = result->total_output_tokens;
        }
        if (session_append_turn(session_id,
                                &state->conv.turns[i],
                                in_tok, out_tok) < 0) {
            return -1;
        }
    }

    return 0;
}

static void print_sessions(void)
{
    struct session_index index;
    int i;

    if (session_list(&index) != 0 || index.count == 0) {
        printf("No sessions.\n");
        return;
    }

    for (i = 0; i < index.count; i++) {
        printf("%d. %s  %s  %s\n",
               i + 1,
               index.entries[i].id,
               index.entries[i].name[0] ? index.entries[i].name : "main",
               index.entries[i].cwd[0] ? index.entries[i].cwd : "/");
    }
}

static int resolve_continue_session(char *session_id, int cap)
{
    char cwd[AGENT_PATH_MAX];

    if (!session_id || cap <= 0)
        return -1;
    if (build_current_path(cwd, sizeof(cwd)) < 0)
        safe_copy(cwd, sizeof(cwd), "/");
    return agent_resume_latest_for_cwd(cwd, session_id, cap);
}

static int pick_resume_session(char *session_id, int cap)
{
    struct session_index index;
    char line[64];
    int n;
    int choice;

    if (!session_id || cap <= 0)
        return -1;
    if (session_list(&index) != 0 || index.count == 0)
        return -1;

    print_sessions();
    printf("resume> ");
    n = read_line_stdin(line, sizeof(line));
    if (n <= 0)
        return -1;

    choice = atoi(line);
    if (choice >= 1 && choice <= index.count) {
        safe_copy(session_id, cap, index.entries[choice - 1].id);
        return 0;
    }

    safe_copy(session_id, cap, line);
    return 0;
}

static int start_new_session(struct agent_state *state,
                             struct session_meta *session)
{
    char cwd[AGENT_PATH_MAX];

    if (!state || !session)
        return -1;
    if (build_current_path(cwd, sizeof(cwd)) < 0)
        safe_copy(cwd, sizeof(cwd), "/");

    agent_state_init(state, &s_config);
    return session_create(session, s_config.model, cwd);
}

static int resume_session(const char *session_id,
                          struct agent_state *state,
                          struct session_meta *session)
{
    if (!session_id || !state || !session)
        return -1;

    agent_state_init(state, &s_config);
    if (session_load(session_id, &state->conv) < 0)
        return -1;
    if (session_read_meta(session_id, session) < 0) {
        memset(session, 0, sizeof(*session));
        safe_copy(session->id, sizeof(session->id), session_id);
    }
    return 0;
}

static void print_status(const struct agent_state *state,
                         const struct session_meta *session)
{
    int total_tokens;
    int ctx_percent;

    total_tokens = conv_total_tokens(&state->conv);
    ctx_percent = (total_tokens * 100) / CONV_TOKEN_LIMIT;
    if (ctx_percent < 0)
        ctx_percent = 0;

    printf("session=%s name=%s turns=%d tokens=%d ctx=%d%% max=%d\n",
           session->id[0] ? session->id : "-",
           session->name[0] ? session->name : "main",
           state->conv.turn_count,
           total_tokens,
           ctx_percent,
           s_config.max_steps);
}

static void print_memory_sources(void)
{
    struct agent_memory_source sources[AGENT_MEMORY_SOURCE_MAX];
    char cwd[AGENT_PATH_MAX];
    char buf[512];
    int count;
    int i;

    if (build_current_path(cwd, sizeof(cwd)) < 0)
        safe_copy(cwd, sizeof(cwd), "/");

    printf("Memory sources:\n");
    count = agent_collect_memory_sources(cwd, sources, AGENT_MEMORY_SOURCE_MAX);
    if (count <= 0) {
        printf("(none)\n");
        return;
    }

    for (i = 0; i < count; i++) {
        printf("- %s\n", sources[i].path);
        if (read_text_file(sources[i].path, buf, sizeof(buf)) > 0) {
            printf("%s\n", buf);
        }
    }
}

static int append_workspace_memory_note(struct agent_state *state,
                                        const char *note,
                                        int quiet)
{
    char cwd[AGENT_PATH_MAX];
    char path[AGENT_PATH_MAX];
    int ret;

    if (!state || !note || !*note)
        return -1;

    if (build_current_path(cwd, sizeof(cwd)) < 0)
        safe_copy(cwd, sizeof(cwd), "/");
    ensure_memory_dirs();
    ret = agent_memory_append_workspace(cwd, note, path, sizeof(path));
    if (ret == -2) {
        if (!quiet)
            printf("memory skipped: secret-like text\n");
        return -1;
    }
    if (ret < 0)
        return -1;

    conv_append_system_text(&state->conv, "Workspace Memory", note);
    if (!quiet)
        printf("memory saved: %s\n", path);
    return 0;
}

static int handle_compact(struct agent_state *state,
                          const struct session_meta *session,
                          const char *focus)
{
    char summary[COMPACT_SUMMARY_MAX];
    int keep_from;
    int removed;

    if (!state || !session)
        return -1;
    if (state->conv.turn_count <= COMPACT_KEEP_TURNS) {
        printf("compact: not needed\n");
        return 0;
    }

    keep_from = state->conv.turn_count - COMPACT_KEEP_TURNS;
    removed = conv_compact(&state->conv, COMPACT_KEEP_TURNS,
                           focus, summary, sizeof(summary));
    if (removed < 0)
        return -1;
    session_append_compact(session->id, summary, 0, keep_from - 1);
    printf("compacted: kept %d recent turns\n", COMPACT_KEEP_TURNS);
    return 0;
}

static int handle_shell_shortcut(struct agent_state *state,
                                 const struct session_meta *session,
                                 const char *command)
{
    char input_json[1024];
    char result_json[4096];
    char conv_text[PROMPT_MAX];
    struct json_writer jw;
    int start_turn;
    int len;

    if (!state || !session || !command)
        return -1;

    jw_init(&jw, input_json, sizeof(input_json));
    jw_object_start(&jw);
    jw_key(&jw, "command");
    jw_string(&jw, command);
    jw_object_end(&jw);
    len = jw_finish(&jw);
    if (len < 0)
        return -1;

    len = tool_run_command(input_json, len, result_json, sizeof(result_json));
    if (len < 0)
        return -1;

    printf("! %s\n", command);
    write(STDOUT_FILENO, result_json, (size_t)len);
    if (result_json[len - 1] != '\n')
        printf("\n");

    start_turn = state->conv.turn_count;
    snprintf(conv_text, sizeof(conv_text),
             "Shell command: %s\nResult JSON: %s", command, result_json);
    conv_add_user_text(&state->conv, conv_text);
    session_append_turn(session->id, &state->conv.turns[start_turn], 0, 0);
    {
        char auto_note[512];

        if (agent_memory_auto_note_command(command,
                                           auto_note, sizeof(auto_note)) > 0) {
            append_workspace_memory_note(state, auto_note, 1);
        }
    }
    return 0;
}

static void print_help(void)
{
    printf("/help /clear /compact [/focus] /memory [/add ...] /permissions /resume [id]\n");
    printf("/sessions /rename <name> /status\n");
    printf("# memo で workspace memory 追記\n");
    printf("!cmd でシェル実行結果を会話へ追加\n");
}

static int run_single_turn(struct agent_state *state,
                           const struct session_meta *session,
                           const char *prompt)
{
    int start_turn;
    int ret;

    if (!state || !session || !prompt)
        return -1;

    start_turn = state->conv.turn_count;
    s_streamed_chars = 0;
    claude_client_set_text_stream_callback(repl_stream_text, (void *)0);
    ret = agent_run_turn(&s_config, state, prompt, &s_result);
    claude_client_set_text_stream_callback((claude_stream_text_fn)0, (void *)0);
    if (ret == 0)
        persist_new_turns(session->id, state, start_turn, &s_result);
    if (s_streamed_chars > 0 &&
        s_result.stop_reason == AGENT_STOP_END_TURN &&
        s_result.final_text_len > 0) {
        if (s_result.final_text[s_result.final_text_len - 1] != '\n')
            printf("\n");
    } else {
        print_result(&s_result);
    }
    if (ret == 0 &&
        (state->conv.turn_count > (COMPACT_KEEP_TURNS * 2) ||
         conv_check_tokens(&state->conv) != 0)) {
        handle_compact(state, session, "auto");
    }
    return ret;
}

static int print_repl_prompt(const struct agent_state *state,
                             const struct session_meta *session)
{
    char cwd[AGENT_PATH_MAX];
    char prompt[REPL_PROMPT_MAX];
    int total_tokens;
    int ctx_percent;

    if (build_current_path(cwd, sizeof(cwd)) < 0)
        safe_copy(cwd, sizeof(cwd), "/");

    total_tokens = conv_total_tokens(&state->conv);
    ctx_percent = (total_tokens * 100) / CONV_TOKEN_LIMIT;
    if (ctx_percent < 0)
        ctx_percent = 0;
    if (ctx_percent > 999)
        ctx_percent = 999;

    snprintf(prompt, sizeof(prompt),
             "[agent %.8s %s ctx=%d%%] > ",
             session->id[0] ? session->id : "new",
             cwd,
             ctx_percent);
    write(STDOUT_FILENO, prompt, (size_t)strlen(prompt));
    return 0;
}

static int handle_slash_command(char *line,
                                struct agent_state *state,
                                struct session_meta *session,
                                int custom_steps)
{
    if (strcmp(line, "/help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(line, "/sessions") == 0) {
        print_sessions();
        return 0;
    }

    if (strcmp(line, "/status") == 0) {
        print_status(state, session);
        return 0;
    }

    if (strncmp(line, "/rename ", 8) == 0) {
        safe_copy(session->name, sizeof(session->name), line + 8);
        session_append_rename(session->id, session->name);
        printf("renamed: %s\n", session->name);
        return 0;
    }

    if (strcmp(line, "/memory") == 0) {
        print_memory_sources();
        return 0;
    }

    if (strncmp(line, "/memory add ", 12) == 0) {
        append_workspace_memory_note(state, line + 12, 0);
        return 0;
    }

    if (strncmp(line, "/compact", 8) == 0) {
        const char *focus = line[8] ? line + 9 : "";
        handle_compact(state, session, focus);
        return 0;
    }

    if (strcmp(line, "/permissions") == 0 ||
        strncmp(line, "/permissions ", 13) == 0) {
        printf("permissions: standard\n");
        return 0;
    }

    if (strcmp(line, "/clear") == 0) {
        prepare_agent_config(custom_steps);
        if (start_new_session(state, session) == 0)
            printf("new session: %s\n", session->id);
        return 0;
    }

    if (strncmp(line, "/resume", 7) == 0) {
        char resume_id[SESSION_ID_LEN + 1];

        memset(resume_id, 0, sizeof(resume_id));
        if (line[7] == ' ' && line[8] != '\0') {
            safe_copy(resume_id, sizeof(resume_id), line + 8);
        } else if (pick_resume_session(resume_id, sizeof(resume_id)) < 0) {
            printf("resume: no session\n");
            return 0;
        }
        prepare_agent_config(custom_steps);
        if (resume_session(resume_id, state, session) < 0) {
            printf("resume: failed %s\n", resume_id);
            return 0;
        }
        printf("resumed: %s\n", session->id);
        return 0;
    }

    printf("unknown command: %s\n", line);
    return 0;
}

static int repl_loop(struct agent_state *state,
                     struct session_meta *session,
                     const char *initial_prompt,
                     int custom_steps)
{
    if (initial_prompt && *initial_prompt) {
        if (run_single_turn(state, session, initial_prompt) < 0)
            return -1;
    }

    for (;;) {
        int n;

        print_repl_prompt(state, session);
        n = read_line_stdin(s_input_line, sizeof(s_input_line));
        if (n < 0)
            break;
        if (n == 0)
            continue;

        if (strcmp(s_input_line, "exit") == 0 ||
            strcmp(s_input_line, "quit") == 0)
            break;
        if (s_input_line[0] == '/') {
            handle_slash_command(s_input_line, state, session, custom_steps);
            continue;
        }
        if (s_input_line[0] == '#') {
            const char *note = s_input_line + 1;
            while (*note == ' ')
                note++;
            append_workspace_memory_note(state, note, 0);
            continue;
        }
        if (s_input_line[0] == '!') {
            const char *command = s_input_line + 1;
            while (*command == ' ')
                command++;
            handle_shell_shortcut(state, session, command);
            continue;
        }

        run_single_turn(state, session, s_input_line);
    }

    return 0;
}

static int run_oneshot(const char *prompt)
{
    int ret;

    ret = agent_run(&s_config, prompt, &s_result);
    print_result(&s_result);
    return ret;
}

int main(int argc, char *argv[])
{
    enum agent_cli_mode mode = AGENT_MODE_REPL;
    char prompt[PROMPT_MAX];
    char session_id[SESSION_ID_LEN + 1];
    char delete_id[SESSION_ID_LEN + 1];
    int custom_steps = 0;
    int i;
    int prompt_start = -1;

    memset(prompt, 0, sizeof(prompt));
    memset(session_id, 0, sizeof(session_id));
    memset(delete_id, 0, sizeof(delete_id));

    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            custom_steps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0) {
            mode = AGENT_MODE_ONESHOT;
            prompt_start = i + 1;
            break;
        } else if (strcmp(argv[i], "run") == 0) {
            mode = AGENT_MODE_RUN;
            prompt_start = i + 1;
            break;
        } else if (strcmp(argv[i], "--continue") == 0) {
            mode = AGENT_MODE_CONTINUE;
        } else if (strcmp(argv[i], "--resume") == 0) {
            mode = AGENT_MODE_RESUME;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                safe_copy(session_id, sizeof(session_id), argv[i + 1]);
                i++;
            }
        } else if (strcmp(argv[i], "sessions") == 0) {
            mode = AGENT_MODE_SESSIONS;
            if (i + 2 < argc && strcmp(argv[i + 1], "--delete") == 0) {
                safe_copy(delete_id, sizeof(delete_id), argv[i + 2]);
            }
            break;
        } else if (strcmp(argv[i], "memory") == 0) {
            mode = AGENT_MODE_MEMORY;
            if (i + 2 < argc && strcmp(argv[i + 1], "add") == 0) {
                prompt_start = i + 2;
            }
            break;
        } else {
            prompt_start = i;
            break;
        }
    }

    if (mode == AGENT_MODE_SESSIONS) {
        if (delete_id[0] != '\0') {
            session_delete(delete_id);
            printf("deleted: %s\n", delete_id);
        }
        print_sessions();
        return 0;
    }

    if (mode == AGENT_MODE_MEMORY) {
        if (prompt_start >= 0)
            build_prompt(argc, argv, prompt_start, prompt, sizeof(prompt));
        if (prompt[0] != '\0') {
            struct agent_state tmp_state;
            struct agent_config tmp_config;

            agent_config_init(&tmp_config);
            agent_state_init(&tmp_state, &tmp_config);
            if (append_workspace_memory_note(&tmp_state, prompt, 0) < 0)
                return 1;
        }
        print_memory_sources();
        return 0;
    }

    if (prompt_start >= 0)
        build_prompt(argc, argv, prompt_start, prompt, sizeof(prompt));

    entropy_init();
    entropy_collect_jitter(512);
    if (prng_init() < 0) {
        printf("agent: PRNG init failed\n");
        return 1;
    }

    if (prepare_agent_config(custom_steps) < 0)
        return 1;

    if (mode == AGENT_MODE_ONESHOT || mode == AGENT_MODE_RUN) {
        if (prompt[0] == '\0') {
            printf("agent: empty prompt\n");
            return 1;
        }
        return run_oneshot(prompt) == 0 ? 0 : 1;
    }

    if (mode == AGENT_MODE_CONTINUE) {
        if (resolve_continue_session(session_id, sizeof(session_id)) < 0) {
            printf("continue: no session for cwd\n");
            return 1;
        }
        if (resume_session(session_id, &s_state, &s_session) < 0) {
            printf("continue: failed %s\n", session_id);
            return 1;
        }
        return repl_loop(&s_state, &s_session, 0, custom_steps) == 0 ? 0 : 1;
    }

    if (mode == AGENT_MODE_RESUME) {
        if (session_id[0] == '\0' &&
            pick_resume_session(session_id, sizeof(session_id)) < 0) {
            printf("resume: no session\n");
            return 1;
        }
        if (resume_session(session_id, &s_state, &s_session) < 0) {
            printf("resume: failed %s\n", session_id);
            return 1;
        }
        return repl_loop(&s_state, &s_session, 0, custom_steps) == 0 ? 0 : 1;
    }

    if (start_new_session(&s_state, &s_session) < 0) {
        printf("agent: session create failed\n");
        return 1;
    }
    printf("session: %s\n", s_session.id);
    return repl_loop(&s_state, &s_session,
                     prompt[0] ? prompt : 0,
                     custom_steps) == 0 ? 0 : 1;
}
