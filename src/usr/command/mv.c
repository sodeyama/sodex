#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: mv <old> <new>\n");
    exit(1);
    return 1;
  }

  if (rename(argv[1], argv[2]) < 0) {
    printf("mv: failed %s %s\n", argv[1], argv[2]);
    exit(1);
    return 1;
  }

  exit(0);
  return 0;
}
