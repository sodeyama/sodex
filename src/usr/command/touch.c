#include <stdlib.h>
#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv)
{
  int fd;

  if (argc < 2) {
    printf("usage: touch <path>\n");
    exit(1);
    return 1;
  }

  fd = open(argv[1], O_CREAT | O_WRONLY, 0644);
  if (fd < 0) {
    printf("touch: failed %s\n", argv[1]);
    exit(1);
    return 1;
  }

  close(fd);
  exit(0);
  return 0;
}
