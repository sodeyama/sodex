#include <pager.h>

int main(int argc, char **argv)
{
  return pager_command_main("more", argc, argv, 0);
}
