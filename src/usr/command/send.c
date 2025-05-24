#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>
#include <process.h>

#define BUF_SIZE 512

int main(int argc, char **argv)
{
  char buf[BUF_SIZE];
  memset(buf, 0, BUF_SIZE);
  int ret = send(buf);
  exit(1);
  return 0;
}

