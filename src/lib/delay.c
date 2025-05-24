#include <delay.h>

PUBLIC void delay(u_int32_t n)
{
  asm __volatile__ (
                    "   movl %0, %%eax   \n"
                    "1: subl $1, %%eax   \n"
                    "   jz 2f            \n" 
                    "   jmp 1b           \n"
                    "2:                  \n"
                    :: "r" (n));
}
