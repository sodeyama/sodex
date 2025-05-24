#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>

int main(int argc, char** argv)
{
  int ret = chdir(argv[1]);
  if (ret == FALSE)
    printf("chdir error\n");
  exit(1);
  return 0;
}
