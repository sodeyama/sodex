#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>
#include <process.h>

#define BUF_SIZE 512

int main(int argc, char **argv)
{
  char buf[BUF_SIZE];
  int fd = 0;
  int read_len;

  if (argc >= 2) {
    fd = open(argv[1], 0, 0);
    if (fd < 0) {
      printf("cat: open failed %s\n", argv[1]);
      exit(1);
      return 1;
    }
  }

  while ((read_len = read(fd, buf, BUF_SIZE)) > 0) {
    write(1, buf, read_len);
  }

  if (argc >= 2)
    close(fd);
  exit(0);
  return 0;
}
