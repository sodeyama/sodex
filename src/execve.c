/*
 *  @File execve.c
 *  @Brief execve
 *
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/10/03  update: 2007/10/03
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
#include <signal.h>
#include <execve.h>
#include <tty.h>

PRIVATE struct task_struct* __execve(const char *filename, char *const argv[],
                                     char *const envp[],
                                     struct tty *stdio_tty);
PRIVATE void set_default_sigaction(struct task_struct* task);
PRIVATE u_int32_t get_proc_stackmem(u_int32_t *pg_dir);
PRIVATE int get_argc(char *const argv[]);
PRIVATE char** get_argv(char *const argv[]);
PRIVATE pid_t alloc_pid();
PRIVATE struct tty *inherit_stdio_tty(struct tty *stdio_tty);

PUBLIC pid_t sys_fork()
{
  pid_t pid = 0;
  return pid;
}

PUBLIC void kernel_execve(const char *filename, char *const argv[],
                          char *const envp[])
{
  struct task_struct* kern_task;

  disable_pic_interrupt(IRQ_TIMER);
  kern_task = __execve(filename, argv, envp, NULL);
  if (kern_task == NULL) {
    enable_pic_interrupt(IRQ_TIMER);
    return;
  }
  current = kern_task;
  enable_pic_interrupt(IRQ_TIMER);
}

PUBLIC pid_t kernel_execve_tty(const char *filename, char *const argv[],
                               struct tty *stdio_tty)
{
  struct task_struct* kern_task;

  if (stdio_tty == NULL)
    return -1;

  disable_pic_interrupt(IRQ_TIMER);
  kern_task = __execve(filename, argv, NULL, stdio_tty);
  if (kern_task == NULL) {
    enable_pic_interrupt(IRQ_TIMER);
    return -1;
  }

  dlist_insert_after(&(kern_task->run_list), &(current->run_list));
  /* debug shell / SSH の top-level shell は親 wait なしで片付ける。 */
  kern_task->auto_reap = TRUE;
  enable_pic_interrupt(IRQ_TIMER);
  return kern_task->pid;
}

PUBLIC pid_t sys_execve(const char *filename, char *const argv[],
                        char *const envp[])
{
  disable_pic_interrupt(IRQ_TIMER);

  struct task_struct* kern_task = __execve(filename, argv, envp, NULL);
  if (kern_task == NULL) {
    enable_pic_interrupt(IRQ_TIMER);
    return -1;
  }
  dlist_insert_after(&(kern_task->run_list), &(current->run_list));

  enable_pic_interrupt(IRQ_TIMER);
  return kern_task->pid;
}

PUBLIC pid_t sys_execve_pty(const char *filename, char *const argv[],
                            int master_fd)
{
  struct tty *tty;
  struct task_struct* kern_task;

  disable_pic_interrupt(IRQ_TIMER);

  tty = tty_lookup_master(current->files, master_fd);
  if (tty == NULL) {
    enable_pic_interrupt(IRQ_TIMER);
    return -1;
  }

  kern_task = __execve(filename, argv, NULL, tty);
  if (kern_task == NULL) {
    enable_pic_interrupt(IRQ_TIMER);
    return -1;
  }

  dlist_insert_after(&(kern_task->run_list), &(current->run_list));
  enable_pic_interrupt(IRQ_TIMER);
  return kern_task->pid;
}

PRIVATE struct task_struct* __execve(const char *filename, char *const argv[],
                                     char *const envp[],
                                     struct tty *stdio_tty)
{
  struct tty *child_tty;
  /* timer IRQ の mask は caller 側で握り、run list 更新までまとめて保護する。 */
  /* set the memory translation using page feature for user process */
  // alloc 4096Byte from kernel memory manager
  u_int32_t* pg_dir = kalloc(BLOCK_SIZE*2);
  if (pg_dir == NULL) {
    _kprintf("%s: kern_task kalloc error\n", __func__);
    return NULL;
  }
  pg_dir = ((u_int32_t)pg_dir & ~(BLOCK_SIZE-1)) + BLOCK_SIZE;
  memset(pg_dir, 0, BLOCK_SIZE);
  create_kernel_page(pg_dir);


  //Afterwards, we must modify this kalloc, because task_struct must be in
  // shared memory area between stack and task_struct
  struct task_struct* kern_task = kalloc(sizeof(struct task_struct));
  if (kern_task == NULL) {
    _kprintf("%s: kern_task kalloc error\n", __func__);
    return NULL;
  }
  memset(kern_task, 0, sizeof(struct task_struct));
  kern_task->files = kalloc(sizeof(struct files_struct));
  memset(kern_task->files, 0, sizeof(struct files_struct));
  child_tty = inherit_stdio_tty(stdio_tty);
  if (current != NULL && stdio_tty == NULL) {
    files_clone(kern_task->files, current->files);
  } else if (child_tty != NULL) {
    fs_stdio_open_tty(kern_task->files, child_tty);
  } else {
    fs_stdio_open(kern_task->files);
  }

  memcpy(kern_task->filename, filename, PROC_LEN_FILENAME-1);
  if (current == NULL || current->dentry == NULL)
    kern_task->dentry = rootdir;
  else
    kern_task->dentry = current->dentry;

  u_int32_t entrypoint, loadaddr, allocation_point;
  int elf_ret;
  {
    struct task_struct *saved_current = current;
    current = kern_task;
    elf_ret = elf_loader(filename, &entrypoint, &loadaddr, pg_dir, kern_task,
                         &allocation_point);
    current = saved_current;
  }
  if (elf_ret == ELF_FAIL)
	return NULL;
  kern_task->allocpoint = allocation_point;
  init_dlist_set(&(kern_task->run_list));
  init_dlist_set(&(kern_task->children));
  init_dlist_set(&(kern_task->sibling));
  kern_task->pid = alloc_pid();

  // 初回の kernel_execve() では current が未設定なので自分自身を親にする。
  if (current == NULL)
    kern_task->parent = kern_task;
  else
    kern_task->parent = current;

  // 親が別タスクのときだけ子リストへ繋ぐ。
  if (kern_task->parent != kern_task)
    dlist_insert_after(&(kern_task->sibling), &(kern_task->parent->children));

  kern_task->context = kalloc(sizeof(struct hard_context));
  if (kern_task->context == NULL) {
    _kprintf("%s: kern_task->context kalloc error\n", __func__);
    return NULL;
  }
  memset(kern_task->context, 0, sizeof(struct hard_context));
  kern_task->context->eip = entrypoint;
  kern_task->context->esp = get_proc_stackmem(pg_dir);
  kern_task->context->cr3 = (u_int32_t)pg_dir - __PAGE_OFFSET;
  kern_task->context->cs = __USER_CS;
  kern_task->context->ds = __USER_DS;
  kern_task->context->eflags = DEFAULT_EFLAGS;
  kern_task->context->eax = get_argc(argv);
  kern_task->context->ebx = get_argv(argv);
  kern_task->is_usermode = OUTER_PRIVILEGE;
  kern_task->state = TASK_RUNNING;

  set_default_sigaction(kern_task);

  kern_task->esp0 = kalloc(BLOCK_SIZE * (PROC_KERNEL_STACK_PAGES + 1));
  if (kern_task->esp0 == NULL) {
    _kprintf("%s: kern_task->esp0 kalloc error\n", __func__);
    return NULL;
  }
  // 割り込みと exec の深い call stack に備えて、kernel stack は複数 page 分を確保する。
  kern_task->esp0 =
      (kern_task->esp0 & ~(BLOCK_SIZE - 1)) +
      (BLOCK_SIZE * PROC_KERNEL_STACK_PAGES);

  return kern_task;
}

PRIVATE void set_default_sigaction(struct task_struct* task)
{
  int i;
  for (i=0; i<MAX_SIGNALS; i++) {
    task->sigactions[i] = signal_dummy;
  }
  task->sigactions[SIGHUP-1] = task_exit;
  task->sigactions[SIGINT-1] = task_exit;
  task->sigactions[SIGQUIT-1] = core_dump;
  task->sigactions[SIGILL-1] = core_dump;
  task->sigactions[SIGTRAP-1] = core_dump;
  task->sigactions[SIGIOT-1] = core_dump;
  task->sigactions[SIGFPE-1] = core_dump;
  task->sigactions[SIGSEGV-1] = core_dump;
  task->sigactions[SIGPIPE-1] = task_exit;
  task->sigactions[SIGALRM-1] = task_exit;
  task->sigactions[SIGTERM-1] = task_exit;
}

PRIVATE u_int32_t get_proc_stackmem(u_int32_t *pg_dir)
{
  set_process_page(pg_dir, PROC_STACK-PROC_STACK_SIZE, PROC_STACK_SIZE);

  return PROC_STACK;
}

//temporary function, we'll afterwards modify this function.
PRIVATE pid_t alloc_pid()
{
  static pid_t pid = 0;
  return (pid++);
}

PRIVATE struct tty *inherit_stdio_tty(struct tty *stdio_tty)
{
  struct tty *tty = stdio_tty;

  if (tty != NULL)
    return tty;
  if (current == NULL || current->files == NULL)
    return NULL;

  tty = tty_lookup_file(current->files, STDIN_FILENO);
  if (tty == NULL)
    tty = tty_lookup_file(current->files, STDOUT_FILENO);
  if (tty == NULL)
    tty = tty_lookup_file(current->files, STDERR_FILENO);
  return tty;
}

PRIVATE int get_argc(char *const argv[])
{
  if (argv == NULL)
    return 0;

  int i;
  for (i=0; ;i++) {
    if (argv[i] == NULL)
      break;
  }
  return i;
}

PRIVATE char** get_argv(char *const argv[])
{
  if (argv == NULL)
    return NULL;
  char **ret_argv = kalloc(ARGV_MAX_NUMS*4);
  if (ret_argv == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  int i;
  for (i=0; ;i++) {
    if (argv[i] == NULL) {
      ret_argv[i] = NULL;
      break;
    }
    ret_argv[i] = kalloc(ARGV_MAX_LEN);
    memset(ret_argv[i], 0, ARGV_MAX_LEN);
    memcpy(ret_argv[i], argv[i], strlen(argv[i]));
  }
  /*
  for (i=0;;i++) {
    if (ret_argv[i] == NULL)
      break;
    _kprintf("ret_argv[%x] %x %s\n", i, ret_argv[i], ret_argv[i]);
  }
  */
  return ret_argv;
}
