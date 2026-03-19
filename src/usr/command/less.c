#include <pager.h>

int main(int argc, char **argv)
{
  return pager_command_main("less", argc, argv, 1);
}
