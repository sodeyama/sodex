#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>
#include <process.h>

#define BUF_SIZE 512

int main(int argc, char **argv)
{
  char buf[BUF_SIZE];
  int ret;

  (void)argc;
  (void)argv;
  memset(buf, 0, BUF_SIZE);
  ret = send(buf);
  exit(ret == 0 ? 0 : 1);
  return 0;
}
