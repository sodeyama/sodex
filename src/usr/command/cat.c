#include <stdlib.h>
#include <string.h>
#include <process.h>

#define BUF_SIZE 512

static int report_open_failed(const char *path)
{
  write(2, "cat: open failed ", 17);
  write(2, path, strlen(path));
  write(2, "\n", 1);
  return 1;
}

static void cat_fd(int fd)
{
  char buf[BUF_SIZE];
  int read_len;

  while ((read_len = read(fd, buf, BUF_SIZE)) > 0) {
    write(1, buf, read_len);
  }
}

int main(int argc, char **argv)
{
  int i;

  if (argc < 2) {
    cat_fd(0);
    exit(0);
    return 0;
  }

  for (i = 1; i < argc; i++) {
    int fd = open(argv[i], 0, 0);
    if (fd < 0) {
      exit(report_open_failed(argv[i]));
      return 1;
    }
    cat_fd(fd);
    close(fd);
  }

  exit(0);
  return 0;
}
