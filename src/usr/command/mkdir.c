#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: mkdir <path>\n");
    exit(1);
    return 1;
  }

  if (mkdir(argv[1], 0755) < 0) {
    printf("mkdir: failed %s\n", argv[1]);
    exit(1);
    return 1;
  }

  exit(0);
  return 0;
}
