#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <key.h>
#include <tty.h>

PRIVATE int pump_master(int master_fd);
PRIVATE int pump_keys(int master_fd);
PRIVATE int translate_key(struct key_event *event, char *buf);

int main(int argc, char** argv)
{
  int master_fd;
  char *shell_argv[2];

  (void)argc;
  (void)argv;

  master_fd = openpty();
  if (master_fd < 0) {
    write(1, "term: openpty failed\n", 21);
    return execve("/usr/bin/eshell", 0, 0);
  }

  set_input_mode(INPUT_MODE_RAW);

  shell_argv[0] = "eshell";
  shell_argv[1] = NULL;
  if (execve_pty("/usr/bin/eshell", shell_argv, master_fd) < 0) {
    set_input_mode(INPUT_MODE_CONSOLE);
    write(1, "term: fallback to eshell\n", 25);
    return execve("/usr/bin/eshell", 0, 0);
  }

  write(1, "\n[rterm] PTY console beta\n", 26);
  while (TRUE) {
    int activity = 0;
    activity += pump_master(master_fd);
    activity += pump_keys(master_fd);
    if (activity == 0) {
      volatile int spin;
      for (spin = 0; spin < 200000; spin++);
    }
  }

  return 0;
}

PRIVATE int pump_master(int master_fd)
{
  char buf[128];
  int total = 0;

  while (TRUE) {
    int len = read(master_fd, buf, sizeof(buf));
    if (len <= 0)
      break;

    write(1, buf, len);
    total += len;
    if (len < sizeof(buf))
      break;
  }

  return total;
}

PRIVATE int pump_keys(int master_fd)
{
  struct key_event event;
  int total = 0;

  while (getkeyevent(&event) > 0) {
    char buf[4];
    int len = translate_key(&event, buf);
    if (len > 0) {
      write(master_fd, buf, len);
      total += len;
    }
  }

  return total;
}

PRIVATE int translate_key(struct key_event *event, char *buf)
{
  if (event->flags & KEY_EVENT_RELEASE)
    return 0;

  if (event->flags & KEY_EVENT_EXTENDED) {
    switch (event->scancode) {
    case KEY_SCANCODE_UP:
      memcpy(buf, "\x1b[A", 3);
      return 3;
    case KEY_SCANCODE_DOWN:
      memcpy(buf, "\x1b[B", 3);
      return 3;
    case KEY_SCANCODE_RIGHT:
      memcpy(buf, "\x1b[C", 3);
      return 3;
    case KEY_SCANCODE_LEFT:
      memcpy(buf, "\x1b[D", 3);
      return 3;
    default:
      return 0;
    }
  }

  if (event->ascii == 0)
    return 0;

  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'A' && event->ascii <= 'Z') {
    buf[0] = event->ascii - 'A' + 1;
    return 1;
  }
  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'a' && event->ascii <= 'z') {
    buf[0] = event->ascii - 'a' + 1;
    return 1;
  }

  buf[0] = event->ascii;
  return 1;
}
