#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>

#define ROOT 1
#define NOTROOT 0

static int print_dir(ext3_dentry* dentry);

int main(int argc, char** argv)
{
  ext3_dentry* dentry = (ext3_dentry*)getdentry();
  int ret = print_dir(dentry);
  if (ret == ROOT)
    puts("/\n");
  else
    puts("\n");
  exit(1);
  return 0;
}

static int print_dir(ext3_dentry* dentry)
{
  ext3_dentry* parent = dentry->d_parent;
  if (parent != NULL) {
    print_dir(parent);
  }
  if (strcmp(dentry->d_name, "/") == 0) {
    return ROOT;
  } else {
    printf("/%s", dentry->d_name);
    return NOTROOT;
  }
}
