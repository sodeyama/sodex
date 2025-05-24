/*
 *  @File process.c
 *  @Brief control the process(task) using cpu's feature
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/05/09  update: 2007/05/10
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <kernel.h>
#include <sodex/list.h>
#include <ld/page_linker.h>
#include <vga.h>
#include <descriptor.h>
#include <io.h>
#include <string.h>
#include <ihandlers.h>
#include <process.h>
#include <floppy.h>
#include <fs.h>
#include <page.h>
#include <elfloader.h>
#include <execve.h>


PRIVATE TSS tss;

PRIVATE void p_print_debug(struct task_struct* prev, struct task_struct* next);
PRIVATE u_int32_t get_proc_stackmem(u_int32_t *pg_dir);
PRIVATE void set_prev_context(struct task_struct* prev, u_int16_t cs,
                              u_int16_t ds, u_int32_t eip,
                              u_int32_t eax, u_int32_t ebx, u_int32_t ecx,
                              u_int32_t edx, u_int32_t ebp, u_int32_t esp,
                              u_int32_t esi, u_int32_t edi, u_int32_t eflags,
                              int is_usermode);
PRIVATE void _exit();
PRIVATE int maxsignal(u_int32_t signal);

PUBLIC void init_process()
{
  current = (struct task_struct *)0;

  u_int16_t sel = allocSel();
  u_int16_t type_tss = 0x89;
  memset(&tss, 0, sizeof(TSS));
  tss.ss0 = __KERNEL_DS;
  u_int32_t* pg_dir = kalloc(BLOCK_SIZE*2);
  pg_dir = ((u_int32_t)pg_dir & ~(BLOCK_SIZE-1)) + BLOCK_SIZE;
  memset(pg_dir, 0, BLOCK_SIZE);
  create_kernel_page(pg_dir);
  //tss.esp0 = pg_dir;
  makeGdt((u_int32_t)&tss, sizeof(TSS), type_tss, sel);
  ltr(sel);
  kernel_execve("/usr/bin/init", NULL, NULL);
  struct task_struct* next = dlist_entry(current->run_list.next,
                                         struct task_struct, run_list);
  tss.esp0 = next->esp0;
  set_trap_gate(0x20,&asm_process_switch);
}

PUBLIC void set_context(struct task_struct* prev, u_int32_t eip, u_int32_t esp,
                        u_int32_t eflags)
{
  prev->context->eip = eip;
  prev->context->esp = esp;
  prev->context->eflags = eflags;
  current = dlist_entry(prev->run_list.next,
                        struct task_struct, run_list);
  prev->count++;
}

PRIVATE void set_prev_context(struct task_struct* prev, u_int16_t cs,
                              u_int16_t ds, u_int32_t eip,
                              u_int32_t eax, u_int32_t ebx, u_int32_t ecx,
                              u_int32_t edx, u_int32_t ebp, u_int32_t esp,
                              u_int32_t esi, u_int32_t edi, u_int32_t eflags,
                              int is_usermode)
{
  prev->context->cs = cs;
  prev->context->ds = ds;
  prev->context->eip = eip;
  prev->context->eax = eax;
  prev->context->ebx = ebx;
  prev->context->ecx = ecx;
  prev->context->edx = edx;
  prev->context->ebp = ebp;
  prev->context->esp = esp;
  prev->context->esi = esi;
  prev->context->edi = edi;
  prev->context->eflags = (eflags | 0x200);
  prev->is_usermode = is_usermode;

  //if (prev->firstexec != 0 && is_usermode == SAME_PRIVILEGE)
  //  prev->esp0 = esp;


  prev->count++;
}

PUBLIC void i20h_do_timer(int is_usermode, u_int32_t iret_eip,
                          u_int32_t iret_cs, u_int32_t iret_eflags,
                          u_int32_t iret_esp, u_int32_t iret_ss,
                          u_int32_t ebp)
{
  pic_eoi(IRQ_TIMER);
  save_process(is_usermode, iret_eip, iret_cs, iret_eflags,
               iret_esp, iret_ss, ebp);

  while (current->signal) {
    u_int32_t sig = maxsignal(current->signal)+1;
    switch (sig) {
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
    case SIGKILL:
      current->state = TASK_STOPPED;
      break;

    default:
      (current->sigactions[sig-1]->sa_handler)(sig-1);
      break;
    }
    current->signal &= (~(1<<(sig-1)));
  }

  int state = current->state;
  if (state == TASK_STOPPED) {
    //_kprintf("pid:%x task stopped\n", current->pid);
    // delete the current
    _exit();
  } else if (state == TASK_ZOMBIE) {
    // skip the current
    current = dlist_entry(current->run_list.next,
                          struct task_struct, run_list);    
  } else if (state == TASK_RUNNING) {
  } else {
    _kprintf("The number %x of task state is not implemented\n",
             state);
  }

  schedule();
}

PUBLIC void save_process(int is_usermode, u_int32_t iret_eip,
                         u_int32_t iret_cs, u_int32_t iret_eflags,
                         u_int32_t iret_esp, u_int32_t iret_ss,
                         u_int32_t ebp)
{
  /* Don't create the local variable. If u want to create the local variable
   * , check the stack and modify the position of stack at switch_to function.
   */

  struct task_struct* prev = current;
  struct task_struct* next = dlist_entry(prev->run_list.next,
                                         struct task_struct, run_list);

  u_int32_t prev_eip = prev->context->eip;
  u_int32_t prev_esp = prev->context->esp;

  u_int32_t prev_count = prev->count;

  u_int32_t *p_eax = (u_int32_t*)(ebp-4);
  u_int32_t *p_ecx = (u_int32_t*)(ebp-8);
  u_int32_t *p_edx = (u_int32_t*)(ebp-12);
  u_int32_t *p_ebx = (u_int32_t*)(ebp-16);
  u_int32_t *p_esp = (u_int32_t*)(ebp-20);
  u_int32_t *p_ebp = (u_int32_t*)(ebp-24);
  u_int32_t *p_esi = (u_int32_t*)(ebp-28);
  u_int32_t *p_edi = (u_int32_t*)(ebp-32);
  u_int32_t *prev_ebp = (u_int32_t*)(ebp);


  if (prev->firstexec != 0) { // prev already exist
    if (is_usermode == SAME_PRIVILEGE) {
      set_prev_context(prev, __KERNEL_CS, __KERNEL_DS, iret_eip, *p_eax,
                       *p_ebx, *p_ecx, *p_edx, *prev_ebp, ebp+16,
                       *p_esi, *p_edi, iret_eflags, is_usermode);
    } else {
      set_prev_context(prev, __USER_CS, __USER_DS, iret_eip, *p_eax,
                       *p_ebx, *p_ecx, *p_edx, *prev_ebp, iret_esp,
                       *p_esi, *p_edi, iret_eflags, is_usermode);
    }
  } else { // prev didn't exist
    set_prev_context(prev, __USER_CS, __USER_DS, prev_eip, next->context->eax,
                     next->context->ebx, next->context->ecx,
                     next->context->edx, *prev_ebp, prev_esp, *p_esi,
                     *p_edi, iret_eflags, OUTER_PRIVILEGE);
  }
}

PUBLIC void schedule()
{
  struct task_struct* next = dlist_entry(current->run_list.next,
                                         struct task_struct, run_list);

  u_int32_t next_eip = next->context->eip;
  u_int32_t next_esp = next->context->esp;

  u_int32_t next_cr3 = next->context->cr3;
  u_int16_t next_cs = next->context->cs;
  u_int16_t next_ds = next->context->ds;
  u_int32_t next_eflags = next->context->eflags;
  u_int32_t next_ebp = next->context->ebp;
  u_int32_t next_eax = next->context->eax;
  u_int32_t next_ebx = next->context->ebx;
  u_int32_t next_ecx = next->context->ecx;
  u_int32_t next_edx = next->context->edx;
  u_int32_t next_esi = next->context->esi;
  u_int32_t next_edi = next->context->edi;
  int next_is_usermode = next->is_usermode;

  current = next;
  tss.esp0 = next->esp0;
  if (next->firstexec == 0 || next_is_usermode == OUTER_PRIVILEGE) {
    if (next->firstexec == 0) {
      next_eflags = DEFAULT_EFLAGS;
      next->firstexec++;
    }
    //_kprintf("outer pv %x cr3:%x\n", next, next_cr3);

    switch_to_outer_privilege(next_cs, next_ds, next_esp, next_cr3, 
                              next_eflags, next_eip, next_esp,
                              next_eax, next_ebx, next_ecx, next_edx,
                              next_esp, next_ebp, next_esi, next_edi);
  } else {
    if (next->firstexec == 0)
      next->firstexec++;
    //_kprintf("same pv %x cr3:%x\n", next, next_cr3);

    switch_to_same_privilege(next_cs, next_cr3, next_eflags, next_eip,
                             next_esp,
                             next_eax, next_ebx, next_ecx, next_edx,
                             next_esp, next_ebp, next_esi, next_edi);
  }
}

PUBLIC void sys_exit()
{
  current->state = TASK_ZOMBIE;
  for(;;);
}

PRIVATE void _exit()
{
  struct task_struct* next = dlist_entry(current->run_list.next,
                                         struct task_struct, run_list);
  dlist_remove(&(current->run_list));
  current = next;
  schedule();
}

PUBLIC int sys_waitpid(pid_t pid, int *status, int options)
{
  while (TRUE) {
    struct dlist_set* plist = &(current->children);
    struct dlist_set* pos;
    // The child does't exist
    if (plist->next == plist)
      return ERROR_WAITPID;

    struct task_struct* p = NULL;
    int existflag = 0;
    dlist_for_each(pos, plist) {
      p = dlist_entry(pos, struct task_struct, sibling);
      if (p->pid == pid) {
        existflag = 1;
        break;
      }
    }
    if (existflag == 0)
      return ERROR_WAITPID;

    if (p->state == TASK_ZOMBIE) {
      p->state = TASK_STOPPED;
      return p->pid;
    }
  }
}

PUBLIC void to_usermode()
{
  current->is_usermode = TRUE;
}

PUBLIC void to_kernelmode()
{
  current->is_usermode = FALSE;
}

PUBLIC int is_usermode()
{
  return current->is_usermode;
}

PRIVATE int maxsignal(u_int32_t signal)
{
  int i;
  for(i=31; i>=0; i--) {
    if (signal & (1<<i))
      return i;
  }
  return -1;
}

PRIVATE void p_print_debug(struct task_struct* prev, struct task_struct* next)
{
  _kprintf("prev is %x, next is %x\n", prev, next);
  _kprintf("prev->cr3 is %x, next->cr3 is %x\n",
		   prev->context->cr3, next->context->cr3);
  _kprintf("prev->esp is %x, next->esp is %x\n",
           prev->context->esp, next->context->esp);
  _kprintf("prev->count is %x, next->count is %x\n",
           prev->count, next->count);
}


