#include <signal.h>
#include <process.h>

PUBLIC sighandler_t sys_signal(int signum, sighandler_t sighandler)
{
  sighandler_t old;

  if (signum < 1 || signum > MAX_SIGNALS || signum == SIGKILL 
      || signum == SIGSTOP)
    return -1;

  old = current->sigactions[signum-1];
  current->sigactions[signum-1] = sighandler;
  return old;
}

PUBLIC int sys_kill(pid_t pid, int signum)
{
  struct task_struct* proc;

  if (pid < 0)
    return -1;

  proc = process_find_pid(pid);
  if (proc == NULL)
    return -1;

  if (signum == 0)
    return process_has_pid(pid) ? 0 : -1;

  proc->signal |= (1<<(signum-1));
  proc->state = TASK_RUNNING;
  return 0;
}

PUBLIC void signal_dummy(int signum)
{
  (void)signum;
  // dummy
  _kprintf("signal dummy\n");
}

PUBLIC void core_dump(int signum)
{
  (void)signum;
  // still don't implement
  _kprintf("core dump\n");
}

PUBLIC void task_exit(int signum)
{
  current->exit_status = 128 + signum;
  current->state = TASK_ZOMBIE;
}
