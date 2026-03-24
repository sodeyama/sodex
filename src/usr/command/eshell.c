#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>
#include <eshell.h>
#include <debug.h>
#include <termios.h>
#include <winsize.h>
#include <poll.h>
#include <shell.h>
#include <agent/agent.h>
#include <agent/audit.h>
#include <agent/bounded_output.h>
#include <agent/conversation.h>
#include <agent/path_utils.h>
#include <agent/session.h>
#include <agent/tool_handlers.h>
#include <agent/term_session_surface.h>
#include <agent_fusion.h>
#include <json.h>
#include <term_command_recovery.h>

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
static int shell_run_agent_fusion(struct shell_state *state,
                                  const char *text, int force_agent);
static void shell_write_fusion_status(int fusion_mode, int route_reason);
static int shell_handle_fusion_command(struct shell_state *state,
                                       const char *text, int *fusion_mode);
static int shell_route_reason_from_probe(const struct shell_state *state,
                                         const char *text);
static const char *shell_route_reason_text(int route_reason);
static int shell_handle_command_recovery(struct shell_state *state,
                                         const char *command_buf,
                                         int *status);
static void shell_audit_recovery(const struct term_command_recovery_result *result,
                                 int auto_applied);
static int shell_read_input_line(char **buf, int *buf_size,
                                 int *data_pos, int *data_len,
                                 int tty_echoes_newline,
                                 char **line, int *line_len,
                                 int *add_newline);
static void shell_write_text(const char *text);
static void shell_fusion_state_init(void);
static int shell_fusion_ensure_session(void);
static int shell_fusion_resume_session(const char *session_id);
static void shell_fusion_update_surface(void);
static void shell_fusion_update_audit(void);
static void shell_fusion_write_drawer(void);
static void shell_fusion_record_recent_shell(const char *command,
                                             int status,
                                             const char *stderr_tail);
static void shell_fusion_set_route_text(const char *route_text);
static int shell_fusion_compact(const char *focus);
static void shell_fusion_list_sessions(void);
static int shell_fusion_execute_approved_command(struct shell_state *state,
                                                 const char *command);
static int shell_fusion_maybe_auto_execute_proposal(struct shell_state *state);
static int shell_fusion_capture_agent_output(char *const argv[],
                                             struct bounded_output *bounded,
                                             int *status_out);
static int shell_fusion_build_agent_argv(const char *text, int force_agent,
                                         char prompt_buf[TERM_SESSION_SURFACE_PROMPT_MAX],
                                         char storage[AGENT_FUSION_MAX_ARGS + 4][AGENT_FUSION_TEXT_MAX],
                                         char *argv[AGENT_FUSION_MAX_ARGS + 4]);

struct shell_agent_fusion_state {
  struct term_session_surface surface;
  struct session_meta session;
  int session_ready;
  int next_recent_timestamp;
  int session_allow_by_class[TERM_COMMAND_CLASS_COUNT];
};

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
static struct shell_agent_fusion_state g_fusion_state;

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
  if (fusion_enabled != 0)
    shell_fusion_state_init();
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
    int shell_executed = 0;
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
      if (fusion_enabled != 0) {
        shell_write_fusion_status(fusion_mode, route_reason);
        shell_fusion_write_drawer();
      }
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
      status = shell_handle_fusion_command(state, command_buf, &fusion_mode);
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
        status = shell_run_agent_fusion(state, command_buf, 1);
      } else {
        status = shell_run_agent_fusion(state, command_buf, 0);
        if (status != -2) {
          route_reason = SHELL_FUSION_ROUTE_AGENT_FORCED;
        } else {
          if (fusion_mode == AGENT_FUSION_MODE_SHELL)
            route_reason = SHELL_FUSION_ROUTE_SHELL_MODE;
          else
            route_reason = shell_route_reason_from_probe(state, command_buf);
          shell_executed = 1;
          status = shell_execute_string(state, command_buf);
        }
      }
    } else {
      shell_executed = 1;
      status = shell_execute_string(state, command_buf);
    }
    if (fusion_enabled != 0 && shell_executed != 0 && status != 0)
      shell_handle_command_recovery(state, command_buf, &status);
    if (fusion_enabled != 0 && shell_executed != 0) {
      shell_fusion_record_recent_shell(command_buf, status,
                                       shell_state_last_error(state));
      shell_fusion_set_route_text(shell_route_reason_text(route_reason));
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

static void shell_write_text(const char *text)
{
  if (text == 0)
    return;
  write(STDOUT_FILENO, text, strlen(text));
}

static int shell_handle_fusion_mode_command_impl(const char *text, int *fusion_mode)
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

static void shell_fusion_default_model(char *out, int cap)
{
  struct agent_config config;

  if (out == 0 || cap <= 0)
    return;
  agent_config_init(&config);
  agent_load_config(&config);
  if (config.model != 0 && config.model[0] != '\0')
    snprintf(out, (size_t)cap, "%s", config.model);
  else
    snprintf(out, (size_t)cap, "claude");
}

static void shell_fusion_current_cwd(char *out, int cap)
{
  char *path;

  if (out == 0 || cap <= 0)
    return;
  path = get_path_recursively((ext3_dentry *)getdentry());
  if (path != 0 && path[0] != '\0')
    snprintf(out, (size_t)cap, "%s", path);
  else
    snprintf(out, (size_t)cap, "%s", AGENT_DEFAULT_HOME);
}

static void shell_fusion_state_init(void)
{
  memset(&g_fusion_state, 0, sizeof(g_fusion_state));
  term_session_surface_init(&g_fusion_state.surface);
  g_fusion_state.next_recent_timestamp = 1;
  shell_fusion_update_surface();
  g_fusion_state.surface.transient_visible = 0;
}

static void shell_fusion_update_surface(void)
{
  char cwd[SESSION_CWD_LEN];

  shell_fusion_current_cwd(cwd, sizeof(cwd));
  if (g_fusion_state.session_ready != 0 &&
      session_read_meta(g_fusion_state.session.id, &g_fusion_state.session) == 0) {
    term_session_surface_set_session(&g_fusion_state.surface,
                                     &g_fusion_state.session,
                                     cwd,
                                     g_fusion_state.session.total_tokens);
  } else {
    term_session_surface_set_session(&g_fusion_state.surface,
                                     0,
                                     cwd,
                                     0);
  }
}

static int shell_fusion_ensure_session(void)
{
  char cwd[SESSION_CWD_LEN];
  char model[64];

  if (g_fusion_state.session_ready != 0)
    return 0;

  shell_fusion_current_cwd(cwd, sizeof(cwd));
  shell_fusion_default_model(model, sizeof(model));
  if (session_create(&g_fusion_state.session, model, cwd) < 0)
    return -1;
  g_fusion_state.session_ready = 1;
  shell_fusion_update_surface();
  term_session_surface_set_audit(&g_fusion_state.surface, "session created");
  debug_write("AUDIT eshell_fusion_session_created=",
              sizeof("AUDIT eshell_fusion_session_created=") - 1);
  debug_write(g_fusion_state.session.id, strlen(g_fusion_state.session.id));
  debug_write("\n", 1);
  return 0;
}

static int shell_fusion_resume_session(const char *session_id)
{
  if (session_id == 0 || session_id[0] == '\0')
    return -1;
  if (session_read_meta(session_id, &g_fusion_state.session) < 0)
    return -1;
  g_fusion_state.session_ready = 1;
  shell_fusion_update_surface();
  term_session_surface_set_audit(&g_fusion_state.surface, "session resumed");
  return 0;
}

static void shell_fusion_update_audit(void)
{
  struct audit_entry entries[8];
  struct term_command_block block;
  char line[TERM_SESSION_SURFACE_TEXT_MAX];
  int count = 0;
  int index;

  if (audit_read_last(entries, 8, &count) < 0 || count <= 0)
    return;

  index = count - 1;
  if (g_fusion_state.session_ready != 0) {
    int i;

    for (i = count - 1; i >= 0; i--) {
      if (strcmp(entries[i].session_id, g_fusion_state.session.id) == 0) {
        index = i;
        break;
      }
    }
  }
  snprintf(line, sizeof(line), "%s %s %s",
           entries[index].action[0] ? entries[index].action : "-",
           entries[index].tool_name[0] ? entries[index].tool_name : "-",
           entries[index].detail[0] ? entries[index].detail : "-");
  term_session_surface_set_audit(&g_fusion_state.surface, line);
  if (strcmp(entries[index].action, "propose") == 0 &&
      strcmp(entries[index].tool_name, "run_command") == 0 &&
      entries[index].detail[0] != '\0') {
    term_command_block_init(&block);
    term_command_block_set_proposal(&block, entries[index].detail);
    term_session_surface_set_command_block(&g_fusion_state.surface, &block);
  }
}

static void shell_fusion_write_drawer(void)
{
  char drawer[TERM_SESSION_SURFACE_LINE_MAX];

  if (term_session_surface_should_render(&g_fusion_state.surface) == 0)
    return;
  if (term_session_surface_format_drawer(&g_fusion_state.surface,
                                         drawer, sizeof(drawer)) > 0) {
    shell_write_text(drawer);
  }
  term_session_surface_after_render(&g_fusion_state.surface);
}

static int shell_fusion_execute_approved_command(struct shell_state *state,
                                                 const char *command)
{
  struct json_writer jw;
  char input_json[TERM_SESSION_SURFACE_PROMPT_MAX];
  char result_json[4096];
  char output_text[TERM_SESSION_SURFACE_TEXT_MAX];
  char error_text[TERM_SESSION_SURFACE_TEXT_MAX];
  char summary[TERM_SESSION_SURFACE_TEXT_MAX];
  int input_len;
  int result_len;
  int exit_code = -1;
  int status = 0;

  (void)state;
  if (!command || command[0] == '\0')
    return 1;

  term_command_block_mark_running(&g_fusion_state.surface.command_block);
  jw_init(&jw, input_json, sizeof(input_json));
  jw_object_start(&jw);
  jw_key(&jw, "command");
  jw_string(&jw, command);
  jw_object_end(&jw);
  input_len = jw_finish(&jw);
  if (input_len < 0)
    return 1;

  result_len = tool_run_command(input_json, input_len,
                                result_json, sizeof(result_json));
  if (result_len < 0)
    return 1;

  memset(output_text, 0, sizeof(output_text));
  memset(error_text, 0, sizeof(error_text));
  if (agent_tool_result_copy_string_field(result_json, result_len,
                                          "output_tail",
                                          output_text, sizeof(output_text)) < 0) {
    if (agent_tool_result_copy_string_field(result_json, result_len,
                                            "output",
                                            output_text, sizeof(output_text)) < 0) {
      output_text[0] = '\0';
    }
  }
  if (agent_tool_result_copy_string_field(result_json, result_len,
                                          "error",
                                          error_text, sizeof(error_text)) < 0) {
    error_text[0] = '\0';
  }
  if (agent_tool_result_get_exit_code(result_json, result_len, &exit_code) < 0)
    exit_code = error_text[0] != '\0' ? -1 : 0;
  status = exit_code == 0 ? 0 : 1;

  if (output_text[0] != '\0') {
    shell_write_text(output_text);
    if (output_text[strlen(output_text) - 1] != '\n')
      shell_write_text("\n");
  } else if (error_text[0] != '\0') {
    shell_write_text(error_text);
    shell_write_text("\n");
  }

  term_session_surface_set_recent_command(&g_fusion_state.surface,
                                          command,
                                          exit_code,
                                          output_text,
                                          error_text,
                                          g_fusion_state.next_recent_timestamp++);
  snprintf(summary, sizeof(summary), "approved exit=%d %s",
           exit_code,
           output_text[0] != '\0' ? output_text :
           (error_text[0] != '\0' ? error_text : "-"));
  term_command_block_mark_done(&g_fusion_state.surface.command_block,
                               exit_code, summary);
  term_session_surface_set_command_block(&g_fusion_state.surface,
                                         &g_fusion_state.surface.command_block);
  term_session_surface_set_audit(&g_fusion_state.surface, summary);
  if (g_fusion_state.session_ready != 0) {
    char compact[TERM_SESSION_SURFACE_PROMPT_MAX];

    snprintf(compact, sizeof(compact),
             "approved shell command: %s (class=%s exit=%d result=%s)",
             command,
             term_command_block_class_name(
               g_fusion_state.surface.command_block.command_class),
             exit_code,
             output_text[0] != '\0' ? output_text :
             (error_text[0] != '\0' ? error_text : "-"));
    session_append_compact(g_fusion_state.session.id, compact, 0, 0);
    shell_fusion_update_surface();
  }
  debug_write("AUDIT eshell_fusion_proposal_execute=",
              sizeof("AUDIT eshell_fusion_proposal_execute=") - 1);
  debug_write(command, strlen(command));
  debug_write("\n", 1);
  return status;
}

static int shell_fusion_maybe_auto_execute_proposal(struct shell_state *state)
{
  struct term_command_block *block = &g_fusion_state.surface.command_block;
  enum term_command_class command_class;

  if (block->active == 0 || block->state != TERM_COMMAND_STATE_PENDING)
    return 0;
  command_class = block->command_class;
  if (command_class <= TERM_COMMAND_CLASS_NONE ||
      command_class >= TERM_COMMAND_CLASS_COUNT)
    return 0;
  if (g_fusion_state.session_allow_by_class[command_class] == 0)
    return 0;
  return shell_fusion_execute_approved_command(state, block->command);
}

static void shell_fusion_record_recent_shell(const char *command,
                                             int status,
                                             const char *stderr_tail)
{
  term_session_surface_set_recent_command(&g_fusion_state.surface,
                                          command,
                                          status,
                                          "",
                                          stderr_tail ? stderr_tail : "",
                                          g_fusion_state.next_recent_timestamp++);
}

static void shell_fusion_set_route_text(const char *route_text)
{
  term_session_surface_set_route(&g_fusion_state.surface,
                                 route_text ? route_text : "shell");
}

static void shell_fusion_list_sessions(void)
{
  struct session_index index;
  char line[256];
  int i;

  if (session_list(&index) != 0 || index.count == 0) {
    shell_write_text("sessions: none\n");
    return;
  }
  for (i = 0; i < index.count; i++) {
    snprintf(line, sizeof(line), "%d. %s %s %s\n",
             i + 1,
             index.entries[i].id,
             index.entries[i].name[0] ? index.entries[i].name : "main",
             index.entries[i].cwd[0] ? index.entries[i].cwd : AGENT_DEFAULT_HOME);
    shell_write_text(line);
  }
}

static int shell_fusion_compact(const char *focus)
{
  struct conversation conv;
  char summary[1024];
  int keep_from;

  if (shell_fusion_ensure_session() < 0)
    return 1;
  conv_init(&conv, "");
  if (session_load(g_fusion_state.session.id, &conv) < 0)
    return 1;
  if (conv.turn_count <= 8) {
    shell_write_text("compact: not needed\n");
    return 0;
  }
  keep_from = conv.turn_count - 8;
  if (conv_compact(&conv, 8, focus ? focus : "", summary, sizeof(summary)) < 0)
    return 1;
  if (session_append_compact(g_fusion_state.session.id,
                             summary, 0, keep_from - 1) < 0) {
    return 1;
  }
  shell_fusion_update_surface();
  term_session_surface_set_audit(&g_fusion_state.surface, "compact appended");
  shell_write_text("compact: recorded\n");
  return 0;
}

static int shell_handle_fusion_command(struct shell_state *state,
                                       const char *text, int *fusion_mode)
{
  char token[AGENT_FUSION_TEXT_MAX];
  int start = 0;
  int len = 0;
  int mode_status;

  (void)state;
  mode_status = shell_handle_fusion_mode_command_impl(text, fusion_mode);
  if (mode_status != -2)
    return mode_status;
  if (text == 0)
    return -2;

  while (text[start] == ' ' || text[start] == '\t' ||
         text[start] == '\r' || text[start] == '\n') {
    start++;
  }
  if (text[start] != '/')
    return -2;
  while (text[start] != '\0' &&
         text[start] != ' ' && text[start] != '\t' &&
         text[start] != '\r' && text[start] != '\n' &&
         len < AGENT_FUSION_TEXT_MAX - 1) {
    token[len++] = text[start++];
  }
  token[len] = '\0';
  while (text[start] == ' ' || text[start] == '\t' ||
         text[start] == '\r' || text[start] == '\n') {
    start++;
  }

  if (strcmp(token, "/help") == 0) {
    shell_write_text("/mode /status /clear /compact [/focus] /permissions [mode]\n");
    shell_write_text("/sessions /resume <id|latest> /drawer [hidden|transient|pinned]\n");
    shell_write_text("/approve once|session /deny\n");
    return 0;
  }
  if (strcmp(token, "/status") == 0) {
    if (shell_fusion_ensure_session() < 0)
      return 1;
    shell_fusion_update_surface();
    shell_fusion_write_drawer();
    debug_write("AUDIT eshell_fusion_status\n",
                sizeof("AUDIT eshell_fusion_status\n") - 1);
    return 0;
  }
  if (strcmp(token, "/sessions") == 0) {
    shell_fusion_list_sessions();
    return 0;
  }
  if (strcmp(token, "/clear") == 0) {
    g_fusion_state.session_ready = 0;
    memset(&g_fusion_state.session, 0, sizeof(g_fusion_state.session));
    if (shell_fusion_ensure_session() < 0)
      return 1;
    term_session_surface_set_transcript(&g_fusion_state.surface, "", "");
    term_session_surface_set_recent_command(&g_fusion_state.surface,
                                            "", 0, "", "", 0);
    term_session_surface_set_command_block(&g_fusion_state.surface, 0);
    debug_write("AUDIT eshell_fusion_clear\n",
                sizeof("AUDIT eshell_fusion_clear\n") - 1);
    shell_write_text("session: new\n");
    return 0;
  }
  if (strcmp(token, "/compact") == 0) {
    debug_write("AUDIT eshell_fusion_compact\n",
                sizeof("AUDIT eshell_fusion_compact\n") - 1);
    return shell_fusion_compact(text[start] != '\0' ? text + start : "");
  }
  if (strcmp(token, "/permissions") == 0) {
    enum permission_mode mode = g_fusion_state.surface.permission_mode;
    char line[64];

    if (text[start] != '\0') {
      if (term_session_surface_permission_parse(text + start, &mode) < 0) {
        shell_write_text("permissions: usage /permissions strict|standard|permissive\n");
        return 1;
      }
      term_session_surface_set_permission_mode(&g_fusion_state.surface, mode);
      debug_write("AUDIT eshell_fusion_permissions=", 32);
      debug_write(term_session_surface_permission_name(mode),
                  strlen(term_session_surface_permission_name(mode)));
      debug_write("\n", 1);
    }
    snprintf(line, sizeof(line), "permissions: %s\n",
             term_session_surface_permission_name(mode));
    shell_write_text(line);
    return 0;
  }
  if (strcmp(token, "/resume") == 0) {
    char session_id[SESSION_ID_LEN + 1];
    char cwd[SESSION_CWD_LEN];

    if (text[start] == '\0') {
      shell_fusion_list_sessions();
      shell_write_text("resume: usage /resume <id|latest>\n");
      return 0;
    }
    memset(session_id, 0, sizeof(session_id));
    if (strcmp(text + start, "latest") == 0) {
      shell_fusion_current_cwd(cwd, sizeof(cwd));
      if (agent_resume_latest_for_cwd(cwd, session_id, sizeof(session_id)) < 0) {
        shell_write_text("resume: no session\n");
        return 0;
      }
    } else {
      snprintf(session_id, sizeof(session_id), "%s", text + start);
    }
    if (shell_fusion_resume_session(session_id) < 0) {
      shell_write_text("resume: failed\n");
      return 1;
    }
    debug_write("AUDIT eshell_fusion_resume=", 27);
    debug_write(session_id, strlen(session_id));
    debug_write("\n", 1);
    shell_write_text("resume: ok\n");
    return 0;
  }
  if (strcmp(token, "/approve") == 0) {
    struct term_command_block *block = &g_fusion_state.surface.command_block;

    if (block->active == 0 || block->state != TERM_COMMAND_STATE_PENDING) {
      shell_write_text("approve: no pending proposal\n");
      return 0;
    }
    if (text[start] == '\0') {
      shell_write_text("approve: usage /approve once|session\n");
      return 1;
    }
    if (strcmp(text + start, "session") == 0 &&
        block->command_class > TERM_COMMAND_CLASS_NONE &&
        block->command_class < TERM_COMMAND_CLASS_COUNT) {
      g_fusion_state.session_allow_by_class[block->command_class] = 1;
      debug_write("AUDIT eshell_fusion_proposal_allow_session\n",
                  sizeof("AUDIT eshell_fusion_proposal_allow_session\n") - 1);
    } else if (strcmp(text + start, "once") != 0) {
      shell_write_text("approve: usage /approve once|session\n");
      return 1;
    }
    return shell_fusion_execute_approved_command(state, block->command);
  }
  if (strcmp(token, "/deny") == 0) {
    struct term_command_block *block = &g_fusion_state.surface.command_block;

    if (block->active == 0 || block->state != TERM_COMMAND_STATE_PENDING) {
      shell_write_text("deny: no pending proposal\n");
      return 0;
    }
    term_command_block_mark_denied(block, "user denied");
    term_session_surface_set_command_block(&g_fusion_state.surface, block);
    term_session_surface_set_audit(&g_fusion_state.surface, "proposal denied");
    debug_write("AUDIT eshell_fusion_proposal_denied\n",
                sizeof("AUDIT eshell_fusion_proposal_denied\n") - 1);
    shell_write_text("proposal: denied\n");
    return 0;
  }
  if (strcmp(token, "/drawer") == 0) {
    char line[64];

    if (text[start] == '\0') {
      snprintf(line, sizeof(line), "drawer: %s\n",
               g_fusion_state.surface.drawer_mode == TERM_SESSION_DRAWER_PINNED
               ? "pinned"
               : (g_fusion_state.surface.drawer_mode == TERM_SESSION_DRAWER_HIDDEN
                  ? "hidden" : "transient"));
      shell_write_text(line);
      return 0;
    }
    if (strcmp(text + start, "hidden") == 0)
      term_session_surface_set_drawer_mode(&g_fusion_state.surface,
                                           TERM_SESSION_DRAWER_HIDDEN);
    else if (strcmp(text + start, "transient") == 0)
      term_session_surface_set_drawer_mode(&g_fusion_state.surface,
                                           TERM_SESSION_DRAWER_TRANSIENT);
    else if (strcmp(text + start, "pinned") == 0)
      term_session_surface_set_drawer_mode(&g_fusion_state.surface,
                                           TERM_SESSION_DRAWER_PINNED);
    else {
      shell_write_text("drawer: usage /drawer hidden|transient|pinned\n");
      return 1;
    }
    debug_write("AUDIT eshell_fusion_drawer=", 27);
    debug_write(text + start, strlen(text + start));
    debug_write("\n", 1);
    return 0;
  }
  return -2;
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

static void shell_audit_recovery(const struct term_command_recovery_result *result,
                                 int auto_applied)
{
  if (result == 0 || result->kind == TERM_COMMAND_RECOVERY_NONE)
    return;
  debug_write("AUDIT eshell_recovery_reason=", 29);
  debug_write(result->reason, strlen(result->reason));
  debug_write("\n", 1);
  if (result->kind == TERM_COMMAND_RECOVERY_SUGGEST) {
    debug_write("AUDIT eshell_recovery_suggest=", 30);
    debug_write(result->display, strlen(result->display));
    debug_write("\n", 1);
  } else {
    debug_write("AUDIT eshell_recovery_hint=", 27);
    debug_write(result->display, strlen(result->display));
    debug_write("\n", 1);
  }
  if (auto_applied != 0) {
    debug_write("AUDIT eshell_recovery_auto_apply=", 33);
    debug_write(result->replacement, strlen(result->replacement));
    debug_write("\n", 1);
  }
}

static int shell_handle_command_recovery(struct shell_state *state,
                                         const char *command_buf,
                                         int *status)
{
  struct term_command_recovery_result recovery;

  if (state == 0 || command_buf == 0 || status == 0)
    return 0;
  if (term_command_recovery_build(state, command_buf, *status, &recovery) == 0)
    return 0;
  term_command_recovery_write(&recovery);
  shell_audit_recovery(&recovery, 0);
  if (recovery.auto_apply == 0 || recovery.replacement[0] == '\0')
    return 1;
  shell_audit_recovery(&recovery, 1);
  *status = shell_execute_string(state, recovery.replacement);
  return 1;
}

static int shell_fusion_capture_agent_output(char *const argv[],
                                             struct bounded_output *bounded,
                                             int *status_out)
{
  int pipefd[2];
  int saved_stdout;
  int saved_stderr;
  int status = 0;
  pid_t pid;
  struct pollfd pfd;
  int child_done = 0;

  if (argv == 0 || bounded == 0)
    return -1;
  if (status_out != 0)
    *status_out = 1;
  bounded_output_init(bounded);
  if (pipe(pipefd) < 0)
    return -1;
  saved_stdout = dup(STDOUT_FILENO);
  if (saved_stdout < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  saved_stderr = dup(STDERR_FILENO);
  if (saved_stderr < 0) {
    close(saved_stdout);
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  close(STDOUT_FILENO);
  if (dup(pipefd[1]) != STDOUT_FILENO) {
    close(saved_stdout);
    close(saved_stderr);
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  close(STDERR_FILENO);
  if (dup(pipefd[1]) != STDERR_FILENO) {
    close(STDOUT_FILENO);
    dup(saved_stdout);
    close(saved_stdout);
    close(saved_stderr);
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  close(pipefd[1]);

  pid = execve("/usr/bin/agent", argv, 0);

  close(STDOUT_FILENO);
  dup(saved_stdout);
  close(saved_stdout);
  close(STDERR_FILENO);
  dup(saved_stderr);
  close(saved_stderr);
  if (pid < 0) {
    close(pipefd[0]);
    return -1;
  }

  set_foreground_pid(STDIN_FILENO, pid);
  pfd.fd = pipefd[0];
  pfd.events = POLLIN;
  while (child_done == 0) {
    int polled;

    pfd.revents = 0;
    polled = poll(&pfd, 1, 10);
    if (polled > 0 && (pfd.revents & POLLIN)) {
      char chunk[256];
      int nread = (int)read(pipefd[0], chunk, sizeof(chunk));

      if (nread > 0) {
        write(STDOUT_FILENO, chunk, (size_t)nread);
        bounded_output_append(bounded, chunk, nread);
      }
    }
    if (waitpid(pid, &status, WNOHANG) > 0)
      child_done = 1;
  }

  while (1) {
    char chunk[256];
    int nread;

    pfd.revents = 0;
    if (poll(&pfd, 1, 1) <= 0 || !(pfd.revents & POLLIN))
      break;
    nread = (int)read(pipefd[0], chunk, sizeof(chunk));
    if (nread <= 0)
      break;
    write(STDOUT_FILENO, chunk, (size_t)nread);
    bounded_output_append(bounded, chunk, nread);
  }
  close(pipefd[0]);
  set_foreground_pid(STDIN_FILENO, 0);
  bounded_output_finish(bounded, 0);
  if (status_out != 0)
    *status_out = status;
  return 0;
}

static int shell_fusion_build_agent_argv(const char *text, int force_agent,
                                         char prompt_buf[TERM_SESSION_SURFACE_PROMPT_MAX],
                                         char storage[AGENT_FUSION_MAX_ARGS + 4][AGENT_FUSION_TEXT_MAX],
                                         char *argv[AGENT_FUSION_MAX_ARGS + 4])
{
  char *base_argv[AGENT_FUSION_MAX_ARGS + 1];
  int base_argc;
  int argc = 0;
  int extra_flag = 0;
  int i;

  base_argc = agent_fusion_build_mode_argv(text, force_agent,
                                           (char (*)[AGENT_FUSION_TEXT_MAX])storage,
                                           base_argv);
  if (base_argc <= 0)
    return base_argc;

  argv[argc++] = base_argv[0];
  if (g_fusion_state.surface.permission_mode != PERM_STANDARD) {
    snprintf(storage[AGENT_FUSION_MAX_ARGS], AGENT_FUSION_TEXT_MAX,
             "--perm-mode=%s",
             term_session_surface_permission_name(
               g_fusion_state.surface.permission_mode));
    argv[argc++] = storage[AGENT_FUSION_MAX_ARGS];
    extra_flag = 1;
  }
  if (base_argc >= 3 && strcmp(base_argv[1], "run") == 0) {
    if (shell_fusion_ensure_session() < 0)
      return -1;
    argv[argc++] = "--proposal-shell";
    if (term_session_surface_build_prompt(&g_fusion_state.surface,
                                          base_argv[2],
                                          prompt_buf,
                                          TERM_SESSION_SURFACE_PROMPT_MAX) < 0) {
      return -1;
    }
    argv[argc++] = "--resume";
    argv[argc++] = g_fusion_state.session.id;
    argv[argc++] = prompt_buf;
    argv[argc] = 0;
    return argc;
  }

  for (i = 1; i < base_argc; i++)
    argv[i + extra_flag] = base_argv[i];
  argv[base_argc + extra_flag] = 0;
  return base_argc + extra_flag;
}

static int shell_run_agent_fusion(struct shell_state *state,
                                  const char *text, int force_agent)
{
  char prompt_buf[TERM_SESSION_SURFACE_PROMPT_MAX];
  char storage[AGENT_FUSION_MAX_ARGS + 4][AGENT_FUSION_TEXT_MAX];
  char *argv[AGENT_FUSION_MAX_ARGS + 4];
  struct bounded_output bounded;
  const char *assistant_text;
  int argc;
  int status = 0;

  (void)state;
  argc = shell_fusion_build_agent_argv(text, force_agent,
                                       prompt_buf, storage, argv);
  if (argc == 0)
    return -2;
  if (argc < 0) {
    write(STDERR_FILENO, "eshell: invalid agent command\n", 30);
    return 1;
  }

  debug_write("AUDIT eshell_agent_route_begin\n", 31);
  if (shell_fusion_capture_agent_output(argv, &bounded, &status) < 0) {
    write(STDERR_FILENO, "eshell: failed to launch agent\n", 31);
    return 1;
  }
  shell_fusion_update_surface();
  shell_fusion_update_audit();
  if (shell_fusion_maybe_auto_execute_proposal(state) != 0)
    status = 1;
  assistant_text = bounded.tail_len > 0 ? bounded.tail : bounded.inline_buf;
  term_session_surface_set_transcript(&g_fusion_state.surface,
                                      text,
                                      assistant_text);
  shell_fusion_set_route_text("agent");
  debug_write("AUDIT eshell_agent_route_done\n", 30);
  return status;
}
