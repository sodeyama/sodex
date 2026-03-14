#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: rmdir <path>\n");
    exit(1);
    return 1;
  }

  if (rmdir(argv[1]) < 0) {
    printf("rmdir: failed %s\n", argv[1]);
    exit(1);
    return 1;
  }

  exit(0);
  return 0;
}
