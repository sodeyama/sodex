#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>

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

int main(int argc, char **argv)
{
  char path[128];
  char *sh_argv[5];
  int status = 1;
  pid_t pid;

  if (argc < 3) {
    printf("usage: service <name> <action>\n");
    exit(1);
    return 1;
  }

  copy_text(path, sizeof(path), "/etc/init.d/");
  copy_text(path + strlen(path), (int)(sizeof(path) - strlen(path)), argv[1]);
  sh_argv[0] = "sh";
  sh_argv[1] = path;
  sh_argv[2] = argv[2];
  sh_argv[3] = 0;
  pid = execve("/usr/bin/sh", sh_argv, 0);
  if (pid < 0) {
    printf("service: spawn failed\n");
    exit(1);
    return 1;
  }

  if (waitpid(pid, &status, 0) < 0)
    status = 1;
  exit(status);
  return status;
}
