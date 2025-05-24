
int main(int argc, char** argv)
{
  int fd = open("/usr/bin/hello", 0, 0);
  char buf[16];
  int ret = read(fd, buf, 16);
  buf[ret] = '\n';
  for(;;) {
    write(1, buf, ret+1);
  }
  return 0;
}
