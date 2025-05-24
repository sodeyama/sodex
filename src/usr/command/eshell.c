#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <eshell.h>

static void set_prompt(char* prompt);
static char* get_path_recursively(ext3_dentry* dentry);

char g_pathname[PATH_MAX];

int main(int argc, char** argv)
{
  char arg_tmpbuf[ARGV_MAX_NUMS][ARGV_MAX_LEN];
  char *arg_buf[ARGV_MAX_NUMS];
  memset(arg_tmpbuf, 0, ARGV_MAX_LEN*ARGV_MAX_NUMS);
  memset(arg_buf, 0, 4*ARGV_MAX_NUMS);
  char buf[BUF_SIZE];
  char prompt[PROMPT_BUF];
  memset(prompt, 0, PROMPT_BUF);
  set_prompt(prompt);

  int read_len;
  while (TRUE) {
    write(1, prompt, strlen(prompt));
    read_len = read(0, buf, BUF_SIZE);
    if (read_len == 1)
      continue;

    char *p = buf;
    char *prev_p = buf;
    int i=0;
    while (TRUE) {
      p = strchr(prev_p, ' ');
      if (p == NULL) {
        memcpy(arg_tmpbuf[i], prev_p, buf+read_len-prev_p);
        arg_tmpbuf[i][buf+read_len-prev_p] = 0;
        arg_buf[i] = arg_tmpbuf[i];
        break;
      }
      memcpy(arg_tmpbuf[i], prev_p, p-prev_p);
      arg_tmpbuf[i][p-prev_p] = 0;
      arg_buf[i] = arg_tmpbuf[i];

      if (p >= buf + read_len) {
        printf("p is over\n");
        break;
      }
      i++;
      prev_p = p + 1;
    }
    arg_buf[i+1] = NULL;
    
    if (redirect_check(arg_buf)) {
      printf("redirect\n");
    }
    
    pid_t cid = execve(arg_buf[0], arg_buf, 0);
    if (strcmp(arg_buf[0], "test") != 0) {
      waitpid(cid, NULL, NULL);
    }

    if (strcmp(arg_buf[0], "cd") == 0) { 
      set_prompt(prompt);
    }

    memset(buf, 0, BUF_SIZE);
    memset(arg_tmpbuf, 0, ARGV_MAX_LEN*ARGV_MAX_NUMS);
    memset(arg_buf, 0, 4*ARGV_MAX_NUMS);
  }

  exit(1);
  return 0;
}

static void set_prompt(char* prompt)
{
  const int PREFIX = 6;
  ext3_dentry* dentry = (ext3_dentry*)getdentry();
  memcpy(prompt, "sodex ", PREFIX);
  char* pathname = get_path_recursively(dentry);
  int pathname_len = strlen(pathname);
  memcpy(prompt+PREFIX, pathname, pathname_len);
  memcpy(prompt+PREFIX+pathname_len, "> ", 3);
}

static char* get_path_recursively(ext3_dentry* dentry)
{
  struct list* head = malloc(sizeof(struct list));
  head->next = head;
  head->prev = head;
  memcpy(head->name, dentry->d_name, dentry->d_namelen);

  ext3_dentry* pdentry = dentry->d_parent;
  while (pdentry != NULL) {
    struct list* new = malloc(sizeof(struct list));
    memcpy(new->name, pdentry->d_name, pdentry->d_namelen);
    LIST_INSERT_BEFORE(new, head);
    pdentry = pdentry->d_parent;
  }

  char* p = g_pathname;
  memset(p, 0, 32);
  struct list* plist = head->prev;
  do {
    if (strcmp(plist->name, "/") == 0) {
      memcpy(p, plist->name, strlen(plist->name));
      p += strlen(plist->name);
    } else {
      if (p[-1] != '/') {
        memcpy(p, "/", 1);
        p += 1;
      }
      int len = strlen(plist->name);
      memcpy(p, plist->name, len);
      p += len;
    }
    plist = plist->prev;
  } while (plist != head->prev);

  plist = head->next;
  struct list* next_list;
  do {
    next_list = plist->next;
    free(plist);
    plist = next_list;
  } while (plist != head);
  
  return g_pathname;
}

int redirect_check(const char** arg_buf)
{
  if (arg_buf[1] != NULL && arg_buf[2] != NULL &&
      strcmp(arg_buf[2], ">") == 0)
    return TRUE;
  else
    return FALSE;
}

int pipe_check(const char** arg_buf)
{
  if (arg_buf[1] != NULL && arg_buf[2] != NULL &&
      strcmp(arg_buf[2], "|") == 0)
    return TRUE;
  else
    return FALSE;
}


