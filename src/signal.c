#include <signal.h>
#include <process.h>

PUBLIC sighandler_t sys_signal(int signum, sighandler_t sighandler)
{
  if (signum < 1 || signum > MAX_SIGNALS || signum == SIGKILL 
      || signum == SIGSTOP)
    return -1;

  struct sigaction* action = kalloc(sizeof(struct sigaction));
  memset(action, 0, sizeof(struct sigaction));
  action->sa_handler = sighandler;
  action->sa_mask = 0;
  current->sigactions[signum-1] = action;
  return sighandler;
}

PUBLIC int sys_kill(pid_t pid, int signum)
{
  if (pid <= 0)
    return -1;

  struct dlist_set* plist = &(current->run_list);
  struct dlist_set* pos;
  struct task_struct* proc = NULL;
  int exist_flag = FALSE;
  dlist_for_each (pos, plist) {
    proc = dlist_entry(pos, struct task_struct, run_list);
    if (proc->pid == pid) {
      exist_flag = TRUE;
      break;
    }
  }
  if (exist_flag == FALSE)
    return -1;

  proc->signal |= (1<<(signum-1));
  return 0;
}

PUBLIC void *signal_dummy(int signum)
{
  // dummy
  _kprintf("signal dummy\n");
}

PUBLIC void *core_dump(int signum)
{
  // still don't implement
  _kprintf("core dump\n");
}
