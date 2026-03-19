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

PUBLIC int sys_chdir(char* path)
{
  ext3_dentry* new_dentry;

  if (path == NULL || path[0] == '\0')
    return -1;

  new_dentry = get_dentry_by_path(path);

  if (new_dentry == NULL || new_dentry->d_filetype != FTYPE_DIR)
    return -1;

  current->dentry = new_dentry;
  return 0;
}
