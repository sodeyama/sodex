/*
 *  @File chdir.c
 *  @Brief system call functions
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/10/06  update: 2007/10/06
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <vga.h>
#include <ext3fs.h>
#include <fs.h>
#include <process.h>
#include <chdir.h>

PRIVATE char* __create_absolute_path(char* path);
PRIVATE int __path_walk(char* abs_path, ext3_dentry* dentry);

PUBLIC int sys_chdir(char* path)
{
  ext3_dentry* new_dentry;
  if (path[0] == '/') {
    char* absolute_path = __create_absolute_path(path);
    new_dentry = get_dentry_absolutely(absolute_path);
  } else if (strcmp(path, ".") == 0) {
    new_dentry = current->dentry;
  } else if (strcmp(path, "..") == 0) {
    ext3_dentry* parent = current->dentry->d_parent;
    if (parent == NULL)
      new_dentry = current->dentry;
    else
      new_dentry = parent;
  } else {
    new_dentry = get_dentry_from_current(path);
  }

  if (new_dentry == NULL) {
    return FALSE;
  } else {
    current->dentry = new_dentry;

    /* We set the all process to new_dentry at once.
     *  Afterwords, we will modify setting related process
     *  to  new dentry.
     */
    struct dlist_set* plist = &(current->run_list);
    while (TRUE) {
      struct task_struct* proc =
        dlist_entry(plist, struct task_struct, run_list);
      proc->dentry = new_dentry;
      plist = plist->next;
      if (plist == &(current->run_list))
        break;
    }
    return TRUE;
  }
}

PRIVATE char* __create_absolute_path(char* path)
{
  char* absolute_path = kalloc(PATHNAME_MAX);
  memset(absolute_path, 0, PATHNAME_MAX);
  int ret = __path_walk(absolute_path, current->dentry);
  if (strcmp(path, "/") == 0)
    return absolute_path;
  if (ret == ROOT) {
    memcpy(absolute_path+strlen(absolute_path), path, strlen(path));
  } else {
    memcpy(absolute_path+strlen(absolute_path), "/", 1);
    memcpy(absolute_path+strlen(absolute_path), path, strlen(path));
  }
  return absolute_path;
}

PRIVATE int __path_walk(char* abs_path, ext3_dentry* dentry)
{
  int ret;
  ext3_dentry* parent = dentry->d_parent;
  if (parent != NULL) {
    ret = __path_walk(abs_path, parent);
  }
  if (strcmp(dentry->d_name, "/") == 0) {
    memcpy(abs_path, "/", 1);
    return ROOT;
  } else {
    if (ret != ROOT)
      memcpy(abs_path+strlen(abs_path), "/", 1);
    memcpy(abs_path+strlen(abs_path), dentry->d_name, dentry->d_namelen);
    return NOTROOT;
  }
}

