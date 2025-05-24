#include <stdlib.h>
#include <string.h>
#include <math.h>

int atoi(const char* nptr)
{
  int len = strlen(nptr);
  int i, ret=0;
  for (i = len-1; i>=0; i--) {
    ret += (nptr[i] - '0')*pow(10, len-1-i);
  }
  return ret;
}

int is_number(const char* nptr)
{
  int len = strlen(nptr);
  int i, ret=0;
  for (i = len-1; i>=0; i--) {
    char c = nptr[i];
    if (c < '0' || c > '9')
      return FALSE;
  }
  return TRUE;
}
