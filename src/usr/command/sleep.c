#include <stdio.h>
#include <stdlib.h>
#include <sleep.h>

#define SLEEP_TICKS_PER_SECOND 100

int main(int argc, char **argv)
{
  int seconds = 1;

  if (argc > 1)
    seconds = atoi(argv[1]);
  if (seconds < 0)
    seconds = 0;
  sleep_ticks((u_int32_t)seconds * SLEEP_TICKS_PER_SECOND);
  exit(0);
  return 0;
}
