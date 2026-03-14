#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <eshell.h>
#include <winsize.h>

static void set_prompt(char* prompt);
static char* get_path_recursively(ext3_dentry* dentry);
static int shell_buf_size(void);
static int refresh_shell_buffer(char **buf, int *buf_size);
static int clamp_copy_len(int len, int max_len);

char g_pathname[PATH_MAX];

int main(int argc, char** argv)
{
  char arg_tmpbuf[ARGV_MAX_NUMS][ARGV_MAX_LEN];
  char *arg_buf[ARGV_MAX_NUMS];
  memset(arg_tmpbuf, 0, ARGV_MAX_LEN*ARGV_MAX_NUMS);
  memset(arg_buf, 0, 4*ARGV_MAX_NUMS);
  char *buf;
  char prompt[PROMPT_BUF];
  int input_buf_size;
  int input_len;
  memset(prompt, 0, PROMPT_BUF);
  set_prompt(prompt);
  input_buf_size = shell_buf_size();
  buf = malloc(input_buf_size);
  if (buf == NULL)
    return 1;
  memset(buf, 0, input_buf_size);

  int read_len;
  while (TRUE) {
    if (refresh_shell_buffer(&buf, &input_buf_size) < 0)
      break;
    write(1, prompt, strlen(prompt));
    read_len = read(0, buf, input_buf_size);
    if (read_len == 1)
      continue;
    input_len = read_len;
    if (input_len > 0 && buf[input_len - 1] == '\0')
      input_len--;
    if (input_len <= 0)
      continue;

    char *p = buf;
    char *prev_p = buf;
    int i=0;
    while (TRUE) {
      p = strchr(prev_p, ' ');
      if (p == NULL || p >= buf + input_len || i >= ARGV_MAX_NUMS - 1) {
        int len = clamp_copy_len((buf + input_len) - prev_p, ARGV_MAX_LEN);
        memcpy(arg_tmpbuf[i], prev_p, len);
        arg_tmpbuf[i][len] = 0;
        arg_buf[i] = arg_tmpbuf[i];
        break;
      }
      {
        int len = clamp_copy_len(p - prev_p, ARGV_MAX_LEN);
        memcpy(arg_tmpbuf[i], prev_p, len);
        arg_tmpbuf[i][len] = 0;
      }
      arg_buf[i] = arg_tmpbuf[i];

      if (p >= buf + input_len) {
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

    memset(buf, 0, input_buf_size);
    memset(arg_tmpbuf, 0, ARGV_MAX_LEN*ARGV_MAX_NUMS);
    memset(arg_buf, 0, 4*ARGV_MAX_NUMS);
  }

  exit(1);
  return 0;
}

static int shell_buf_size(void)
{
  struct winsize winsize;
  int size = BUF_SIZE;

  if (get_winsize(0, &winsize) == 0 && winsize.cols > 0) {
    size = winsize.cols + 1;
  }
  if (size < BUF_SIZE)
    size = BUF_SIZE;
  if (size > 255)
    size = 255;
  return size;
}

static int refresh_shell_buffer(char **buf, int *buf_size)
{
  char *next_buf;
  int next_size = shell_buf_size();

  if (next_size == *buf_size)
    return 0;

  next_buf = malloc(next_size);
  if (next_buf == NULL)
    return -1;

  memset(next_buf, 0, next_size);
  free(*buf);
  *buf = next_buf;
  *buf_size = next_size;
  return 1;
}

static int clamp_copy_len(int len, int max_len)
{
  if (len < 0)
    return 0;
  if (len >= max_len)
    return max_len - 1;
  return len;
}

static void set_prompt(char* prompt)
{
  const int PREFIX = 6;
  ext3_dentry* dentry = (ext3_dentry*)getdentry();
  int pathname_len;
  int copy_len;

  memset(prompt, 0, PROMPT_BUF);
  memcpy(prompt, "sodex ", PREFIX);
  char* pathname = get_path_recursively(dentry);
  pathname_len = strlen(pathname);
  copy_len = clamp_copy_len(pathname_len, PROMPT_BUF - PREFIX - 2);
  memcpy(prompt+PREFIX, pathname, copy_len);
  memcpy(prompt+PREFIX+copy_len, "> ", 3);
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
  memset(p, 0, PATH_MAX);
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
