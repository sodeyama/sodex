#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <malloc.h>
#include <fs.h>
#include <init_policy.h>
#include <sleep.h>

#define INIT_DEFAULT_HOME "/home/user"

static void init_debug_log(const char *text)
{
  if (text == 0)
    return;
  debug_write(text, strlen(text));
}

static void init_debug_value(const char *label, const char *value)
{
  if (label == 0 || value == 0)
    return;
  debug_write(label, strlen(label));
  debug_write(value, strlen(value));
  debug_write("\n", 1);
}

static char *init_read_fd_all(int fd)
{
  char *buf;
  int cap = 256;
  int len = 0;

  buf = (char *)malloc((size_t)cap);
  if (buf == 0)
    return 0;

  while (1) {
    int read_len;

    if (len >= cap - 1) {
      char *next;
      int next_cap = cap * 2;

      next = (char *)malloc((size_t)next_cap);
      if (next == 0) {
        free(buf);
        return 0;
      }
      memset(next, 0, (size_t)next_cap);
      memcpy(next, buf, (size_t)len);
      free(buf);
      buf = next;
      cap = next_cap;
    }

    read_len = (int)read(fd, buf + len, (size_t)(cap - len - 1));
    if (read_len <= 0)
      break;
    len += read_len;
  }

  buf[len] = '\0';
  return buf;
}

static void init_load_policy(struct init_inittab *inittab)
{
  int fd;
  char *text;

  init_inittab_init(inittab);
  fd = open("/etc/inittab", O_RDONLY, 0);
  if (fd < 0)
    return;

  text = init_read_fd_all(fd);
  close(fd);
  if (text == 0)
    return;
  init_policy_parse_inittab(text, inittab);
  free(text);
}

static int init_split_command(char *buf, char **argv, int max_args)
{
  int argc = 0;
  int index = 0;

  if (buf == 0 || argv == 0 || max_args <= 0)
    return 0;

  while (buf[index] != '\0' && argc < max_args - 1) {
    while (buf[index] == ' ' || buf[index] == '\t')
      index++;
    if (buf[index] == '\0')
      break;
    argv[argc++] = &buf[index];
    while (buf[index] != '\0' && buf[index] != ' ' && buf[index] != '\t')
      index++;
    if (buf[index] == '\0')
      break;
    buf[index++] = '\0';
  }
  argv[argc] = 0;
  return argc;
}

static void init_restore_default_dir(void)
{
  if (chdir(INIT_DEFAULT_HOME) == 0)
    return;
  chdir("/");
}

static pid_t init_spawn_command(const char *command)
{
  char buf[INIT_POLICY_CMD_MAX];
  char *argv[8];
  int argc;
  pid_t pid;

  if (command == 0 || command[0] == '\0')
    return -1;

  memset(buf, 0, sizeof(buf));
  memcpy(buf, command, strlen(command) < sizeof(buf) - 1 ?
         strlen(command) : sizeof(buf) - 1);
  argc = init_split_command(buf, argv, 8);
  if (argc <= 0)
    return -1;
  init_restore_default_dir();
  pid = execve(argv[0], argv, 0);
  if (pid < 0) {
    init_debug_value("AUDIT init_spawn_failed=", argv[0]);
    set_foreground_pid(STDIN_FILENO, 0);
    return -1;
  }
  init_debug_value("AUDIT init_spawned=", argv[0]);
  set_foreground_pid(STDIN_FILENO, pid);
  return pid;
}

static int init_run_boot_script(const struct init_inittab *inittab)
{
  char *argv[4];
  int status = 0;
  pid_t pid;

  init_debug_log("AUDIT init_rc_begin\n");
  init_debug_value("AUDIT init_runlevel=", inittab->runlevel);
  argv[0] = "sh";
  argv[1] = (char *)inittab->sysinit;
  argv[2] = (char *)inittab->runlevel;
  argv[3] = 0;
  pid = execve("/usr/bin/sh", argv, 0);
  if (pid < 0) {
    init_debug_log("AUDIT init_rc_spawn_failed\n");
    return 1;
  }
  if (waitpid(pid, &status, 0) < 0)
    status = 1;
  init_restore_default_dir();
  if (status == 0)
    init_debug_log("AUDIT init_rc_done ok\n");
  else
    init_debug_log("AUDIT init_rc_done fail\n");
  return status;
}

static const char *init_select_respawn(const struct init_inittab *inittab,
                                       int boot_status)
{
  const char *respawn;

  respawn = init_policy_find_respawn(inittab, inittab->runlevel);
  if (boot_status == 0)
    return respawn;

  respawn = init_policy_find_respawn(inittab, "rescue");
  if (respawn != 0 && respawn[0] != '\0') {
    init_debug_log("AUDIT init_enter_rescue\n");
    return respawn;
  }
  init_debug_log("AUDIT init_rescue_missing\n");
  return init_policy_find_respawn(inittab, inittab->runlevel);
}

int main(int argc, char **argv)
{
  struct init_inittab inittab;
  const char *respawn;
  pid_t foreground_pid;
  int boot_status;
  int status;

  (void)argc;
  (void)argv;

  init_load_policy(&inittab);
  boot_status = init_run_boot_script(&inittab);
  respawn = init_select_respawn(&inittab, boot_status);
  foreground_pid = init_spawn_command(respawn);
  for (;;) {
    pid_t pid = waitpid(-1, &status, 0);

    if (pid < 0) {
      sleep_ticks(1);
      continue;
    }
    init_debug_log("AUDIT init_child_reaped\n");
    if (pid == foreground_pid) {
      set_foreground_pid(STDIN_FILENO, 0);
      foreground_pid = init_spawn_command(respawn);
    }
  }
  return 0;
}
