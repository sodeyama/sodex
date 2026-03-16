#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>
#include <eshell.h>
#include <debug.h>
#include <winsize.h>
#include <shell.h>

static void set_prompt(char *prompt);
static char *get_path_recursively(ext3_dentry *dentry);
static int shell_buf_size(void);
static int refresh_shell_buffer(char **buf, int *buf_size);
static int clamp_copy_len(int len, int max_len);

char g_pathname[PATH_MAX];
static struct shell_state g_shell_state;

int main(int argc, char **argv)
{
  struct shell_state *state = &g_shell_state;
  char *buf;
  char prompt[PROMPT_BUF];
  int input_buf_size;

  (void)argc;
  (void)argv;

  shell_state_init(state, 1);
  memset(prompt, 0, sizeof(prompt));
  set_prompt(prompt);
  input_buf_size = shell_buf_size();
  buf = (char *)malloc((size_t)input_buf_size);
  if (buf == NULL)
    return 1;
  memset(buf, 0, (size_t)input_buf_size);
  debug_write("AUDIT eshell_ready\n", 19);

  while (1) {
    int read_len;
    int input_len;
    int status;

    shell_reap_background(state);
    if (refresh_shell_buffer(&buf, &input_buf_size) < 0)
      break;
    write(STDOUT_FILENO, prompt, strlen(prompt));
    read_len = (int)read(STDIN_FILENO, buf, (size_t)input_buf_size);
    if (read_len <= 1)
      continue;
    input_len = read_len;
    if (input_len > 0 && buf[input_len - 1] == '\0')
      input_len--;
    if (input_len <= 0)
      continue;
    buf[input_len] = '\0';

    status = shell_execute_string(state, buf);
    if (status == 2)
      printf("eshell: parse error\n");
    if (state->exit_requested != 0)
      exit(state->exit_status);
    set_prompt(prompt);
    memset(buf, 0, (size_t)input_buf_size);
  }

  free(buf);
  exit(1);
  return 0;
}

static int shell_buf_size(void)
{
  struct winsize winsize;
  int size = BUF_SIZE;

  if (get_winsize(0, &winsize) == 0 && winsize.cols > 0)
    size = winsize.cols + 1;
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

  next_buf = (char *)malloc((size_t)next_size);
  if (next_buf == NULL)
    return -1;

  memset(next_buf, 0, (size_t)next_size);
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

static void set_prompt(char *prompt)
{
  const int prefix = 6;
  ext3_dentry *dentry = (ext3_dentry *)getdentry();
  char *pathname;
  int pathname_len;
  int copy_len;

  memset(prompt, 0, PROMPT_BUF);
  memcpy(prompt, "sodex ", (size_t)prefix);
  pathname = get_path_recursively(dentry);
  pathname_len = (int)strlen(pathname);
  copy_len = clamp_copy_len(pathname_len, PROMPT_BUF - prefix - 2);
  memcpy(prompt + prefix, pathname, (size_t)copy_len);
  memcpy(prompt + prefix + copy_len, "> ", 3);
}

static char *get_path_recursively(ext3_dentry *dentry)
{
  struct list *head = (struct list *)malloc(sizeof(struct list));
  ext3_dentry *pdentry;
  struct list *plist;
  struct list *next_list;
  char *p = g_pathname;
  int len;

  if (head == NULL)
    return "/";

  memset(head, 0, sizeof(struct list));
  head->next = head;
  head->prev = head;
  len = clamp_copy_len(dentry->d_namelen, NAME_MAX);
  memcpy(head->name, dentry->d_name, (size_t)len);
  head->name[len] = '\0';

  pdentry = dentry->d_parent;
  while (pdentry != NULL) {
    struct list *node = (struct list *)malloc(sizeof(struct list));

    if (node == NULL)
      break;
    memset(node, 0, sizeof(struct list));
    len = clamp_copy_len(pdentry->d_namelen, NAME_MAX);
    memcpy(node->name, pdentry->d_name, (size_t)len);
    node->name[len] = '\0';
    LIST_INSERT_BEFORE(node, head);
    pdentry = pdentry->d_parent;
  }

  memset(p, 0, PATH_MAX);
  plist = head->prev;
  do {
    if (strcmp(plist->name, "/") == 0) {
      memcpy(p, plist->name, strlen(plist->name));
      p += strlen(plist->name);
    } else {
      if (p[-1] != '/') {
        memcpy(p, "/", 1);
        p += 1;
      }
      len = (int)strlen(plist->name);
      memcpy(p, plist->name, (size_t)len);
      p += len;
    }
    plist = plist->prev;
  } while (plist != head->prev);

  plist = head->next;
  do {
    next_list = plist->next;
    free(plist);
    plist = next_list;
  } while (plist != head);

  return g_pathname;
}

int redirect_check(const char **arg_buf)
{
  (void)arg_buf;
  return FALSE;
}

int pipe_check(const char **arg_buf)
{
  (void)arg_buf;
  return FALSE;
}
