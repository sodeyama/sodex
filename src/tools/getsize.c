#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define SECTOR 512

int main(int argc, char **argv)
{
  int fd;
  struct stat st;

  if (argc < 2) {
    printf("usage: getsize filename\n");
    exit(0);
  }

  if((fd = open(argv[1], O_RDONLY)) == -1) {
    printf("file open error. %s\n", argv[1]);
    exit(0);
  }

  fstat(fd, &st);
  close(fd);

  int ret = (st.st_size/SECTOR) + ((st.st_size%SECTOR) == 0 ? 0 : 1);

  printf("%d\n", ret);
  exit(0);
}

