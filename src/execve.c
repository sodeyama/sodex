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
PRIVATE void free_argv_copy(char **argv);
PRIVATE void cleanup_exec_task(struct task_struct *task);
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
  int argc;
  char **argv_copy;
  /* timer IRQ の mask は caller 側で握り、run list 更新までまとめて保護する。 */
  /* set the memory translation using page feature for user process */
  // alloc 4096Byte from kernel memory manager
  void *pg_dir_raw = kalloc(BLOCK_SIZE*2);
  if (pg_dir_raw == NULL) {
    _kprintf("%s: kern_task kalloc error\n", __func__);
    return NULL;
  }
  u_int32_t* pg_dir = (u_int32_t *)(((u_int32_t)pg_dir_raw & ~(BLOCK_SIZE-1)) + BLOCK_SIZE);
  memset(pg_dir, 0, BLOCK_SIZE);
  create_kernel_page(pg_dir);


  //Afterwards, we must modify this kalloc, because task_struct must be in
  // shared memory area between stack and task_struct
  struct task_struct* kern_task = kalloc(sizeof(struct task_struct));
  if (kern_task == NULL) {
    kfree(pg_dir_raw);
    _kprintf("%s: kern_task kalloc error\n", __func__);
    return NULL;
  }
  memset(kern_task, 0, sizeof(struct task_struct));
  kern_task->pg_dir = pg_dir;
  kern_task->pg_dir_raw = pg_dir_raw;
  kern_task->files = kalloc(sizeof(struct files_struct));
  if (kern_task->files == NULL) {
    _kprintf("%s: kern_task->files kalloc error\n", __func__);
    cleanup_exec_task(kern_task);
    return NULL;
  }
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
  if (elf_ret == ELF_FAIL) {
    cleanup_exec_task(kern_task);
    return NULL;
  }
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
    cleanup_exec_task(kern_task);
    return NULL;
  }
  memset(kern_task->context, 0, sizeof(struct hard_context));
  kern_task->context->eip = entrypoint;
  kern_task->context->esp = get_proc_stackmem(pg_dir);
  kern_task->context->cr3 = (u_int32_t)pg_dir - __PAGE_OFFSET;
  kern_task->context->cs = __USER_CS;
  kern_task->context->ds = __USER_DS;
  kern_task->context->eflags = DEFAULT_EFLAGS;
  argc = get_argc(argv);
  argv_copy = get_argv(argv);
  if (argc > 0 && argv_copy == NULL) {
    _kprintf("%s: argv copy failed\n", __func__);
    cleanup_exec_task(kern_task);
    return NULL;
  }
  kern_task->context->eax = argc;
  kern_task->context->ebx = (u_int32_t)argv_copy;
  kern_task->is_usermode = OUTER_PRIVILEGE;
  kern_task->state = TASK_RUNNING;

  set_default_sigaction(kern_task);

  kern_task->esp0_raw = kalloc(BLOCK_SIZE * (PROC_KERNEL_STACK_PAGES + 1));
  if (kern_task->esp0_raw == NULL) {
    _kprintf("%s: kern_task->esp0 kalloc error\n", __func__);
    cleanup_exec_task(kern_task);
    return NULL;
  }
  // 割り込みと exec の深い call stack に備えて、kernel stack は複数 page 分を確保する。
  kern_task->esp0 =
      ((u_int32_t)kern_task->esp0_raw & ~(BLOCK_SIZE - 1)) +
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
  int i;
  if (argv == NULL)
    return 0;

  for (i = 0; i < ARGV_MAX_NUMS - 1; i++) {
    if (argv[i] == NULL)
      return i;
  }
  return ARGV_MAX_NUMS - 1;
}

PRIVATE char** get_argv(char *const argv[])
{
  int i;

  if (argv == NULL)
    return NULL;
  char **ret_argv = kalloc(ARGV_MAX_NUMS * sizeof(char *));
  if (ret_argv == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(ret_argv, 0, ARGV_MAX_NUMS * sizeof(char *));

  for (i = 0; i < ARGV_MAX_NUMS - 1; i++) {
    int arg_len;

    if (argv[i] == NULL)
      break;
    ret_argv[i] = kalloc(ARGV_MAX_LEN);
    if (ret_argv[i] == NULL) {
      _kprintf("%s argv[%d] kalloc error\n", __func__, i);
      free_argv_copy(ret_argv);
      return NULL;
    }
    memset(ret_argv[i], 0, ARGV_MAX_LEN);
    arg_len = strlen(argv[i]);
    if (arg_len >= ARGV_MAX_LEN)
      arg_len = ARGV_MAX_LEN - 1;
    memcpy(ret_argv[i], argv[i], arg_len);
  }
  ret_argv[i] = NULL;
  /*
  for (i=0;;i++) {
    if (ret_argv[i] == NULL)
      break;
    _kprintf("ret_argv[%x] %x %s\n", i, ret_argv[i], ret_argv[i]);
  }
  */
  return ret_argv;
}

PRIVATE void free_argv_copy(char **argv)
{
  int i;

  if (argv == NULL)
    return;
  for (i = 0; i < ARGV_MAX_NUMS; i++) {
    if (argv[i] == NULL)
      break;
    kfree(argv[i]);
  }
  kfree(argv);
}

PRIVATE void cleanup_exec_task(struct task_struct *task)
{
  if (task == NULL)
    return;

  if (task->files != NULL) {
    files_close_all(task->files);
    kfree(task->files);
    task->files = NULL;
  }
  if (task->context != NULL) {
    free_argv_copy((char **)task->context->ebx);
    kfree(task->context);
    task->context = NULL;
  }
  if (task->esp0_raw != NULL) {
    kfree(task->esp0_raw);
    task->esp0_raw = NULL;
  }
  if (task->pg_dir != NULL)
    free_process_pages(task->pg_dir);
  if (task->pg_dir_raw != NULL) {
    kfree(task->pg_dir_raw);
    task->pg_dir_raw = NULL;
  }
  kfree(task);
}
