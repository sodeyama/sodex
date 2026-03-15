#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <eshell_parser.h>
#include <fs.h>
#include <eshell.h>
#include <winsize.h>

static void set_prompt(char* prompt);
static char* get_path_recursively(ext3_dentry* dentry);
static int shell_buf_size(void);
static int refresh_shell_buffer(char **buf, int *buf_size);
static int clamp_copy_len(int len, int max_len);
static void print_command_not_found(char *const argv[]);
static int wait_command(pid_t pid, char *const argv[]);
static int swap_fd(int target_fd, int next_fd);
static void restore_fd(int target_fd, int saved_fd);
static int run_single_command(const struct eshell_command *command);
static int run_pipeline(const struct eshell_pipeline *pipeline);

char g_pathname[PATH_MAX];

int main(int argc, char** argv)
{
  struct eshell_pipeline pipeline;
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

    if (eshell_parse_line(buf, input_len, &pipeline) < 0) {
      printf("eshell: parse error\n");
      memset(buf, 0, input_buf_size);
      continue;
    }
    if (pipeline.command_count <= 0) {
      memset(buf, 0, input_buf_size);
      continue;
    }

    if (pipeline.command_count == 1)
      run_single_command(&pipeline.commands[0]);
    else
      run_pipeline(&pipeline);

    if (pipeline.command_count == 1 &&
        pipeline.commands[0].argv[0] != NULL &&
        strcmp(pipeline.commands[0].argv[0], "cd") == 0) {
      set_prompt(prompt);
    }

    memset(buf, 0, input_buf_size);
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

static int wait_command(pid_t pid, char *const argv[])
{
  if (pid < 0) {
    print_command_not_found(argv);
    return -1;
  }
  if (argv != NULL && argv[0] != NULL && strcmp(argv[0], "test") != 0)
    waitpid(pid, NULL, NULL);
  return 0;
}

static void print_command_not_found(char *const argv[])
{
  if (argv == NULL || argv[0] == NULL)
    return;
  printf("eshell: command not found: %s\n", argv[0]);
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

static int run_single_command(const struct eshell_command *command)
{
  int input_fd = -1;
  int output_fd = -1;
  int saved_stdin = -1;
  int saved_stdout = -1;
  pid_t pid;
  int flags;

  if (command == NULL || command->argv[0] == NULL)
    return -1;

  if (command->input_path != NULL) {
    input_fd = open(command->input_path, O_RDONLY, 0);
    if (input_fd < 0)
      return -1;
    saved_stdin = swap_fd(STDIN_FILENO, input_fd);
    close(input_fd);
    if (saved_stdin < 0)
      return -1;
  }

  if (command->output_path != NULL) {
    flags = O_CREAT | O_WRONLY;
    if (command->append_output != 0)
      flags |= O_APPEND;
    else
      flags |= O_TRUNC;
    output_fd = open(command->output_path, flags, 0644);
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

  pid = execve(command->argv[0], command->argv, 0);
  restore_fd(STDIN_FILENO, saved_stdin);
  restore_fd(STDOUT_FILENO, saved_stdout);
  return wait_command(pid, command->argv);
}

static int run_pipeline(const struct eshell_pipeline *pipeline)
{
  int input_fd = -1;
  int i;

  if (pipeline == NULL || pipeline->command_count <= 0)
    return -1;

  for (i = 0; i < pipeline->command_count; i++) {
    const struct eshell_command *command = &pipeline->commands[i];
    int pipefd[2] = {-1, -1};
    int saved_stdin = -1;
    int saved_stdout = -1;
    int output_fd = -1;
    int next_input_fd = -1;
    int flags;
    pid_t pid;

    if (command->argv[0] == NULL)
      return -1;

    if (input_fd >= 0) {
      saved_stdin = swap_fd(STDIN_FILENO, input_fd);
      close(input_fd);
      input_fd = -1;
      if (saved_stdin < 0)
        return -1;
    } else if (command->input_path != NULL) {
      input_fd = open(command->input_path, O_RDONLY, 0);
      if (input_fd < 0)
        return -1;
      saved_stdin = swap_fd(STDIN_FILENO, input_fd);
      close(input_fd);
      input_fd = -1;
      if (saved_stdin < 0)
        return -1;
    }

    if (i + 1 < pipeline->command_count) {
      if (pipe(pipefd) < 0) {
        restore_fd(STDIN_FILENO, saved_stdin);
        return -1;
      }
      saved_stdout = swap_fd(STDOUT_FILENO, pipefd[1]);
      close(pipefd[1]);
      if (saved_stdout < 0) {
        close(pipefd[0]);
        restore_fd(STDIN_FILENO, saved_stdin);
        return -1;
      }
      next_input_fd = pipefd[0];
    } else if (command->output_path != NULL) {
      flags = O_CREAT | O_WRONLY;
      if (command->append_output != 0)
        flags |= O_APPEND;
      else
        flags |= O_TRUNC;
      output_fd = open(command->output_path, flags, 0644);
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

    pid = execve(command->argv[0], command->argv, 0);
    restore_fd(STDIN_FILENO, saved_stdin);
    restore_fd(STDOUT_FILENO, saved_stdout);
    if (wait_command(pid, command->argv) < 0) {
      if (next_input_fd >= 0)
        close(next_input_fd);
      return -1;
    }
    input_fd = next_input_fd;
  }

  if (input_fd >= 0)
    close(input_fd);
  return 0;
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
