#include <math.h>

int pow(int x, int y)
{
  int ret = 1;
  int i;
  for (i = 0; i < y; i++)
	ret *= x;
  return ret;
}	
