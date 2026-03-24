#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>
#include <eshell.h>
#include <debug.h>
#include <termios.h>
#include <winsize.h>
#include <shell.h>
#include <agent_fusion.h>

static void set_prompt(char *prompt);
static char *get_path_recursively(ext3_dentry *dentry);
static int shell_buf_size(void);
static int refresh_shell_buffer(char **buf, int *buf_size);
static int clamp_copy_len(int len, int max_len);
static int shell_tty_echoes_newline(void);
static int shell_ensure_buffer(char **buf, int *buf_size, int need);
static int shell_append_input(char **buf, int *buf_size, int *len,
                              const char *line, int line_len, int add_newline);
static int shell_replace_input(char **buf, int *buf_size, int *len,
                               const char *text);
static int shell_run_agent_fusion(const char *text, int force_agent);
static void shell_write_fusion_status(int fusion_mode, int route_reason);
static int shell_handle_fusion_mode_command(const char *text, int *fusion_mode);
static int shell_route_reason_from_probe(const struct shell_state *state,
                                         const char *text);
static const char *shell_route_reason_text(int route_reason);
static int shell_read_input_line(char **buf, int *buf_size,
                                 int *data_pos, int *data_len,
                                 int tty_echoes_newline,
                                 char **line, int *line_len,
                                 int *add_newline);

enum shell_fusion_route_reason {
  SHELL_FUSION_ROUTE_NONE = 0,
  SHELL_FUSION_ROUTE_AGENT_FORCED = 1,
  SHELL_FUSION_ROUTE_AGENT_MODE = 2,
  SHELL_FUSION_ROUTE_SHELL_MODE = 3,
  SHELL_FUSION_ROUTE_SHELL_ALIAS = 4,
  SHELL_FUSION_ROUTE_SHELL_BUILTIN = 5,
  SHELL_FUSION_ROUTE_SHELL_EXTERNAL = 6,
  SHELL_FUSION_ROUTE_SHELL_PATH = 7,
  SHELL_FUSION_ROUTE_SHELL_ASSIGNMENTS = 8,
  SHELL_FUSION_ROUTE_SHELL_COMPOUND = 9,
  SHELL_FUSION_ROUTE_UNKNOWN_COMMAND = 10
};

char g_pathname[PATH_MAX];
static struct shell_state g_shell_state;

int main(int argc, char **argv)
{
  struct shell_state *state = &g_shell_state;
  char *buf;
  char *command_buf;
  char prompt[PROMPT_BUF];
  struct shell_program *probe_program;
  int command_buf_size;
  int command_len;
  int input_buf_size;
  int input_data_len;
  int input_pos;
  int last_status = 0;
  int fusion_mode;
  int route_reason = SHELL_FUSION_ROUTE_NONE;
  int tty_echoes_newline;
  int fusion_enabled;

  shell_state_init(state, 1);
  fusion_enabled = agent_fusion_enabled(argc, argv);
  fusion_mode = agent_fusion_mode_from_argv(argc, argv, AGENT_FUSION_MODE_AUTO);
  memset(prompt, 0, sizeof(prompt));
  set_prompt(prompt);
  input_buf_size = shell_buf_size();
  tty_echoes_newline = shell_tty_echoes_newline();
  buf = (char *)malloc((size_t)input_buf_size);
  if (buf == NULL)
    return 1;
  command_buf_size = 512;
  command_len = 0;
  command_buf = (char *)malloc((size_t)command_buf_size);
  probe_program = (struct shell_program *)malloc(sizeof(*probe_program));
  if (command_buf == NULL || probe_program == NULL) {
    free(buf);
    if (command_buf != NULL)
      free(command_buf);
    if (probe_program != NULL)
      free(probe_program);
    return 1;
  }
  memset(buf, 0, (size_t)input_buf_size);
  memset(command_buf, 0, (size_t)command_buf_size);
  input_data_len = 0;
  input_pos = 0;
  debug_write("AUDIT eshell_ready\n", 19);
  if (fusion_enabled != 0) {
    debug_write("AUDIT eshell_agent_fusion_enabled\n", 34);
    debug_write("AUDIT eshell_agent_mode=", 24);
    debug_write(agent_fusion_mode_name(fusion_mode),
                strlen(agent_fusion_mode_name(fusion_mode)));
    debug_write("\n", 1);
  }

  while (1) {
    char *line;
    int add_newline = 0;
    int line_len;
    int parse_status;
    int read_status;
    int status;

    shell_reap_background(state);
    if (input_data_len == 0 &&
        refresh_shell_buffer(&buf, &input_buf_size) < 0)
    {
      last_status = 1;
      break;
    }
    if (command_len > 0)
      write(STDOUT_FILENO, "...> ", 5);
    else {
      if (fusion_enabled != 0)
        shell_write_fusion_status(fusion_mode, route_reason);
      write(STDOUT_FILENO, prompt, strlen(prompt));
    }
    read_status = shell_read_input_line(&buf, &input_buf_size,
                                        &input_pos, &input_data_len,
                                        tty_echoes_newline,
                                        &line, &line_len, &add_newline);
    if (read_status < 0) {
      last_status = 1;
      break;
    }
    if (read_status == 0)
      break;
    if (line_len <= 0 && add_newline == 0)
      continue;

    if (shell_append_input(&command_buf, &command_buf_size, &command_len,
                           line, line_len, add_newline) < 0) {
      write(STDERR_FILENO, "eshell: out of memory\n", 22);
      last_status = 1;
      break;
    }

    parse_status = shell_parse_program(command_buf, command_len, probe_program);
    if (parse_status == SHELL_PARSE_INCOMPLETE) {
      continue;
    }
    if (parse_status < 0) {
      write(STDERR_FILENO, "eshell: parse error\n", 20);
      command_len = 0;
      command_buf[0] = '\0';
      set_prompt(prompt);
      continue;
    }
    if (state->interactive != 0) {
      char history_text[SHELL_HISTORY_TEXT_MAX];
      int history_status;

      history_status = shell_history_expand_line(state, command_buf,
                                                 history_text, sizeof(history_text));
      if (history_status < 0) {
        write(STDERR_FILENO, "eshell: history not found\n", 26);
        command_len = 0;
        command_buf[0] = '\0';
        set_prompt(prompt);
        continue;
      }
      if (history_status > 0) {
        write(STDOUT_FILENO, history_text, strlen(history_text));
        write(STDOUT_FILENO, "\n", 1);
        if (shell_replace_input(&command_buf, &command_buf_size, &command_len,
                                history_text) < 0) {
          write(STDERR_FILENO, "eshell: out of memory\n", 22);
          last_status = 1;
          break;
        }
        parse_status = shell_parse_program(command_buf, command_len, probe_program);
        if (parse_status == SHELL_PARSE_INCOMPLETE)
          continue;
        if (parse_status < 0) {
          write(STDERR_FILENO, "eshell: parse error\n", 20);
          command_len = 0;
          command_buf[0] = '\0';
          set_prompt(prompt);
          continue;
        }
      }
      shell_history_add(state, command_buf);
    }

    if (fusion_enabled != 0) {
      status = shell_handle_fusion_mode_command(command_buf, &fusion_mode);
      if (status != -2) {
        route_reason = SHELL_FUSION_ROUTE_NONE;
        last_status = status;
        set_prompt(prompt);
        command_len = 0;
        command_buf[0] = '\0';
        continue;
      }
      if (fusion_mode == AGENT_FUSION_MODE_AGENT) {
        route_reason = SHELL_FUSION_ROUTE_AGENT_MODE;
        status = shell_run_agent_fusion(command_buf, 1);
      } else {
        status = shell_run_agent_fusion(command_buf, 0);
        if (status != -2) {
          route_reason = SHELL_FUSION_ROUTE_AGENT_FORCED;
        } else {
          if (fusion_mode == AGENT_FUSION_MODE_SHELL)
            route_reason = SHELL_FUSION_ROUTE_SHELL_MODE;
          else
            route_reason = shell_route_reason_from_probe(state, command_buf);
          status = shell_execute_string(state, command_buf);
        }
      }
    } else {
      status = shell_execute_string(state, command_buf);
    }
    last_status = status;
    if (status == 2)
      write(STDERR_FILENO, "eshell: parse error\n", 20);
    if (state->exit_requested != 0)
      exit(state->exit_status);
    set_prompt(prompt);
    command_len = 0;
    command_buf[0] = '\0';
  }

  free(buf);
  free(command_buf);
  free(probe_program);
  exit(last_status);
  return last_status;
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

static int shell_tty_echoes_newline(void)
{
  struct termios termios;

  if (tcgetattr(STDIN_FILENO, &termios) < 0)
    return 1;
  return (termios.c_lflag & ECHONL) != 0;
}

static int shell_ensure_buffer(char **buf, int *buf_size, int need)
{
  char *next_buf;
  int next_size;

  if (buf == 0 || *buf == 0 || buf_size == 0)
    return -1;
  if (need <= *buf_size)
    return 0;

  next_size = *buf_size;
  while (next_size < need)
    next_size *= 2;
  next_buf = (char *)malloc((size_t)next_size);
  if (next_buf == NULL)
    return -1;
  memset(next_buf, 0, (size_t)next_size);
  memcpy(next_buf, *buf, (size_t)(*buf_size));
  free(*buf);
  *buf = next_buf;
  *buf_size = next_size;
  return 0;
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

static int shell_read_input_line(char **buf, int *buf_size,
                                 int *data_pos, int *data_len,
                                 int tty_echoes_newline,
                                 char **line, int *line_len,
                                 int *add_newline)
{
  if (buf == 0 || *buf == 0 || buf_size == 0 ||
      data_pos == 0 || data_len == 0 ||
      line == 0 || line_len == 0 || add_newline == 0)
    return -1;

  while (1) {
    int i;

    while (*data_pos < *data_len && (*buf)[*data_pos] == '\0')
      (*data_pos)++;
    if (*data_pos >= *data_len) {
      *data_pos = 0;
      *data_len = 0;
    }

    for (i = *data_pos; i < *data_len; i++) {
      if ((*buf)[i] == '\r' || (*buf)[i] == '\n') {
        int consume = 1;

        *line = *buf + *data_pos;
        *line_len = i - *data_pos;
        *add_newline = 1;
        if ((*buf)[i] == '\r' && i + 1 < *data_len && (*buf)[i + 1] == '\n')
          consume = 2;
        *data_pos = i + consume;
        if (*data_pos >= *data_len) {
          *data_pos = 0;
          *data_len = 0;
        }
        if (tty_echoes_newline == 0)
          write(STDOUT_FILENO, "\n", 1);
        return 1;
      }
    }

    if (*data_pos > 0) {
      memmove(*buf, *buf + *data_pos, (size_t)(*data_len - *data_pos));
      *data_len -= *data_pos;
      *data_pos = 0;
      (*buf)[*data_len] = '\0';
    }

    if (*data_len >= *buf_size - 1) {
      if (shell_ensure_buffer(buf, buf_size, *buf_size * 2) < 0)
        return -1;
    }

    i = (int)read(STDIN_FILENO, *buf + *data_len,
                  (size_t)(*buf_size - *data_len - 1));
    if (i < 0)
      return -1;
    if (i == 0) {
      if (*data_len <= 0)
        return 0;
      *line = *buf;
      *line_len = *data_len;
      *add_newline = 0;
      *data_pos = 0;
      *data_len = 0;
      (*buf)[*line_len] = '\0';
      return 1;
    }
    *data_len += i;
    (*buf)[*data_len] = '\0';
  }
}

static int shell_append_input(char **buf, int *buf_size, int *len,
                              const char *line, int line_len, int add_newline)
{
  int need;

  if (buf == 0 || *buf == 0 || buf_size == 0 || len == 0)
    return -1;

  need = *len + line_len + (add_newline != 0 ? 1 : 0) + 1;
  if (need > *buf_size) {
    int next_size = *buf_size;
    char *next_buf;

    while (next_size < need)
      next_size *= 2;
    next_buf = (char *)malloc((size_t)next_size);
    if (next_buf == NULL)
      return -1;
    memset(next_buf, 0, (size_t)next_size);
    memcpy(next_buf, *buf, (size_t)(*len));
    free(*buf);
    *buf = next_buf;
    *buf_size = next_size;
  }

  if (line_len > 0)
    memcpy(*buf + *len, line, (size_t)line_len);
  *len += line_len;
  if (add_newline != 0)
    (*buf)[(*len)++] = '\n';
  (*buf)[*len] = '\0';
  return 0;
}

static int shell_replace_input(char **buf, int *buf_size, int *len,
                               const char *text)
{
  int text_len;

  if (text == 0)
    text = "";
  *len = 0;
  if (*buf != 0)
    (*buf)[0] = '\0';
  text_len = (int)strlen(text);
  return shell_append_input(buf, buf_size, len, text, text_len, 0);
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

static void shell_write_fusion_status(int fusion_mode, int route_reason)
{
  const char *mode_text;
  const char *reason_text;

  mode_text = agent_fusion_mode_name(fusion_mode);
  write(STDOUT_FILENO, "[", 1);
  write(STDOUT_FILENO, mode_text, strlen(mode_text));
  if (fusion_mode == AGENT_FUSION_MODE_AUTO) {
    reason_text = shell_route_reason_text(route_reason);
    if (reason_text != 0) {
      write(STDOUT_FILENO, ":", 1);
      write(STDOUT_FILENO, reason_text, strlen(reason_text));
    }
  }
  write(STDOUT_FILENO, "]\n", 2);
}

static int shell_handle_fusion_mode_command(const char *text, int *fusion_mode)
{
  char token[AGENT_FUSION_TEXT_MAX];
  int start = 0;
  int len = 0;
  int mode;

  if (text == 0 || fusion_mode == 0)
    return -2;

  while (text[start] == ' ' || text[start] == '\t' ||
         text[start] == '\r' || text[start] == '\n') {
    start++;
  }
  while (text[start] != '\0' &&
         text[start] != ' ' && text[start] != '\t' &&
         text[start] != '\r' && text[start] != '\n' &&
         len < AGENT_FUSION_TEXT_MAX - 1) {
    token[len++] = text[start++];
  }
  token[len] = '\0';
  if (strcmp(token, "/mode") != 0)
    return -2;

  while (text[start] == ' ' || text[start] == '\t' ||
         text[start] == '\r' || text[start] == '\n') {
    start++;
  }
  if (text[start] == '\0') {
    write(STDOUT_FILENO, "mode: ", 6);
    write(STDOUT_FILENO, agent_fusion_mode_name(*fusion_mode),
          strlen(agent_fusion_mode_name(*fusion_mode)));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
  }

  len = 0;
  while (text[start] != '\0' &&
         text[start] != ' ' && text[start] != '\t' &&
         text[start] != '\r' && text[start] != '\n' &&
         len < AGENT_FUSION_TEXT_MAX - 1) {
    token[len++] = text[start++];
  }
  token[len] = '\0';
  if (agent_fusion_parse_mode_text(token, &mode) < 0) {
    write(STDERR_FILENO, "eshell: usage: /mode auto|shell|agent\n", 39);
    return 1;
  }

  *fusion_mode = mode;
  debug_write("AUDIT eshell_agent_mode=", 24);
  debug_write(agent_fusion_mode_name(mode),
              strlen(agent_fusion_mode_name(mode)));
  debug_write("\n", 1);
  write(STDOUT_FILENO, "mode: ", 6);
  write(STDOUT_FILENO, agent_fusion_mode_name(mode),
        strlen(agent_fusion_mode_name(mode)));
  write(STDOUT_FILENO, "\n", 1);
  return 0;
}

static int shell_route_reason_from_probe(const struct shell_state *state,
                                         const char *text)
{
  int route = shell_route_probe(state, text);

  if (route == SHELL_ROUTE_ASSIGNMENTS)
    return SHELL_FUSION_ROUTE_SHELL_ASSIGNMENTS;
  if (route == SHELL_ROUTE_ALIAS)
    return SHELL_FUSION_ROUTE_SHELL_ALIAS;
  if (route == SHELL_ROUTE_BUILTIN)
    return SHELL_FUSION_ROUTE_SHELL_BUILTIN;
  if (route == SHELL_ROUTE_EXTERNAL)
    return SHELL_FUSION_ROUTE_SHELL_EXTERNAL;
  if (route == SHELL_ROUTE_PATH)
    return SHELL_FUSION_ROUTE_SHELL_PATH;
  if (route == SHELL_ROUTE_COMPOUND)
    return SHELL_FUSION_ROUTE_SHELL_COMPOUND;
  if (route == SHELL_ROUTE_UNKNOWN)
    return SHELL_FUSION_ROUTE_UNKNOWN_COMMAND;
  return SHELL_FUSION_ROUTE_NONE;
}

static const char *shell_route_reason_text(int route_reason)
{
  if (route_reason == SHELL_FUSION_ROUTE_AGENT_FORCED)
    return "agent forced";
  if (route_reason == SHELL_FUSION_ROUTE_SHELL_ALIAS)
    return "shell alias";
  if (route_reason == SHELL_FUSION_ROUTE_SHELL_BUILTIN)
    return "shell builtin";
  if (route_reason == SHELL_FUSION_ROUTE_SHELL_EXTERNAL)
    return "shell external";
  if (route_reason == SHELL_FUSION_ROUTE_SHELL_PATH)
    return "shell path";
  if (route_reason == SHELL_FUSION_ROUTE_SHELL_ASSIGNMENTS)
    return "shell assignments";
  if (route_reason == SHELL_FUSION_ROUTE_SHELL_COMPOUND)
    return "shell compound";
  if (route_reason == SHELL_FUSION_ROUTE_UNKNOWN_COMMAND)
    return "unknown command";
  return 0;
}

static int shell_run_agent_fusion(const char *text, int force_agent)
{
  char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
  char *argv[AGENT_FUSION_MAX_ARGS + 1];
  int argc;
  int status = 0;
  pid_t pid;

  argc = agent_fusion_build_mode_argv(text, force_agent, storage, argv);
  if (argc == 0)
    return -2;
  if (argc < 0) {
    write(STDERR_FILENO, "eshell: invalid agent command\n", 30);
    return 1;
  }

  debug_write("AUDIT eshell_agent_route_begin\n", 31);
  pid = execve("/usr/bin/agent", argv, 0);
  if (pid < 0) {
    write(STDERR_FILENO, "eshell: failed to launch agent\n", 31);
    return 1;
  }
  set_foreground_pid(STDIN_FILENO, pid);
  if (waitpid(pid, &status, 0) < 0)
    status = 1;
  set_foreground_pid(STDIN_FILENO, 0);
  debug_write("AUDIT eshell_agent_route_done\n", 30);
  return status;
}
