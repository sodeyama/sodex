#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>

static void ls_walk(ext3_dentry* dentry);

int main(int argc, char** argv)
{
  ext3_dentry* dentry = (ext3_dentry*)getdentry();
  ls_walk(dentry);
  exit(1);
  return 0;
}

static void ls_walk(ext3_dentry* dentry)
{
  struct dlist_set* plist = (&(dentry->d_subdirs))->prev;
  while (TRUE) {
    ext3_dentry* pdentry = dlist_entry(plist, ext3_dentry, d_child);
    char *filetype;
    if (pdentry->d_filetype == FTYPE_FILE)
      filetype = "-";
    else if (pdentry->d_filetype == FTYPE_DIR)
      filetype = "d";
    else
      filetype = " ";
    printf("%x %s %s\n", pdentry->d_inonum, filetype, pdentry->d_name);
    plist = plist->prev;
    if (plist == &(dentry->d_subdirs))
      break;
  }
}
