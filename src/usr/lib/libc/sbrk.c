#include <brk.h>
#include <sbrk.h>
#include <stdlib.h>

void *sbrk(intptr_t increment)
{
  if (last_alloc_addr == 0)
    last_alloc_addr = ((struct task_struct*)getpstat())->allocpoint;

  u_int32_t end_data_segment = last_alloc_addr + increment;
  if (brk((void*)end_data_segment) == -1)
    return NULL;

  u_int32_t ret_addr = last_alloc_addr;
  last_alloc_addr = CEIL(end_data_segment, BLOCK_SIZE);
  return (void*)ret_addr;
}
