int main(int argc, char** argv)
{
  execve("/usr/bin/sshd", 0, 0);
  if (execve("/usr/bin/term", 0, 0) < 0) {
    execve("/usr/bin/eshell", 0, 0);
  }
  for(;;);
  return 0;
}
