#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>

static int run_process(const char *path, char **argv)
{
  int status = 0;
  pid_t pid = execve(path, argv, 0);

  if (pid < 0)
    return -1;
  set_foreground_pid(STDIN_FILENO, pid);
  if (waitpid(pid, &status, 0) < 0)
    status = 1;
  set_foreground_pid(STDIN_FILENO, 0);
  return status;
}

int main(int argc, char **argv)
{
  char *term_argv[3];
  char *fallback_argv[2];
  int status;

  (void)argc;
  (void)argv;

  debug_write("AUDIT agent_term_enter\n", 23);
  term_argv[0] = "term";
  term_argv[1] = "--agent-fusion";
  term_argv[2] = 0;
  status = run_process("/usr/bin/term", term_argv);
  if (status >= 0)
    return status;

  debug_write("AUDIT agent_term_term_failed\n", 29);
  fallback_argv[0] = "term";
  fallback_argv[1] = 0;
  status = run_process("/usr/bin/term", fallback_argv);
  if (status >= 0)
    return status;

  debug_write("AUDIT agent_term_term_plain_failed\n", 35);
  fallback_argv[0] = "eshell";
  fallback_argv[1] = 0;
  return run_process("/usr/bin/eshell", fallback_argv);
}
