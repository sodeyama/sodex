/*
 *  @File semaphore.c
 *  @Brief semaphore
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/08/14  update: 2007/08/14
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <semaphore.h>
#include <io.h>

PUBLIC void binary_up(int *binary)
{
  disableInterrupt();
  while (*binary == 1) {
    //sleep();
  }
  *binary++;
  enableInterrupt();
}

PUBLIC void binary_down(int *binary)
{
  disableInterrupt();
  while (*binary == 0) {
    //sleep();
  }
  *binary--;
  enableInterrupt();
}
