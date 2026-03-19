/*
 * tool_run_command.c - run_command tool implementation
 *
 * Executes commands on the Sodex OS via execve + pipe.
 * Spawns "sh -c <command>" as a child process, captures stdout
 * via pipe, and returns the output as a tool result.
 */

#include <agent/tool_handlers.h>
#include <agent/bounded_output.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fs.h>
#include <poll.h>
#ifdef TEST_BUILD
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_RUN_COMMAND[] =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"Shell command to execute (passed to sh -c)\"}},"
    "\"required\":[\"command\"]}";

#define CMD_POLL_TICKS      10
#define CMD_TIMEOUT_TICKS 1000   /* 約10秒相当 */

static int drain_child_output(int read_fd, pid_t pid,
                              struct bounded_output *bounded,
                              int *exit_status,
                              int *timed_out)
{
    int ret;
    int status = 0;
    int child_done = 0;
    int waited_ticks = 0;
    struct pollfd pfd;

    if (exit_status)
        *exit_status = 0;
    if (timed_out)
        *timed_out = 0;

    pfd.fd = read_fd;
    pfd.events = POLLIN;

    while (!child_done) {
        pfd.revents = 0;
        ret = poll(&pfd, 1, CMD_POLL_TICKS);
        if (ret == 0)
            waited_ticks += CMD_POLL_TICKS;
        if (ret > 0 && (pfd.revents & POLLIN)) {
            char chunk[512];
            int nr = (int)read(read_fd, chunk, sizeof(chunk));

            if (nr > 0)
                bounded_output_append(bounded, chunk, nr);
        }

        if (waitpid(pid, &status, WNOHANG) > 0) {
            child_done = 1;
        } else {
            if (ret > 0 &&
                (pfd.revents & POLLHUP) &&
                !(pfd.revents & POLLIN)) {
                poll((struct pollfd *)0, 0, CMD_POLL_TICKS);
                waited_ticks += CMD_POLL_TICKS;
            }
            if (waited_ticks >= CMD_TIMEOUT_TICKS) {
                debug_printf("[TOOL run_command] pid=%d timeout after %d ticks\n",
                            (int)pid, waited_ticks);
                if (timed_out)
                    *timed_out = 1;
                kill(pid, SIGKILL);
                if (waitpid(pid, &status, 0) < 0)
                    status = -1;
                child_done = 1;
            }
        }

    }

    for (;;) {
        char chunk[512];

        pfd.revents = 0;
        ret = poll(&pfd, 1, 1);
        if (ret <= 0)
            break;
        if (!(pfd.revents & POLLIN))
            break;
        ret = (int)read(read_fd, chunk, sizeof(chunk));
        if (ret <= 0)
            break;
        bounded_output_append(bounded, chunk, ret);
    }

    if (exit_status)
        *exit_status = status;
    return 0;
}

/*
 * Execute a command via execve("/usr/bin/sh", ["sh", "-c", cmd], NULL)
 * and capture its stdout through a pipe.
 *
 * Returns exit code (>= 0) on success, -1 on failure.
 * Output is written to out_buf (up to out_cap bytes).
 * *out_len is set to the number of bytes written.
 */
static int exec_and_capture_bounded(const char *cmd,
                                    struct bounded_output *bounded,
                                    int *exit_status,
                                    int *timed_out)
{
    int pipefd[2];
    int saved_stdout;
    int saved_stderr;
    pid_t pid;
    char *argv[4];

    /* Create pipe: pipefd[0]=read, pipefd[1]=write */
    if (pipe(pipefd) < 0) {
        debug_printf("[TOOL run_command] pipe() failed\n");
        return -1;
    }

    /* Save current stdout and redirect to pipe write end */
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        debug_printf("[TOOL run_command] dup(stdout) failed\n");
        return -1;
    }

    saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0) {
        close(saved_stdout);
        close(pipefd[0]);
        close(pipefd[1]);
        debug_printf("[TOOL run_command] dup(stderr) failed\n");
        return -1;
    }

    close(STDOUT_FILENO);
    if (dup(pipefd[1]) != STDOUT_FILENO) {
        /* Restore stdout */
        close(STDOUT_FILENO);
        dup(saved_stdout);
        close(saved_stdout);
        close(saved_stderr);
        close(pipefd[0]);
        close(pipefd[1]);
        debug_printf("[TOOL run_command] dup(pipefd[1]) != STDOUT\n");
        return -1;
    }

    close(STDERR_FILENO);
    if (dup(pipefd[1]) != STDERR_FILENO) {
        close(STDERR_FILENO);
        dup(saved_stderr);
        close(STDOUT_FILENO);
        dup(saved_stdout);
        close(saved_stdout);
        close(saved_stderr);
        close(pipefd[0]);
        close(pipefd[1]);
        debug_printf("[TOOL run_command] dup(pipefd[1]) != STDERR\n");
        return -1;
    }
    close(pipefd[1]);  /* Close original write end; child inherits stdout/stderr */

    /* Spawn sh -c "command" */
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = (char *)cmd;
    argv[3] = (char *)0;

    pid = execve("/usr/bin/sh", argv, (char *const *)0);

    /* Restore stdout immediately after execve */
    close(STDOUT_FILENO);
    dup(saved_stdout);
    close(saved_stdout);
    close(STDERR_FILENO);
    dup(saved_stderr);
    close(saved_stderr);

    if (pid < 0) {
        close(pipefd[0]);
        debug_printf("[TOOL run_command] execve failed\n");
        return -1;
    }

    debug_printf("[TOOL run_command] spawned pid=%d: sh -c \"%s\"\n",
                (int)pid, cmd);

    drain_child_output(pipefd[0], pid, bounded, exit_status, timed_out);
    close(pipefd[0]);

    debug_printf("[TOOL run_command] pid=%d exited status=%d, output=%d bytes\n",
                (int)pid, exit_status ? *exit_status : 0,
                bounded ? bounded->total_bytes : 0);
    return 0;
}

int tool_run_command(const char *input_json, int input_len,
                     char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;
    char command[512];
    struct json_writer jw;
    int exit_code;
    int timed_out = 0;
    static struct bounded_output bounded;

    if (!input_json || !result_buf)
        return -1;

    /* Parse input JSON */
    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    tok = json_find_key(input_json, tokens, ntokens, 0, "command");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: command\"}");
    }
    if (json_token_str(input_json, &tokens[tok], command, sizeof(command)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"command too long\"}");
    }

    debug_printf("[TOOL run_command] command: %s\n", command);

    /* Execute command and capture output */
    bounded_output_init(&bounded);
    bounded_output_begin_artifact(&bounded, "run", ".txt");
    if (exec_and_capture_bounded(command, &bounded, &exit_code, &timed_out) < 0)
        exit_code = -1;
    bounded_output_finish(&bounded, bounded.total_bytes > AGENT_BOUNDED_INLINE);

    jw_init(&jw, result_buf, result_cap);

    if (timed_out) {
        jw_object_start(&jw);
        jw_key(&jw, "error");
        jw_string(&jw, "command timed out");
        jw_key(&jw, "code");
        jw_string(&jw, "timeout");
        jw_key(&jw, "command");
        jw_string(&jw, command);
        bounded_output_write_json(&bounded, &jw,
                                  "output",
                                  "output_head",
                                  "output_tail");
        jw_key(&jw, "exit_code");
        jw_int(&jw, -1);
        jw_key(&jw, "timed_out");
        jw_bool(&jw, 1);
        jw_key(&jw, "total_bytes");
        jw_int(&jw, bounded.total_bytes);
        jw_object_end(&jw);
    } else if (exit_code < 0) {
        /* Execution failed entirely */
        jw_object_start(&jw);
        jw_key(&jw, "error");
        jw_string(&jw, "command execution failed");
        jw_key(&jw, "command");
        jw_string(&jw, command);
        jw_key(&jw, "exit_code");
        jw_int(&jw, -1);
        jw_object_end(&jw);
    } else {
        jw_object_start(&jw);
        jw_key(&jw, "command");
        jw_string(&jw, command);
        bounded_output_write_json(&bounded, &jw,
                                  "output",
                                  "output_head",
                                  "output_tail");
        jw_key(&jw, "exit_code");
        jw_int(&jw, exit_code);
        jw_key(&jw, "total_bytes");
        jw_int(&jw, bounded.total_bytes);
        jw_object_end(&jw);
    }

    return jw_finish(&jw);
}
