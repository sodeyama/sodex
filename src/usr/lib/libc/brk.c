#include <brk.h>
#include <stdlib.h>

int brk(void* end_data_segment)
{
  u_int32_t alloc_block = CEIL((u_int32_t)end_data_segment, BLOCK_SIZE);
  return _brk((void*)alloc_block);
}
