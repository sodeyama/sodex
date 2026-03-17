#ifndef _PROCESS_H
#define _PROCESS_H

#include <sodex/const.h>
#include <sodex/list.h>
#include <sys/types.h>
#include <signal.h>
#include <ext3fs.h>
#include <fs.h>

struct tty;

#define HZ  100

#define MAXPROCESS 3
#define STACKSIZE 1024
#define EFLAGS_IF_ENABLE 0x200

#define PROC_STACK  0xC0000000
#define PROC_STACK_SIZE 0x40000
/* SSH の KEX と audit 出力で kernel stack が深くなるため余裕を持たせる。 */
#define PROC_KERNEL_STACK_PAGES 8

#define PROC_LEN_FILENAME 32

#define DEFAULT_EFLAGS   0x246

#define TASK_RUNNING            0
#define TASK_INTERRUPTIBLE      1
#define TASK_UNINTERRUPTIBLE    2
#define TASK_ZOMBIE             3
#define TASK_STOPPED            4

#define ERROR_WAITPID       -1
#define WNOHANG             1

struct wait_queue {
    struct task_struct *task;
    struct wait_queue *next;
};

typedef struct _TSS {
  u_int16_t backlink;  u_int16_t dummy1;
  u_int32_t esp0;
  u_int16_t ss0;        u_int16_t dummy2;
  u_int32_t esp1;
  u_int16_t ss1;        u_int16_t dummy3;
  u_int32_t esp2;
  u_int16_t ss2;        u_int16_t dummy4;
  u_int32_t cr3;
  u_int32_t eip;
  u_int32_t eflags;
  u_int32_t eax;
  u_int32_t ecx;
  u_int32_t edx;
  u_int32_t ebx;
  u_int32_t esp;
  u_int32_t ebp;
  u_int32_t esi;
  u_int32_t edi;
  u_int16_t es;         u_int16_t dummy5;
  u_int16_t cs;         u_int16_t dummy6;
  u_int16_t ss;         u_int16_t dummy7;
  u_int16_t ds;         u_int16_t dummy8;
  u_int16_t fs;         u_int16_t dummy9;
  u_int16_t gs;         u_int16_t dummy10;
  u_int16_t ldt;        u_int16_t dummy11;
  u_int16_t t;          u_int16_t iobase;
} TSS;

typedef struct _LTSS {
  u_int32_t eip;
  u_int32_t esp;
  u_int32_t cr3;
} LTSS;

struct task_struct {
  u_int32_t         count;
  pid_t             pid;
  char              filename[PROC_LEN_FILENAME];
  struct _ext3_dentry *dentry;	// current directory
  struct task_struct  *parent;
  struct hard_context *context; // hardware context
  struct files_struct *files;	// opened file descriptors
  struct dlist_set  run_list;	// double linked list for processes
  struct dlist_set  children;	// list of my children
  struct dlist_set  sibling;  	// linkage in my parent's children list
  u_int32_t         esp0;
  int               is_usermode;// if current process is user mode,
                                // the is_user is true, else the is_user is false
  int               firstexec;
  u_int32_t         allocpoint;
  u_int32_t         utime;
  u_int32_t         stime;
  u_int32_t         state;
  int               auto_reap;
  int               exit_status;
  u_int32_t         signal;
  sighandler_t      sigactions[MAX_SIGNALS];
  struct wait_queue *child_wait;
};

struct hard_context {
  u_int32_t eip;
  u_int32_t esp;
  u_int32_t cr3;
  u_int16_t cs;  
  u_int16_t ds;  
  u_int32_t eflags;
  u_int32_t eax;
  u_int32_t ebx;
  u_int32_t ecx;
  u_int32_t edx;
  u_int32_t ebp;
  u_int32_t esi;
  u_int32_t edi;
};

struct pid_hash {
  pid_t	pid_hash;
  pid_t	pid;
  struct task_struct* task;
  struct dlist_set list;
};

PUBLIC struct task_struct* current;// = (struct task_struct *)0;

PUBLIC void init_process();
PUBLIC void schedule();
PUBLIC void ltr(u_int16_t selector);
PUBLIC void set_ltss(LTSS* ltss, u_int32_t eip,u_int32_t esp, u_int32_t cr3);
PUBLIC void switch_to_outer_privilege(u_int16_t cs, u_int16_t ds,
                                      u_int32_t esp, u_int32_t cr3,
                                      u_int32_t eflags, u_int32_t eip,
                                      u_int32_t ebp, u_int32_t next_eax,
                                      u_int32_t next_ebx, u_int32_t next_ecx,
                                      u_int32_t next_edx, u_int32_t next_esp,
                                      u_int32_t next_ebp, u_int32_t next_esi,
                                      u_int32_t next_edi);
PUBLIC void switch_to_same_privilege(u_int16_t cs, u_int32_t cr3,
                                     u_int32_t eflags, u_int32_t eip,
                                     u_int32_t ebp, u_int32_t next_eax,
                                     u_int32_t next_ebx, u_int32_t next_ecx,
                                     u_int32_t next_edx, u_int32_t next_esp,
                                     u_int32_t next_ebp, u_int32_t next_esi,
                                     u_int32_t next_edi);
PUBLIC void save_process(int is_usermode, u_int32_t iret_eip,
                         u_int32_t iret_cs, u_int32_t iret_eflags,
                         u_int32_t iret_esp, u_int32_t iret_ss,
                         u_int32_t ebp);
PUBLIC void set_context(struct task_struct* task, u_int32_t eip, u_int32_t esp,
                        u_int32_t eflags);
PUBLIC void to_usermode();
PUBLIC void to_kernelmode();
PUBLIC int is_usermode();
PUBLIC void sys_exit(int status);
PUBLIC int sys_waitpid(pid_t pid, int *status, int options);
PUBLIC void sleep_on(struct wait_queue **wq);
PUBLIC void sleep_on_timeout(struct wait_queue **wq, u_int32_t ticks);
PUBLIC void wakeup(struct wait_queue **wq);
PUBLIC struct task_struct *process_find_pid(pid_t pid);
PUBLIC int process_has_pid(pid_t pid);
PUBLIC struct task_struct *process_init_task(void);

PUBLIC volatile u_int32_t kernel_tick;
PUBLIC volatile int process_in_timer_interrupt;

#define SAME_PRIVILEGE 0
#define OUTER_PRIVILEGE 1

#endif /* _PROCESS_H */
