#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
  char *sig_str = NULL, *pid_str = NULL;
  if (argv[1][0] == '-') {
    sig_str = argv[1]+1;
    pid_str = argv[2];
  } else {
    pid_str = argv[1];
  }

  if (!is_number(pid_str)) {
    printf("error: wrong pid\n");
    printf("usage: kill [-SIGNAL] [pid]\n");
    exit(-1);
    return -1;
  }

  if (sig_str != NULL && !is_number(sig_str)) {
    printf("error: wrong signal\n");
    printf("usage: kill [-SIGNAL] [pid]\n");
    exit(-1);
    return -1;
  }

  pid_t pid = atoi(pid_str);
  int signal;
  if (sig_str != NULL)
    signal = atoi(sig_str);
  else
    signal = SIGKILL;

  int ret = kill(pid, signal);
  if (ret == -1)
    printf("kill fail\n");

  exit(1);
  return 0;
}
