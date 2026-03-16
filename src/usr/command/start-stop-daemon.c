#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>
#include <sleep.h>

#define DAEMON_MAX_REQUIRED 4
#define DAEMON_STOP_TIMEOUT_TICKS 100
#define DAEMON_KILL_TIMEOUT_TICKS 20

struct daemon_options {
  char service[32];
  char action[16];
  char exec_path[128];
  char pidfile[128];
  char stdout_path[128];
  char stderr_path[128];
  char required_paths[DAEMON_MAX_REQUIRED][128];
  int required_count;
  int stop_timeout_ticks;
};

static void copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0)
    src = "";

  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static int swap_fd(int target_fd, int next_fd)
{
  int saved_fd;
  int new_fd;

  saved_fd = dup(target_fd);
  if (saved_fd < 0)
    return -1;
  close(target_fd);
  new_fd = dup(next_fd);
  if (new_fd != target_fd)
    return -1;
  return saved_fd;
}

static void restore_fd(int target_fd, int saved_fd)
{
  if (saved_fd < 0)
    return;
  close(target_fd);
  dup(saved_fd);
  close(saved_fd);
}

static int text_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int parse_int_text(const char *text, int *value)
{
  int sign = 1;
  int parsed = 0;
  int result = 0;
  int i = 0;

  if (text == 0 || value == 0)
    return -1;

  while (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' ||
         text[i] == '\r')
    i++;
  if (text[i] == '+' || text[i] == '-') {
    if (text[i] == '-')
      sign = -1;
    i++;
  }
  while (text_is_digit(text[i])) {
    parsed = 1;
    result = result * 10 + (text[i] - '0');
    i++;
  }
  while (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' ||
         text[i] == '\r')
    i++;
  if (parsed == 0 || text[i] != '\0')
    return -1;
  *value = result * sign;
  return 0;
}

static int parse_options(int argc, char **argv, struct daemon_options *opts)
{
  int i;

  memset(opts, 0, sizeof(*opts));
  opts->stop_timeout_ticks = DAEMON_STOP_TIMEOUT_TICKS;
  for (i = 1; i + 1 < argc; i += 2) {
    if (strcmp(argv[i], "--service") == 0) {
      copy_text(opts->service, sizeof(opts->service), argv[i + 1]);
    } else if (strcmp(argv[i], "--action") == 0) {
      copy_text(opts->action, sizeof(opts->action), argv[i + 1]);
    } else if (strcmp(argv[i], "--exec") == 0) {
      copy_text(opts->exec_path, sizeof(opts->exec_path), argv[i + 1]);
    } else if (strcmp(argv[i], "--pidfile") == 0) {
      copy_text(opts->pidfile, sizeof(opts->pidfile), argv[i + 1]);
    } else if (strcmp(argv[i], "--stdout") == 0) {
      copy_text(opts->stdout_path, sizeof(opts->stdout_path), argv[i + 1]);
    } else if (strcmp(argv[i], "--stderr") == 0) {
      copy_text(opts->stderr_path, sizeof(opts->stderr_path), argv[i + 1]);
    } else if (strcmp(argv[i], "--require") == 0) {
      if (opts->required_count >= DAEMON_MAX_REQUIRED)
        return -1;
      copy_text(opts->required_paths[opts->required_count],
                sizeof(opts->required_paths[opts->required_count]),
                argv[i + 1]);
      opts->required_count++;
    } else if (strcmp(argv[i], "--stop-timeout") == 0) {
      if (parse_int_text(argv[i + 1], &opts->stop_timeout_ticks) < 0)
        return -1;
    } else {
      return -1;
    }
  }

  if (opts->action[0] == '\0' || opts->exec_path[0] == '\0' ||
      opts->pidfile[0] == '\0')
    return -1;
  return 0;
}

static int write_pidfile(const char *path, pid_t pid)
{
  char buf[16];
  int fd;
  int len = 0;
  int value = (int)pid;
  char tmp[16];

  if (path == 0 || path[0] == '\0')
    return -1;

  if (value == 0) {
    buf[len++] = '0';
  } else {
    while (value > 0 && len < (int)sizeof(tmp)) {
      tmp[len++] = (char)('0' + (value % 10));
      value /= 10;
    }
    {
      int i;
      for (i = 0; i < len; i++)
        buf[i] = tmp[len - i - 1];
    }
  }
  buf[len++] = '\n';
  fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0)
    return -1;
  write(fd, buf, (size_t)len);
  close(fd);
  return 0;
}

static int read_pid_text(const char *text, pid_t *pid)
{
  int value;

  if (text == 0 || pid == 0)
    return -2;
  if (parse_int_text(text, &value) < 0 || value < 0)
    return -2;
  *pid = (pid_t)value;
  return 0;
}

static int read_pidfile(const char *path, pid_t *pid)
{
  char buf[16];
  int fd;
  int len;

  if (path == 0 || pid == 0)
    return -2;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  len = (int)read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (len <= 0)
    return -2;
  buf[len] = '\0';
  return read_pid_text(buf, pid);
}

static int pid_running(pid_t pid)
{
  if (pid < 0)
    return 0;
  return kill(pid, 0) == 0;
}

static int check_required_files(const struct daemon_options *opts)
{
  int i;

  if (opts == 0)
    return 1;
  for (i = 0; i < opts->required_count; i++) {
    int fd = open(opts->required_paths[i], O_RDONLY, 0);

    if (fd < 0)
      return 1;
    close(fd);
  }
  return 0;
}

static int daemon_wait_for_exit(pid_t pid, int timeout_ticks)
{
  int waited = 0;

  while (pid_running(pid) != 0 && waited < timeout_ticks) {
    sleep_ticks(1);
    waited++;
  }
  return pid_running(pid) == 0 ? 0 : 1;
}

static int redirect_stdin_to_eof(int *saved_stdin)
{
  int pipefd[2];

  if (saved_stdin == 0)
    return -1;
  if (pipe(pipefd) < 0)
    return -1;
  set_foreground_pid(STDIN_FILENO, 0);
  *saved_stdin = swap_fd(STDIN_FILENO, pipefd[0]);
  close(pipefd[0]);
  close(pipefd[1]);
  return *saved_stdin < 0 ? -1 : 0;
}

static int daemon_status(const struct daemon_options *opts)
{
  pid_t pid;
  int pidfile_status;

  pidfile_status = read_pidfile(opts->pidfile, &pid);
  if (pidfile_status == -1)
    return 3;
  if (pidfile_status == -2)
    return 4;
  if (pid_running(pid) == 0) {
    unlink(opts->pidfile);
    return 3;
  }
  return 0;
}

static int daemon_start(const struct daemon_options *opts)
{
  pid_t existing;
  char *exec_argv[2];
  int pidfile_status;
  int saved_stdin = -1;
  int saved_stdout = -1;
  int saved_stderr = -1;
  int stdout_fd = -1;
  int stderr_fd = -1;
  int flags = O_CREAT | O_WRONLY | O_APPEND;
  pid_t pid;

  pidfile_status = read_pidfile(opts->pidfile, &existing);
  if (pidfile_status == 0 && pid_running(existing) != 0)
    return 0;

  unlink(opts->pidfile);
  if (check_required_files(opts) != 0)
    return 1;
  if (redirect_stdin_to_eof(&saved_stdin) < 0)
    return 1;
  if (opts->stdout_path[0] != '\0') {
    stdout_fd = open(opts->stdout_path, flags, 0644);
    if (stdout_fd < 0) {
      restore_fd(STDIN_FILENO, saved_stdin);
      return 1;
    }
    saved_stdout = swap_fd(STDOUT_FILENO, stdout_fd);
    close(stdout_fd);
    if (saved_stdout < 0) {
      restore_fd(STDIN_FILENO, saved_stdin);
      return 1;
    }
  }
  if (opts->stderr_path[0] != '\0') {
    stderr_fd = open(opts->stderr_path, flags, 0644);
    if (stderr_fd < 0) {
      restore_fd(STDIN_FILENO, saved_stdin);
      restore_fd(STDOUT_FILENO, saved_stdout);
      return 1;
    }
    saved_stderr = swap_fd(STDERR_FILENO, stderr_fd);
    close(stderr_fd);
    if (saved_stderr < 0) {
      restore_fd(STDIN_FILENO, saved_stdin);
      restore_fd(STDOUT_FILENO, saved_stdout);
      return 1;
    }
  }

  exec_argv[0] = (char *)opts->exec_path;
  exec_argv[1] = 0;
  pid = execve(opts->exec_path, exec_argv, 0);
  restore_fd(STDIN_FILENO, saved_stdin);
  restore_fd(STDOUT_FILENO, saved_stdout);
  restore_fd(STDERR_FILENO, saved_stderr);
  if (pid < 0)
    return 1;
  if (write_pidfile(opts->pidfile, pid) < 0)
    return 1;
  return 0;
}

static int daemon_stop(const struct daemon_options *opts)
{
  pid_t pid;
  int pidfile_status;

  pidfile_status = read_pidfile(opts->pidfile, &pid);
  if (pidfile_status == -1)
    return 3;
  if (pidfile_status == -2)
    return 4;
  if (pid_running(pid) == 0) {
    unlink(opts->pidfile);
    return 3;
  }
  if (kill(pid, SIGTERM) < 0)
    return 1;
  if (daemon_wait_for_exit(pid, opts->stop_timeout_ticks) != 0) {
    if (kill(pid, SIGKILL) < 0)
      return 1;
    if (daemon_wait_for_exit(pid, DAEMON_KILL_TIMEOUT_TICKS) != 0)
      return 1;
  }
  unlink(opts->pidfile);
  return 0;
}

static int daemon_force_reload(const struct daemon_options *opts)
{
  int status = daemon_status(opts);

  if (status != 0)
    return status;
  status = daemon_stop(opts);
  if (status != 0)
    return status;
  return daemon_start(opts);
}

int main(int argc, char **argv)
{
  struct daemon_options opts;
  int status;

  if (parse_options(argc, argv, &opts) < 0) {
    printf("usage: start-stop-daemon --service name --action start|stop|restart|force-reload|status --exec path --pidfile path [--stdout path] [--stderr path] [--require path] [--stop-timeout ticks]\n");
    exit(4);
    return 4;
  }

  if (strcmp(opts.action, "start") == 0) {
    status = daemon_start(&opts);
  } else if (strcmp(opts.action, "stop") == 0) {
    status = daemon_stop(&opts);
  } else if (strcmp(opts.action, "restart") == 0) {
    status = daemon_stop(&opts);
    if (status == 0 || status == 3)
      status = daemon_start(&opts);
  } else if (strcmp(opts.action, "force-reload") == 0) {
    status = daemon_force_reload(&opts);
  } else if (strcmp(opts.action, "status") == 0) {
    status = daemon_status(&opts);
  } else {
    status = 4;
  }

  exit(status);
  return status;
}
