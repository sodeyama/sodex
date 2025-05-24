#include <stdlib.h>
#include <sodex/list.h>
#include <stdio.h>

static void ps_walk(struct task_struct* proc);

int main(int argc, char** argv)
{
  struct task_struct* proc = (struct task_struct*)getpstat();
  ps_walk(proc);
  exit(1);
  return 0;
}

static void ps_walk(struct task_struct* proc)
{
  printf("PID  PPID   CMD\n");
  struct dlist_set* plist = &(proc->run_list);
  while (TRUE) {
    struct task_struct* p = dlist_entry(plist, struct task_struct, run_list);
    printf("%x   %x     %s\n", p->pid, p->parent->pid, p->filename);
    plist = plist->next;
    if (plist == &(proc->run_list))
      break;
  }
}
