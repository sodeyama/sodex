#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>
#include <eshell.h>
#include <winsize.h>

static void set_prompt(char* prompt);
static char* get_path_recursively(ext3_dentry* dentry);
static int shell_buf_size(void);
static int refresh_shell_buffer(char **buf, int *buf_size);
static int clamp_copy_len(int len, int max_len);
static int find_token(char **argv, const char *token);
static int wait_command(pid_t pid, char *const argv[]);
static int swap_fd(int target_fd, int next_fd);
static void restore_fd(int target_fd, int saved_fd);
static int run_with_redirection(char **argv, char *input_path,
                                char *output_path);
static int run_pipeline(char **left_argv, char **right_argv);

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
    
    {
      int pipe_pos = find_token(arg_buf, "|");
      int input_pos = find_token(arg_buf, "<");
      int output_pos = find_token(arg_buf, ">");

      if (pipe_pos > 0 && arg_buf[pipe_pos + 1] != NULL) {
        char **right_argv = &(arg_buf[pipe_pos + 1]);
        arg_buf[pipe_pos] = NULL;
        run_pipeline(arg_buf, right_argv);
      } else {
        char *input_path = NULL;
        char *output_path = NULL;

        if (input_pos > 0 && arg_buf[input_pos + 1] != NULL) {
          input_path = arg_buf[input_pos + 1];
          arg_buf[input_pos] = NULL;
        }
        if (output_pos > 0 && arg_buf[output_pos + 1] != NULL) {
          output_path = arg_buf[output_pos + 1];
          arg_buf[output_pos] = NULL;
        }
        run_with_redirection(arg_buf, input_path, output_path);
      }
    }

    if (arg_buf[0] != NULL && strcmp(arg_buf[0], "cd") == 0) { 
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

static int find_token(char **argv, const char *token)
{
  int i;

  if (argv == NULL || token == NULL)
    return -1;
  for (i = 0; argv[i] != NULL; i++) {
    if (strcmp(argv[i], token) == 0)
      return i;
  }
  return -1;
}

static int wait_command(pid_t pid, char *const argv[])
{
  if (pid < 0)
    return -1;
  if (argv != NULL && argv[0] != NULL && strcmp(argv[0], "test") != 0)
    waitpid(pid, NULL, NULL);
  return 0;
}

static int swap_fd(int target_fd, int next_fd)
{
  int saved_fd;
  int new_fd;

  saved_fd = dup(target_fd);
  if (saved_fd < 0)
    return -1;
  close(target_fd);
  new_fd = dup(next_fd);
  if (new_fd != target_fd)
    return -1;
  return saved_fd;
}

static void restore_fd(int target_fd, int saved_fd)
{
  if (saved_fd < 0)
    return;
  close(target_fd);
  dup(saved_fd);
  close(saved_fd);
}

static int run_with_redirection(char **argv, char *input_path,
                                char *output_path)
{
  int input_fd = -1;
  int output_fd = -1;
  int saved_stdin = -1;
  int saved_stdout = -1;
  pid_t pid;

  if (argv == NULL || argv[0] == NULL)
    return -1;

  if (input_path != NULL) {
    input_fd = open(input_path, O_RDONLY, 0);
    if (input_fd < 0)
      return -1;
    saved_stdin = swap_fd(STDIN_FILENO, input_fd);
    close(input_fd);
    if (saved_stdin < 0)
      return -1;
  }

  if (output_path != NULL) {
    output_fd = open(output_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (output_fd < 0) {
      restore_fd(STDIN_FILENO, saved_stdin);
      return -1;
    }
    saved_stdout = swap_fd(STDOUT_FILENO, output_fd);
    close(output_fd);
    if (saved_stdout < 0) {
      restore_fd(STDIN_FILENO, saved_stdin);
      return -1;
    }
  }

  pid = execve(argv[0], argv, 0);
  restore_fd(STDIN_FILENO, saved_stdin);
  restore_fd(STDOUT_FILENO, saved_stdout);
  return wait_command(pid, argv);
}

static int run_pipeline(char **left_argv, char **right_argv)
{
  int pipefd[2];
  int saved_stdout;
  int saved_stdin;
  pid_t left_pid;
  pid_t right_pid;

  if (left_argv == NULL || left_argv[0] == NULL ||
      right_argv == NULL || right_argv[0] == NULL)
    return -1;
  if (pipe(pipefd) < 0)
    return -1;

  saved_stdout = swap_fd(STDOUT_FILENO, pipefd[1]);
  close(pipefd[1]);
  if (saved_stdout < 0) {
    close(pipefd[0]);
    return -1;
  }
  left_pid = execve(left_argv[0], left_argv, 0);
  restore_fd(STDOUT_FILENO, saved_stdout);
  wait_command(left_pid, left_argv);

  saved_stdin = swap_fd(STDIN_FILENO, pipefd[0]);
  close(pipefd[0]);
  if (saved_stdin < 0)
    return -1;
  right_pid = execve(right_argv[0], right_argv, 0);
  restore_fd(STDIN_FILENO, saved_stdin);
  return wait_command(right_pid, right_argv);
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
  int len;
  if (head == NULL)
    return "/";
  memset(head, 0, sizeof(struct list));
  head->next = head;
  head->prev = head;
  len = clamp_copy_len(dentry->d_namelen, NAME_MAX);
  memcpy(head->name, dentry->d_name, len);
  head->name[len] = '\0';

  ext3_dentry* pdentry = dentry->d_parent;
  while (pdentry != NULL) {
    struct list* new = malloc(sizeof(struct list));
    if (new == NULL)
      break;
    memset(new, 0, sizeof(struct list));
    len = clamp_copy_len(pdentry->d_namelen, NAME_MAX);
    memcpy(new->name, pdentry->d_name, len);
    new->name[len] = '\0';
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
