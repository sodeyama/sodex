int main(int argc, char** argv)
{
  execve("/usr/bin/eshell", 0, 0);
  for(;;);
  return 0;
}
