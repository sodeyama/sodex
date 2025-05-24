#ifndef _BRK_H
#define _BRK_H

#define BRK_SUCCESS 0
#define BRK_ERROR -1

int sys_brk(void* end_data_segment);

#endif
