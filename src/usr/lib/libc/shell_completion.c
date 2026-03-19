#include <shell_completion.h>
#include <key.h>
#include <stdio.h>
#include <string.h>

#ifdef TEST_BUILD
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <ext3fs.h>
#include <fs.h>
#include <stdlib.h>
#endif

#define SHELL_COMPLETION_ICANON 0x0001U
#define SHELL_COMPLETION_DIR_BUF_SIZE 4096

struct shell_completion_token {
  int raw_start;
  int raw_len;
  int raw_chars;
  char logical[SHELL_COMPLETION_LINE_MAX];
  char typed_dir[SHELL_COMPLETION_LINE_MAX];
  char lookup_dir[SHELL_COMPLETION_LINE_MAX];
  char prefix[SHELL_COMPLETION_LINE_MAX];
  char quote_char;
};

#ifndef TEST_BUILD
struct shell_completion_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};
#endif

static void shell_completion_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0)
    src = "";

  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static int shell_completion_append_char(char *dst, int len, int cap, char ch)
{
  if (dst == 0 || cap <= 0 || len < 0 || len >= cap - 1)
    return -1;
  dst[len++] = ch;
  dst[len] = '\0';
  return len;
}

static int shell_completion_append_text(char *dst, int len, int cap,
                                        const char *src)
{
  if (src == 0)
    return len;
  while (*src != '\0') {
    len = shell_completion_append_char(dst, len, cap, *src++);
    if (len < 0)
      return -1;
  }
  return len;
}

static int shell_completion_utf8_prev_char_start(const char *data, int len)
{
  int index;

  if (data == 0 || len <= 0)
    return 0;

  index = len - 1;
  while (index > 0 &&
         (((unsigned char)data[index]) & 0xc0U) == 0x80U) {
    index--;
  }
  return index;
}

static int shell_completion_utf8_char_count(const char *data, int len)
{
  int count = 0;
  int index = 0;

  if (data == 0 || len <= 0)
    return 0;

  while (index < len) {
    count++;
    index++;
    while (index < len &&
           (((unsigned char)data[index]) & 0xc0U) == 0x80U) {
      index++;
    }
  }
  return count;
}

static int shell_completion_is_delim(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
         ch == '|' || ch == '&' || ch == ';' || ch == '<' || ch == '>';
}

static int shell_completion_is_prompt(const char *text, int len)
{
  if (text == 0 || len < 9)
    return 0;
  if (strncmp(text, "sodex ", 6) != 0)
    return 0;
  return len >= 2 && text[len - 2] == '>' && text[len - 1] == ' ';
}

static void shell_completion_clear_session(struct shell_completion_state *state)
{
  if (state == 0)
    return;

  state->completion_active = 0;
  state->candidate_count = 0;
  state->candidate_index = -1;
  state->token_raw_start = 0;
  state->token_raw_len = 0;
  state->original_token_raw_len = 0;
  state->original_token_raw[0] = '\0';
  state->current_token_raw_len = 0;
  state->current_token_raw_chars = 0;
  state->current_token_raw[0] = '\0';
  state->typed_dir_logical[0] = '\0';
  state->prefix_logical[0] = '\0';
  state->quote_char = '\0';
}

static void shell_completion_clear_prompt(struct shell_completion_state *state)
{
  if (state == 0)
    return;
  state->prompt_len = 0;
  state->prompt_line[0] = '\0';
}

static void shell_completion_accept_prompt(struct shell_completion_state *state,
                                           const char *prompt, int len)
{
  int path_len;

  if (state == 0 || prompt == 0 || shell_completion_is_prompt(prompt, len) == 0)
    return;

  path_len = len - 8;
  if (path_len <= 0)
    return;
  if (path_len >= SHELL_COMPLETION_PATH_MAX)
    path_len = SHELL_COMPLETION_PATH_MAX - 1;

  memcpy(state->cwd, prompt + 6, (size_t)path_len);
  state->cwd[path_len] = '\0';
  state->cwd_valid = 1;
  state->line_len = 0;
  state->line[0] = '\0';
  state->sync_valid = 1;
  shell_completion_clear_session(state);
  shell_completion_clear_prompt(state);
}

static int shell_completion_path_join(char *dst, int cap,
                                      const char *left, const char *right)
{
  int len = 0;
  int left_len;

  if (dst == 0 || cap <= 0 || left == 0 || right == 0)
    return -1;

  dst[0] = '\0';
  left_len = (int)strlen(left);
  len = shell_completion_append_text(dst, len, cap, left);
  if (len < 0)
    return -1;
  if (left_len > 0 && left[left_len - 1] != '/') {
    len = shell_completion_append_char(dst, len, cap, '/');
    if (len < 0)
      return -1;
  }
  len = shell_completion_append_text(dst, len, cap, right);
  return len;
}

static int shell_completion_normalize_absolute(const char *src,
                                               char *dst, int cap)
{
  char temp[SHELL_COMPLETION_PATH_MAX];
  int seg_start[SHELL_COMPLETION_LINE_MAX];
  int seg_len[SHELL_COMPLETION_LINE_MAX];
  int seg_count = 0;
  int i = 0;
  int out_len = 0;

  if (src == 0 || dst == 0 || cap <= 1 || src[0] != '/')
    return -1;

  while (src[i] != '\0') {
    int start;
    int len;

    while (src[i] == '/')
      i++;
    if (src[i] == '\0')
      break;
    start = i;
    while (src[i] != '\0' && src[i] != '/')
      i++;
    len = i - start;
    if (len == 1 && src[start] == '.')
      continue;
    if (len == 2 && src[start] == '.' && src[start + 1] == '.') {
      if (seg_count > 0)
        seg_count--;
      continue;
    }
    if (seg_count >= SHELL_COMPLETION_LINE_MAX)
      return -1;
    seg_start[seg_count] = start;
    seg_len[seg_count] = len;
    seg_count++;
  }

  temp[0] = '/';
  temp[1] = '\0';
  out_len = 1;
  for (i = 0; i < seg_count; i++) {
    int j;

    if (out_len > 1) {
      out_len = shell_completion_append_char(temp, out_len,
                                             sizeof(temp), '/');
      if (out_len < 0)
        return -1;
    }
    for (j = 0; j < seg_len[i]; j++) {
      out_len = shell_completion_append_char(temp, out_len,
                                             sizeof(temp),
                                             src[seg_start[i] + j]);
      if (out_len < 0)
        return -1;
    }
  }

  shell_completion_copy_text(dst, cap, temp);
  return (int)strlen(dst);
}

static int shell_completion_resolve_dir(const struct shell_completion_state *state,
                                        const char *lookup_dir,
                                        char *dst, int cap)
{
  char joined[SHELL_COMPLETION_PATH_MAX];
  const char *cwd;

  if (state == 0 || lookup_dir == 0 || dst == 0 || cap <= 1)
    return -1;

  cwd = state->cwd_valid != 0 ? state->cwd : "/home/user";
  if (lookup_dir[0] == '\0')
    return shell_completion_normalize_absolute(cwd, dst, cap);
  if (lookup_dir[0] == '/')
    return shell_completion_normalize_absolute(lookup_dir, dst, cap);
  if (shell_completion_path_join(joined, sizeof(joined), cwd, lookup_dir) < 0)
    return -1;
  return shell_completion_normalize_absolute(joined, dst, cap);
}

static int shell_completion_parse_token(const struct shell_completion_state *state,
                                        struct shell_completion_token *token)
{
  int i;
  int active = 0;
  int logical_len = 0;
  int raw_start = 0;
  int escape = 0;
  char quote = '\0';
  int open_quote_start = -1;

  if (state == 0 || token == 0 || state->line_len <= 0)
    return 0;

  memset(token, 0, sizeof(*token));
  for (i = 0; i < state->line_len; i++) {
    char ch = state->line[i];

    if (quote == '\0') {
      if (escape != 0) {
        if (active == 0) {
          active = 1;
          raw_start = i - 1;
        }
        if (logical_len >= SHELL_COMPLETION_LINE_MAX - 1)
          return 0;
        token->logical[logical_len++] = ch;
        token->logical[logical_len] = '\0';
        escape = 0;
        continue;
      }
      if (shell_completion_is_delim(ch) != 0) {
        active = 0;
        logical_len = 0;
        raw_start = i + 1;
        open_quote_start = -1;
        token->quote_char = '\0';
        token->logical[0] = '\0';
        continue;
      }
      if (active == 0) {
        active = 1;
        raw_start = i;
        logical_len = 0;
        open_quote_start = -1;
        token->quote_char = '\0';
        token->logical[0] = '\0';
      }
      if (ch == '\\') {
        escape = 1;
        continue;
      }
      if (ch == '"' || ch == '\'') {
        if (logical_len == 0 && open_quote_start < 0) {
          raw_start = i + 1;
          open_quote_start = i;
          token->quote_char = ch;
          quote = ch;
          continue;
        }
        quote = ch;
        token->quote_char = '\0';
        open_quote_start = -2;
        continue;
      }
      if (logical_len >= SHELL_COMPLETION_LINE_MAX - 1)
        return 0;
      token->logical[logical_len++] = ch;
      token->logical[logical_len] = '\0';
      continue;
    }

    if (quote == '"' && escape != 0) {
      if (logical_len >= SHELL_COMPLETION_LINE_MAX - 1)
        return 0;
      token->logical[logical_len++] = ch;
      token->logical[logical_len] = '\0';
      escape = 0;
      continue;
    }
    if (quote == '"' && ch == '\\') {
      escape = 1;
      continue;
    }
    if (ch == quote) {
      quote = '\0';
      continue;
    }
    if (logical_len >= SHELL_COMPLETION_LINE_MAX - 1)
      return 0;
    token->logical[logical_len++] = ch;
    token->logical[logical_len] = '\0';
  }

  if (escape != 0)
    return 0;

  if (active == 0) {
    if (state->line_len > 0 &&
        shell_completion_is_delim(state->line[state->line_len - 1]) != 0) {
      token->raw_start = state->line_len;
      token->raw_len = 0;
      token->raw_chars = 0;
      token->logical[0] = '\0';
      token->typed_dir[0] = '\0';
      token->lookup_dir[0] = '\0';
      token->prefix[0] = '\0';
      token->quote_char = '\0';
      return 1;
    }
    return 0;
  }

  if (logical_len <= 0) {
    if (quote != '\0' &&
        open_quote_start >= 0 &&
        open_quote_start == raw_start - 1) {
      token->raw_start = raw_start;
      token->raw_len = 0;
      token->raw_chars = 0;
      token->logical[0] = '\0';
      token->typed_dir[0] = '\0';
      token->lookup_dir[0] = '\0';
      token->prefix[0] = '\0';
      token->quote_char = quote;
      return 1;
    }
    return 0;
  }

  token->raw_start = raw_start;
  token->raw_len = state->line_len - raw_start;
  token->raw_chars = shell_completion_utf8_char_count(state->line + raw_start,
                                                      token->raw_len);
  if (quote != '\0') {
    if (open_quote_start >= 0 && open_quote_start == raw_start - 1) {
      token->raw_start = open_quote_start + 1;
      token->raw_len = state->line_len - token->raw_start;
      token->raw_chars = shell_completion_utf8_char_count(
          state->line + token->raw_start,
          token->raw_len);
      token->quote_char = quote;
    } else {
      return 0;
    }
  } else {
    token->quote_char = '\0';
  }

  {
    const char *slash = strrchr(token->logical, '/');
    if (slash == 0) {
      token->typed_dir[0] = '\0';
      token->lookup_dir[0] = '\0';
      shell_completion_copy_text(token->prefix,
                                 sizeof(token->prefix),
                                 token->logical);
    } else {
      int dir_len = (int)(slash - token->logical);
      if (slash == token->logical) {
        shell_completion_copy_text(token->typed_dir,
                                   sizeof(token->typed_dir),
                                   "/");
        shell_completion_copy_text(token->lookup_dir,
                                   sizeof(token->lookup_dir),
                                   "/");
      } else {
        if (dir_len >= SHELL_COMPLETION_LINE_MAX - 1)
          dir_len = SHELL_COMPLETION_LINE_MAX - 2;
        memcpy(token->typed_dir, token->logical, (size_t)(dir_len + 1));
        token->typed_dir[dir_len + 1] = '\0';
        memcpy(token->lookup_dir, token->logical, (size_t)dir_len);
        token->lookup_dir[dir_len] = '\0';
      }
      shell_completion_copy_text(token->prefix, sizeof(token->prefix),
                                 slash + 1);
      if (slash[1] == '\0')
        token->prefix[0] = '\0';
    }
  }

  return 1;
}

static int shell_completion_match_name(const char *name, const char *prefix)
{
  int prefix_len;

  if (name == 0 || prefix == 0)
    return 0;
  if (prefix[0] != '.' && name[0] == '.')
    return 0;

  prefix_len = (int)strlen(prefix);
  if (prefix_len == 0)
    return 1;
  return strncmp(name, prefix, (size_t)prefix_len) == 0;
}

static void shell_completion_sort_candidates(struct shell_completion_candidate *candidates,
                                             int count)
{
  int i;
  int j;

  for (i = 1; i < count; i++) {
    struct shell_completion_candidate tmp = candidates[i];
    j = i - 1;
    while (j >= 0 && strcmp(candidates[j].name, tmp.name) > 0) {
      candidates[j + 1] = candidates[j];
      j--;
    }
    candidates[j + 1] = tmp;
  }
}

static int shell_completion_collect_candidates(
    const struct shell_completion_state *state,
    const struct shell_completion_token *token,
    struct shell_completion_candidate *candidates,
    int cap)
{
  char dir_path[SHELL_COMPLETION_PATH_MAX];
  int count = 0;

  if (state == 0 || token == 0 || candidates == 0 || cap <= 0)
    return 0;
  if (shell_completion_resolve_dir(state, token->lookup_dir,
                                   dir_path, sizeof(dir_path)) < 0)
    return 0;

#ifdef TEST_BUILD
  {
    DIR *dirp = opendir(dir_path);
    struct dirent *de;

    if (dirp == 0)
      return 0;
    while ((de = readdir(dirp)) != 0 && count < cap) {
      char full_path[SHELL_COMPLETION_PATH_MAX];
      struct stat st;

      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        continue;
      if (shell_completion_match_name(de->d_name, token->prefix) == 0)
        continue;
      if (shell_completion_path_join(full_path, sizeof(full_path),
                                     dir_path, de->d_name) < 0)
        continue;
      shell_completion_copy_text(candidates[count].name,
                                 sizeof(candidates[count].name),
                                 de->d_name);
      candidates[count].is_dir = 0;
      if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        candidates[count].is_dir = 1;
      count++;
    }
    closedir(dirp);
  }
#else
  {
    int fd;
    char dir_buf[SHELL_COMPLETION_DIR_BUF_SIZE];
    int bytes_read;
    int offset = 0;

    fd = open(dir_path, O_RDONLY, 0);
    if (fd < 0)
      return 0;
    bytes_read = (int)read(fd, dir_buf, sizeof(dir_buf));
    close(fd);
    if (bytes_read <= 0)
      return 0;

    while (offset < bytes_read && count < cap) {
      struct shell_completion_dir_entry *de =
          (struct shell_completion_dir_entry *)(dir_buf + offset);
      char name[SHELL_COMPLETION_NAME_MAX];
      int nlen;

      if (de->rec_len == 0 || de->rec_len < 8)
        break;
      if (offset + de->rec_len > bytes_read)
        break;
      if (de->inode == 0 || de->name_len == 0) {
        offset += de->rec_len;
        continue;
      }

      nlen = de->name_len;
      if (nlen >= SHELL_COMPLETION_NAME_MAX)
        nlen = SHELL_COMPLETION_NAME_MAX - 1;
      memcpy(name, de->name, (size_t)nlen);
      name[nlen] = '\0';

      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        offset += de->rec_len;
        continue;
      }
      if (shell_completion_match_name(name, token->prefix) == 0) {
        offset += de->rec_len;
        continue;
      }

      shell_completion_copy_text(candidates[count].name,
                                 sizeof(candidates[count].name),
                                 name);
      candidates[count].is_dir = (de->file_type == FTYPE_DIR);
      count++;
      offset += de->rec_len;
    }
  }
#endif

  shell_completion_sort_candidates(candidates, count);
  return count;
}

static int shell_completion_common_prefix(struct shell_completion_candidate *candidates,
                                          int count, char *out, int cap)
{
  int len;
  int i;

  if (candidates == 0 || out == 0 || cap <= 0 || count <= 0)
    return 0;

  shell_completion_copy_text(out, cap, candidates[0].name);
  len = (int)strlen(out);
  for (i = 1; i < count; i++) {
    while (len > 0 &&
           strncmp(out, candidates[i].name, (size_t)len) != 0) {
      len--;
      out[len] = '\0';
    }
  }
  while (len > 0 &&
         (((unsigned char)out[len]) & 0xc0U) == 0x80U) {
    len--;
    out[len] = '\0';
  }
  return len;
}

static int shell_completion_needs_escape(unsigned char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\\' || ch == '"' || ch == '\'' ||
         ch == '|' || ch == '&' || ch == ';' || ch == '<' || ch == '>' ||
         ch == '#';
}

static int shell_completion_encode_token(const char *logical, char quote_char,
                                         char *out, int cap)
{
  int len = 0;
  int i;
  int logical_len;

  if (logical == 0 || out == 0 || cap <= 0)
    return -1;

  out[0] = '\0';
  logical_len = (int)strlen(logical);
  for (i = 0; i < logical_len; i++) {
    unsigned char ch = (unsigned char)logical[i];

    if (quote_char == '\'') {
      if (ch == '\'')
        return -1;
      len = shell_completion_append_char(out, len, cap, (char)ch);
    } else if (quote_char == '"') {
      if (ch == '"' || ch == '\\') {
        len = shell_completion_append_char(out, len, cap, '\\');
        if (len < 0)
          return -1;
      }
      len = shell_completion_append_char(out, len, cap, (char)ch);
    } else {
      if (shell_completion_needs_escape(ch) != 0) {
        len = shell_completion_append_char(out, len, cap, '\\');
        if (len < 0)
          return -1;
      }
      len = shell_completion_append_char(out, len, cap, (char)ch);
    }
    if (len < 0)
      return -1;
  }

  return len;
}

static int shell_completion_build_apply_bytes(int backspaces,
                                              const char *replacement,
                                              int replacement_len,
                                              char *out, int out_cap)
{
  int len = 0;
  int i;

  if (out == 0 || out_cap <= 0 || backspaces < 0 || replacement_len < 0)
    return -1;

  out[0] = '\0';
  for (i = 0; i < backspaces; i++) {
    len = shell_completion_append_char(out, len, out_cap, KEY_BACK);
    if (len < 0)
      return -1;
  }
  if (replacement != 0 && replacement_len > 0) {
    if (len + replacement_len >= out_cap)
      return -1;
    memcpy(out + len, replacement, (size_t)replacement_len);
    len += replacement_len;
    out[len] = '\0';
  }
  return len;
}

static int shell_completion_apply_logical(struct shell_completion_state *state,
                                          const struct shell_completion_token *token,
                                          const char *logical,
                                          int finalize_unique,
                                          int is_dir,
                                          char *out, int out_cap)
{
  char logical_text[SHELL_COMPLETION_LINE_MAX];
  char raw_text[SHELL_COMPLETION_LINE_MAX];
  int logical_len = 0;
  int raw_len;

  if (state == 0 || token == 0 || logical == 0 || out == 0)
    return -1;

  logical_text[0] = '\0';
  logical_len = shell_completion_append_text(logical_text, logical_len,
                                             sizeof(logical_text), logical);
  if (logical_len < 0)
    return -1;
  if (finalize_unique != 0) {
    if (is_dir != 0) {
      logical_len = shell_completion_append_char(logical_text, logical_len,
                                                 sizeof(logical_text), '/');
    }
    if (logical_len < 0)
      return -1;
  }

  raw_len = shell_completion_encode_token(logical_text, token->quote_char,
                                          raw_text, sizeof(raw_text));
  if (raw_len < 0)
    return -1;
  if (finalize_unique != 0 && is_dir == 0 && token->quote_char == '\0') {
    raw_len = shell_completion_append_char(raw_text, raw_len,
                                           sizeof(raw_text), ' ');
    if (raw_len < 0)
      return -1;
  }
  if (raw_len >= SHELL_COMPLETION_LINE_MAX)
    return -1;

  shell_completion_copy_text(state->current_token_raw,
                             sizeof(state->current_token_raw),
                             raw_text);
  state->current_token_raw_len = raw_len;
  state->current_token_raw_chars = shell_completion_utf8_char_count(raw_text,
                                                                    raw_len);
  return shell_completion_build_apply_bytes(token->raw_chars, raw_text, raw_len,
                                            out, out_cap);
}

static int shell_completion_build_from_state(struct shell_completion_state *state,
                                             int candidate_index,
                                             int finalize_unique,
                                             char *out, int out_cap)
{
  char logical[SHELL_COMPLETION_LINE_MAX];
  int logical_len = 0;
  int is_dir = 0;
  struct shell_completion_token token;

  if (state == 0 || out == 0)
    return -1;

  memset(&token, 0, sizeof(token));
  token.raw_len = state->current_token_raw_len;
  token.raw_chars = state->current_token_raw_chars;
  token.quote_char = state->quote_char;

  logical_len = shell_completion_append_text(logical, logical_len,
                                             sizeof(logical),
                                             state->typed_dir_logical);
  if (logical_len < 0)
    return -1;

  if (candidate_index >= 0) {
    logical_len = shell_completion_append_text(logical, logical_len,
                                               sizeof(logical),
                                               state->candidates[candidate_index].name);
    if (logical_len < 0)
      return -1;
    is_dir = state->candidates[candidate_index].is_dir;
  } else {
    logical_len = shell_completion_append_text(logical, logical_len,
                                               sizeof(logical),
                                               state->prefix_logical);
    if (logical_len < 0)
      return -1;
  }

  token.raw_len = state->current_token_raw_len;
  token.raw_chars = state->current_token_raw_chars;
  return shell_completion_apply_logical(state, &token, logical,
                                        finalize_unique, is_dir,
                                        out, out_cap);
}

void shell_completion_state_init(struct shell_completion_state *state)
{
  if (state == 0)
    return;
  memset(state, 0, sizeof(*state));
  state->sync_valid = 1;
  state->candidate_index = -1;
  shell_completion_copy_text(state->cwd, sizeof(state->cwd), "/home/user");
  state->cwd_valid = 1;
}

void shell_completion_state_set_shell_pid(struct shell_completion_state *state,
                                          pid_t shell_pid)
{
  if (state == 0)
    return;
  state->shell_pid = shell_pid;
}

void shell_completion_state_reset(struct shell_completion_state *state)
{
  if (state == 0)
    return;
  state->line_len = 0;
  state->line[0] = '\0';
  state->sync_valid = 1;
  shell_completion_clear_session(state);
  shell_completion_clear_prompt(state);
}

void shell_completion_state_invalidate(struct shell_completion_state *state)
{
  if (state == 0)
    return;
  state->line_len = 0;
  state->line[0] = '\0';
  state->sync_valid = 0;
  shell_completion_clear_session(state);
  shell_completion_clear_prompt(state);
}

int shell_completion_state_valid(const struct shell_completion_state *state)
{
  if (state == 0)
    return 0;
  return state->sync_valid != 0;
}

int shell_completion_state_can_track(const struct shell_completion_state *state,
                                     pid_t foreground_pid,
                                     u_int32_t lflag)
{
  if (state == 0 || state->shell_pid <= 0)
    return 0;
  if ((lflag & SHELL_COMPLETION_ICANON) == 0)
    return 0;
  if (foreground_pid > 0 && foreground_pid != state->shell_pid)
    return 0;
  return 1;
}

int shell_completion_state_can_complete(const struct shell_completion_state *state,
                                        pid_t foreground_pid,
                                        u_int32_t lflag,
                                        int ime_busy)
{
  if (shell_completion_state_can_track(state, foreground_pid, lflag) == 0)
    return 0;
  if (ime_busy != 0)
    return 0;
  return shell_completion_state_valid(state);
}

int shell_completion_state_feed_input(struct shell_completion_state *state,
                                      const char *buf, int len)
{
  int i;

  if (state == 0 || buf == 0 || len <= 0)
    return 0;

  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)buf[i];

    if (state->sync_valid == 0) {
      if (ch == KEY_ENTER || ch == '\n')
        shell_completion_state_reset(state);
      continue;
    }

    if (ch == KEY_BACK || ch == 0x7fU) {
      if (state->line_len > 0) {
        state->line_len =
            shell_completion_utf8_prev_char_start(state->line, state->line_len);
        state->line[state->line_len] = '\0';
      }
      shell_completion_clear_prompt(state);
      continue;
    }

    if (ch == KEY_ENTER || ch == '\n') {
      state->line_len = 0;
      state->line[0] = '\0';
      shell_completion_clear_session(state);
      shell_completion_clear_prompt(state);
      continue;
    }

    if (ch == 0)
      continue;

    if (ch < 0x20U) {
      shell_completion_state_invalidate(state);
      continue;
    }

    if (state->line_len >= SHELL_COMPLETION_LINE_MAX - 1) {
      shell_completion_state_invalidate(state);
      continue;
    }

    state->line[state->line_len++] = (char)ch;
    state->line[state->line_len] = '\0';
    shell_completion_clear_prompt(state);
  }

  return state->line_len;
}

void shell_completion_state_observe_output(struct shell_completion_state *state,
                                           const char *buf, int len,
                                           pid_t foreground_pid)
{
  int i;

  if (state == 0 || buf == 0 || len <= 0)
    return;

  if (foreground_pid > 0 && foreground_pid != state->shell_pid) {
    shell_completion_state_invalidate(state);
    return;
  }

  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)buf[i];

    if (ch == '\r') {
      shell_completion_clear_prompt(state);
      continue;
    }
    if (ch == '\n') {
      if (state->sync_valid == 0)
        state->sync_valid = 1;
      shell_completion_clear_prompt(state);
      continue;
    }

    if (state->line_len != 0)
      continue;
    if (state->prompt_len >= SHELL_COMPLETION_LINE_MAX - 1) {
      shell_completion_clear_prompt(state);
      continue;
    }
    state->prompt_line[state->prompt_len++] = (char)ch;
    state->prompt_line[state->prompt_len] = '\0';
    if (shell_completion_is_prompt(state->prompt_line,
                                   state->prompt_len) != 0) {
      shell_completion_accept_prompt(state,
                                     state->prompt_line,
                                     state->prompt_len);
    }
  }
}

int shell_completion_state_active(const struct shell_completion_state *state)
{
  if (state == 0)
    return 0;
  return state->completion_active != 0;
}

void shell_completion_state_finish_completion(struct shell_completion_state *state)
{
  shell_completion_clear_session(state);
}

int shell_completion_state_complete(struct shell_completion_state *state,
                                    int reverse,
                                    char *out, int out_cap)
{
  struct shell_completion_token token;
  struct shell_completion_candidate candidates[SHELL_COMPLETION_CANDIDATE_MAX];
  char common_prefix[SHELL_COMPLETION_LINE_MAX];
  int count;
  int common_len;

  if (state == 0 || out == 0 || out_cap <= 0 || state->sync_valid == 0)
    return 0;

  if (state->completion_active != 0) {
    if (state->candidate_count <= 0)
      return 0;
    if (state->candidate_index < 0) {
      state->candidate_index = (reverse != 0) ?
          state->candidate_count - 1 : 0;
    } else if (reverse != 0) {
      state->candidate_index--;
      if (state->candidate_index < 0)
        state->candidate_index = state->candidate_count - 1;
    } else {
      state->candidate_index =
          (state->candidate_index + 1) % state->candidate_count;
    }
    return shell_completion_build_from_state(state,
                                             state->candidate_index,
                                             0, out, out_cap);
  }

  if (shell_completion_parse_token(state, &token) == 0)
    return 0;
  count = shell_completion_collect_candidates(state, &token,
                                              candidates,
                                              SHELL_COMPLETION_CANDIDATE_MAX);
  if (count <= 0)
    return 0;

  shell_completion_clear_session(state);
  memcpy(state->candidates, candidates, sizeof(candidates));
  state->candidate_count = count;
  state->candidate_index = -1;
  state->token_raw_start = token.raw_start;
  state->token_raw_len = token.raw_len;
  shell_completion_copy_text(state->typed_dir_logical,
                             sizeof(state->typed_dir_logical),
                             token.typed_dir);
  shell_completion_copy_text(state->prefix_logical,
                             sizeof(state->prefix_logical),
                             token.prefix);
  state->quote_char = token.quote_char;
  if (token.raw_len >= SHELL_COMPLETION_LINE_MAX)
    return 0;
  memcpy(state->original_token_raw,
         state->line + token.raw_start,
         (size_t)token.raw_len);
  state->original_token_raw[token.raw_len] = '\0';
  state->original_token_raw_len = token.raw_len;
  shell_completion_copy_text(state->current_token_raw,
                             sizeof(state->current_token_raw),
                             state->original_token_raw);
  state->current_token_raw_len = token.raw_len;
  state->current_token_raw_chars = token.raw_chars;

  if (count == 1) {
    char logical[SHELL_COMPLETION_LINE_MAX];
    int logical_len = 0;

    logical_len = shell_completion_append_text(logical, logical_len,
                                               sizeof(logical),
                                               token.typed_dir);
    if (logical_len < 0)
      return 0;
    logical_len = shell_completion_append_text(logical, logical_len,
                                               sizeof(logical),
                                               candidates[0].name);
    if (logical_len < 0)
      return 0;
    shell_completion_clear_session(state);
    return shell_completion_apply_logical(state, &token, logical,
                                          1, candidates[0].is_dir,
                                          out, out_cap);
  }

  common_len = shell_completion_common_prefix(candidates, count,
                                              common_prefix,
                                              sizeof(common_prefix));
  state->completion_active = 1;
  if (common_len > (int)strlen(token.prefix)) {
    char logical[SHELL_COMPLETION_LINE_MAX];
    int logical_len = 0;

    logical_len = shell_completion_append_text(logical, logical_len,
                                               sizeof(logical),
                                               token.typed_dir);
    if (logical_len < 0)
      return 0;
    logical_len = shell_completion_append_text(logical, logical_len,
                                               sizeof(logical),
                                               common_prefix);
    if (logical_len < 0)
      return 0;
    shell_completion_copy_text(state->prefix_logical,
                               sizeof(state->prefix_logical),
                               common_prefix);
    return shell_completion_apply_logical(state, &token, logical,
                                          0, 0, out, out_cap);
  }

  state->candidate_index = (reverse != 0) ? count - 1 : 0;
  return shell_completion_build_from_state(state,
                                           state->candidate_index,
                                           0, out, out_cap);
}

int shell_completion_state_cancel_completion(struct shell_completion_state *state,
                                             char *out, int out_cap)
{
  int len;

  if (state == 0 || out == 0 || state->completion_active == 0)
    return 0;

  len = shell_completion_build_apply_bytes(state->current_token_raw_chars,
                                           state->original_token_raw,
                                           state->original_token_raw_len,
                                           out, out_cap);
  shell_completion_clear_session(state);
  return len;
}

int shell_completion_state_overlay_text(const struct shell_completion_state *state,
                                        char *buf, int cap)
{
  const char *name;

  if (state == 0 || buf == 0 || cap <= 0 || state->completion_active == 0)
    return 0;

  name = state->prefix_logical;
  if (state->candidate_index >= 0 &&
      state->candidate_index < state->candidate_count) {
    name = state->candidates[state->candidate_index].name;
  }

  if (state->candidate_count <= 0)
    return 0;

  if (state->candidate_index >= 0) {
    snprintf(buf, (size_t)cap, "CMP %d/%d %s%s",
             state->candidate_index + 1,
             state->candidate_count,
             name,
             state->candidates[state->candidate_index].is_dir != 0 ? "/" : "");
  } else {
    snprintf(buf, (size_t)cap, "CMP 0/%d %s",
             state->candidate_count,
             name);
  }
  return (int)strlen(buf);
}

const char *shell_completion_state_line(const struct shell_completion_state *state)
{
  if (state == 0)
    return "";
  return state->line;
}

int shell_completion_state_line_len(const struct shell_completion_state *state)
{
  if (state == 0)
    return 0;
  return state->line_len;
}
