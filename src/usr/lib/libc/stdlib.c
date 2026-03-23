#include <stdlib.h>
#include <string.h>

#ifdef TEST_BUILD
extern char **environ;
#else
char **environ = 0;
#endif

static int stdlib_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' ||
         ch == '\r' || ch == '\f' || ch == '\v';
}

static int stdlib_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

int atoi(const char* nptr)
{
  int sign = 1;
  int value = 0;
  int index = 0;

  if (nptr == 0)
    return 0;

  while (stdlib_is_space(nptr[index]))
    index++;

  if (nptr[index] == '-' || nptr[index] == '+') {
    if (nptr[index] == '-')
      sign = -1;
    index++;
  }

  while (stdlib_is_digit(nptr[index])) {
    value = value * 10 + (nptr[index] - '0');
    index++;
  }

  return value * sign;
}

int is_number(const char* nptr)
{
  int i;

  if (nptr == 0 || nptr[0] == '\0')
    return 0;

  for (i = 0; nptr[i] != '\0'; i++) {
    if (stdlib_is_digit(nptr[i]) == 0)
      return 0;
  }
  return 1;
}

char *getenv(const char *name)
{
  int name_len;
  int i;

  if (name == 0 || name[0] == '\0' || environ == 0)
    return 0;
  name_len = (int)strlen(name);
  for (i = 0; environ[i] != 0; i++) {
    char *entry = environ[i];

    if (entry == 0)
      continue;
    if (strncmp(entry, name, (size_t)name_len) != 0)
      continue;
    if (entry[name_len] != '=')
      continue;
    return entry + name_len + 1;
  }
  return 0;
}
