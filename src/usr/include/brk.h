#ifndef _BRK_H
#define _BRK_H

#define BRK_SUCCESS 0
#define BRK_ERROR -1
#define BLOCK_SIZE 4096

#include <sys/types.h>

int brk(void* end_data_segment);

#endif
