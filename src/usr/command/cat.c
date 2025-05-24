#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>
#include <process.h>

#define BUF_SIZE 512

int main(int argc, char **argv)
{
  char buf[BUF_SIZE];
  int fd = open(argv[1], NULL, NULL);
  int read_len;
  read_len = read(fd, buf, BUF_SIZE);
  printf("%s", buf);
  exit(1);
  return 0;
}

