#ifndef _PROCESS_H
#define _PROCESS_H

#include <sodex/const.h>
#include <sodex/list.h>
#include <sys/types.h>
#include <signal.h>
#include <ext3fs.h>
#include <fs.h>

#define MAXPROCESS 3
#define STACKSIZE 1024
#define EFLAGS_IF_ENABLE 0x200

#define PROC_STACK  0xC0000000
#define PROC_STACK_SIZE 0x2000

#define PROC_LEN_FILENAME 32

#define ARGV_MAX_NUMS 4
#define ARGV_MAX_LEN 16


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
  u_int32_t         signal;
  struct sigaction* sigactions[MAX_SIGNALS];
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

#endif /* _PROCESS_H */
