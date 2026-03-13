#ifdef TEST_BUILD
typedef unsigned int size_t;
#else
#include <lib.h>
#endif

PUBLIC int pow(int x, int y)
{
  int ret = 1;
  int i;
  for (i = 0; i < y; i++)
	ret *= x;
  return ret;
}

PUBLIC int logn(int x, int y)
{
  int i;
  for (i=0; ;i++) {
    if (pow(x, i) == y)
      return i;
    else if (pow(x, i) > y)
      return -1;
  }
}
