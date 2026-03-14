#include <stdlib.h>
#include <stdio.h>

static const char demo_text[] =
  "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e "
  "\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86\xe3\x81\x88\xe3\x81\x8a "
  "ABC\n";

int main(void)
{
  write(1, demo_text, sizeof(demo_text) - 1);
  exit(0);
  return 0;
}
