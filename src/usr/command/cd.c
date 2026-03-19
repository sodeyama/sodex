#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>

int main(int argc, char** argv)
{
  const char *path = "/";
  int ret;

  if (argc > 1)
    path = argv[1];

  ret = chdir((char *)path);
  if (ret < 0)
    printf("chdir error\n");
  exit(ret < 0 ? 1 : 0);
  return 0;
}
