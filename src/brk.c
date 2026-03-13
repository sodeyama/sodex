/*
 *  @File brk.c
 *  @Brief memory allocation function for user process
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/10/12  update: 2007/10/12
 *      
 *  Copyright (C) 2007 Sodex
 */
#include <sys/types.h>
#include <process.h>
#include <page.h>
#include <brk.h>

int sys_brk(void* end_data_segment)
{
  u_int32_t last_block = current->allocpoint;
  u_int32_t target = (u_int32_t)end_data_segment;
  if (last_block < target) {
    u_int32_t newsize = target - last_block;
    u_int32_t pg_dir = pg_get_cr3() + __PAGE_OFFSET;
    set_process_page(pg_dir, last_block, newsize);
    pg_load_cr3(pg_dir);
    current->allocpoint += newsize;
    return BRK_SUCCESS;
  }
  return BRK_ERROR;
}
