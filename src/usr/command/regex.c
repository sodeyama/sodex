#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>
#include <process.h>
#include <regex.h>

int main(int argc, char **argv)
{
  int result = regex(argv[1], argv[2]);
  printf("The result is %x\n", result);
  exit(1);
  return 0;
}

