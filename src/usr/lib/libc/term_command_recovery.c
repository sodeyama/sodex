#include <term_command_recovery.h>
#ifdef TEST_BUILD
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fs.h>
#include <stdlib.h>
#endif
#include <stdio.h>
#include <string.h>

#define TERM_COMMAND_RECOVERY_CANDIDATE_MAX 96
#define TERM_COMMAND_RECOVERY_NAME_MAX 128
#define TERM_COMMAND_RECOVERY_PATH_MAX 512
#define TERM_COMMAND_RECOVERY_DIR_BUF_SIZE 4096

#ifndef TEST_BUILD
struct term_command_recovery_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};
#endif

struct term_command_candidate {
  char token[TERM_COMMAND_RECOVERY_NAME_MAX];
  char replacement[TERM_COMMAND_RECOVERY_NAME_MAX];
  int score;
  int distance;
  int transposed;
};

static const char *g_term_command_builtins[] = {
  "cd", "exit", "export", "set", ".", "wait", "jobs", "fg", "bg",
  "trap", "break", "continue", "echo", "true", "false", "[", "test",
  "alias", "unalias", "type", "history", "command"
};

static void term_command_recovery_copy_text(char *dst, int cap, const char *src)
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

static int term_command_recovery_append_char(char *dst, int cap, int len, char ch)
{
  if (dst == 0 || cap <= 0 || len < 0 || len >= cap - 1)
    return -1;
  dst[len++] = ch;
  dst[len] = '\0';
  return len;
}

static int term_command_recovery_append_text(char *dst, int cap, int len,
                                             const char *src)
{
  if (src == 0)
    return len;
  while (*src != '\0') {
    len = term_command_recovery_append_char(dst, cap, len, *src++);
    if (len < 0)
      return -1;
  }
  return len;
}

static int term_command_recovery_abs(int value)
{
  return value < 0 ? -value : value;
}

static int term_command_recovery_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int term_command_recovery_contains(const char *text, const char *needle)
{
  if (text == 0 || needle == 0 || needle[0] == '\0')
    return 0;
  return strstr(text, needle) != 0;
}

static int term_command_recovery_starts_with(const char *text, const char *prefix)
{
  int i;

  if (text == 0 || prefix == 0)
    return 0;
  for (i = 0; prefix[i] != '\0'; i++) {
    if (text[i] != prefix[i])
      return 0;
  }
  return 1;
}

static int term_command_recovery_token_match(const char *left, const char *right)
{
  if (left == 0 || right == 0)
    return 0;
  return strcmp(left, right) == 0;
}

static int term_command_recovery_path_join(char *dst, int cap,
                                           const char *left, const char *right)
{
  int len = 0;
  int left_len;

  if (dst == 0 || cap <= 0 || left == 0 || right == 0)
    return -1;
  dst[0] = '\0';
  left_len = (int)strlen(left);
  len = term_command_recovery_append_text(dst, cap, len, left);
  if (len < 0)
    return -1;
  if (left_len > 0 && left[left_len - 1] != '/') {
    len = term_command_recovery_append_char(dst, cap, len, '/');
    if (len < 0)
      return -1;
  }
  return term_command_recovery_append_text(dst, cap, len, right);
}

static int term_command_recovery_current_dir(char *out, int cap)
{
#ifdef TEST_BUILD
  return getcwd(out, (size_t)cap) != 0;
#else
  ext3_dentry *stack[64];
  ext3_dentry *dentry;
  int depth = 0;
  int len = 0;
  int i;

  if (out == 0 || cap <= 0)
    return 0;
  out[0] = '\0';
  dentry = (ext3_dentry *)getdentry();
  while (dentry != 0 && depth < 64) {
    stack[depth++] = dentry;
    if (dentry->d_parent == 0 || dentry->d_parent == dentry)
      break;
    dentry = dentry->d_parent;
  }
  if (depth <= 0) {
    term_command_recovery_copy_text(out, cap, "/");
    return 1;
  }
  for (i = depth - 1; i >= 0; i--) {
    int name_len;

    if (stack[i]->d_namelen == 1 && stack[i]->d_name[0] == '/') {
      if (len == 0)
        len = term_command_recovery_append_char(out, cap, len, '/');
      continue;
    }
    if (len == 0)
      len = term_command_recovery_append_char(out, cap, len, '/');
    else if (out[len - 1] != '/')
      len = term_command_recovery_append_char(out, cap, len, '/');
    if (len < 0)
      return 0;
    name_len = stack[i]->d_namelen;
    if (name_len >= cap - len)
      name_len = cap - len - 1;
    if (name_len <= 0)
      break;
    memcpy(out + len, stack[i]->d_name, (size_t)name_len);
    len += name_len;
    out[len] = '\0';
  }
  if (len == 0)
    term_command_recovery_copy_text(out, cap, "/");
  return 1;
#endif
}

static int term_command_recovery_path_exists(const char *path)
{
#ifdef TEST_BUILD
  struct stat st;

  if (path == 0 || path[0] == '\0')
    return 0;
  return stat(path, &st) == 0;
#else
  int fd;

  if (path == 0 || path[0] == '\0')
    return 0;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return 0;
  close(fd);
  return 1;
#endif
}

static int term_command_recovery_common_prefix(const char *left, const char *right)
{
  int count = 0;

  if (left == 0 || right == 0)
    return 0;
  while (left[count] != '\0' && right[count] != '\0' &&
         left[count] == right[count]) {
    count++;
  }
  return count;
}

static int term_command_recovery_transposed(const char *typed, const char *candidate)
{
  int len;
  int i;
  int first = -1;
  int second = -1;

  if (typed == 0 || candidate == 0)
    return 0;
  len = (int)strlen(typed);
  if (len != (int)strlen(candidate) || len < 2)
    return 0;
  for (i = 0; i < len; i++) {
    if (typed[i] == candidate[i])
      continue;
    if (first < 0) {
      first = i;
      continue;
    }
    if (second < 0) {
      second = i;
      continue;
    }
    if (second >= 0)
      return 0;
  }
  if (first < 0 || second < 0 || second != first + 1)
    return 0;
  return typed[first] == candidate[second] &&
         typed[second] == candidate[first];
}

static int term_command_recovery_edit_distance(const char *left, const char *right)
{
  int prev[TERM_COMMAND_RECOVERY_NAME_MAX];
  int next[TERM_COMMAND_RECOVERY_NAME_MAX];
  int left_len;
  int right_len;
  int i;
  int j;

  if (left == 0 || right == 0)
    return 32;
  left_len = (int)strlen(left);
  right_len = (int)strlen(right);
  if (left_len >= TERM_COMMAND_RECOVERY_NAME_MAX ||
      right_len >= TERM_COMMAND_RECOVERY_NAME_MAX)
    return 32;
  for (j = 0; j <= right_len; j++)
    prev[j] = j;
  for (i = 1; i <= left_len; i++) {
    next[0] = i;
    for (j = 1; j <= right_len; j++) {
      int cost = left[i - 1] == right[j - 1] ? 0 : 1;
      int best = prev[j] + 1;

      if (next[j - 1] + 1 < best)
        best = next[j - 1] + 1;
      if (prev[j - 1] + cost < best)
        best = prev[j - 1] + cost;
      next[j] = best;
    }
    for (j = 0; j <= right_len; j++)
      prev[j] = next[j];
  }
  return prev[right_len];
}

static int term_command_recovery_find_token(const char *text, int index,
                                            int *start_out, int *len_out)
{
  int i = 0;
  int count = 0;

  if (start_out == 0 || len_out == 0 || text == 0 || index < 0)
    return 0;

  while (text[i] != '\0') {
    char quote = '\0';
    int start;

    while (term_command_recovery_is_space(text[i]))
      i++;
    if (text[i] == '\0')
      break;
    start = i;
    while (text[i] != '\0') {
      char ch = text[i];

      if (quote != '\0') {
        if (ch == quote)
          quote = '\0';
        else if (ch == '\\' && text[i + 1] != '\0')
          i++;
        i++;
        continue;
      }
      if (ch == '\'' || ch == '"') {
        quote = ch;
        i++;
        continue;
      }
      if (ch == '\\' && text[i + 1] != '\0') {
        i += 2;
        continue;
      }
      if (term_command_recovery_is_space(ch))
        break;
      i++;
    }
    if (count == index) {
      *start_out = start;
      *len_out = i - start;
      return 1;
    }
    count++;
  }
  return 0;
}

static void term_command_recovery_token_text(const char *text, int start, int len,
                                             char *out, int cap)
{
  int copy_len;

  if (out == 0 || cap <= 0) {
    return;
  }
  out[0] = '\0';
  if (text == 0 || start < 0 || len <= 0)
    return;
  copy_len = len;
  if (copy_len >= cap)
    copy_len = cap - 1;
  memcpy(out, text + start, (size_t)copy_len);
  out[copy_len] = '\0';
  if (copy_len >= 2 &&
      ((out[0] == '"' && out[copy_len - 1] == '"') ||
       (out[0] == '\'' && out[copy_len - 1] == '\''))) {
    memmove(out, out + 1, (size_t)(copy_len - 2));
    out[copy_len - 2] = '\0';
  }
}

static int term_command_recovery_replace_span(char *out, int cap,
                                              const char *text,
                                              int start, int len,
                                              const char *replacement)
{
  int out_len = 0;
  int i;

  if (out == 0 || cap <= 0 || text == 0 || replacement == 0)
    return -1;
  out[0] = '\0';
  for (i = 0; i < start; i++) {
    out_len = term_command_recovery_append_char(out, cap, out_len, text[i]);
    if (out_len < 0)
      return -1;
  }
  out_len = term_command_recovery_append_text(out, cap, out_len, replacement);
  if (out_len < 0)
    return -1;
  for (i = start + len; text[i] != '\0'; i++) {
    out_len = term_command_recovery_append_char(out, cap, out_len, text[i]);
    if (out_len < 0)
      return -1;
  }
  return 0;
}

static int term_command_recovery_command_is_destructive(const char *command_text)
{
  char first[TERM_COMMAND_RECOVERY_NAME_MAX];
  char second[TERM_COMMAND_RECOVERY_NAME_MAX];
  int start;
  int len;

  first[0] = '\0';
  second[0] = '\0';
  if (command_text == 0)
    return 0;
  if (term_command_recovery_find_token(command_text, 0, &start, &len) != 0)
    term_command_recovery_token_text(command_text, start, len,
                                     first, sizeof(first));
  if (term_command_recovery_find_token(command_text, 1, &start, &len) != 0)
    term_command_recovery_token_text(command_text, start, len,
                                     second, sizeof(second));
  if (term_command_recovery_token_match(first, "rm"))
    return 1;
  if (term_command_recovery_token_match(first, "mv"))
    return 1;
  if (term_command_recovery_token_match(first, "dd"))
    return 1;
  if (term_command_recovery_token_match(first, "kill"))
    return 1;
  if (term_command_recovery_token_match(first, "git") &&
      term_command_recovery_token_match(second, "push"))
    return 1;
  return 0;
}

static int term_command_recovery_candidate_score(const char *typed,
                                                 const char *candidate,
                                                 int history_bonus,
                                                 int *distance_out,
                                                 int *transposed_out)
{
  int distance;
  int transposed;
  int prefix;
  int len_diff;
  int score;

  if (typed == 0 || candidate == 0 || typed[0] == '\0' || candidate[0] == '\0')
    return -1;
  distance = term_command_recovery_edit_distance(typed, candidate);
  transposed = term_command_recovery_transposed(typed, candidate);
  prefix = term_command_recovery_common_prefix(typed, candidate);
  len_diff = term_command_recovery_abs((int)strlen(typed) - (int)strlen(candidate));
  if (distance > 2 && transposed == 0 && prefix < 2)
    return -1;
  score = 200 - distance * 40 - len_diff * 6 + prefix * 8 + history_bonus;
  if (transposed != 0)
    score += 28;
  if (term_command_recovery_starts_with(candidate, typed) != 0)
    score += 10;
  if (distance_out != 0)
    *distance_out = distance;
  if (transposed_out != 0)
    *transposed_out = transposed;
  return score;
}

static void term_command_recovery_add_candidate(struct term_command_candidate *candidates,
                                                int *count, int cap,
                                                const char *typed,
                                                const char *token,
                                                const char *replacement,
                                                int history_bonus)
{
  int score;
  int distance = 0;
  int transposed = 0;
  int i;

  if (candidates == 0 || count == 0 || typed == 0 || token == 0 ||
      replacement == 0 || token[0] == '\0' || replacement[0] == '\0')
    return;
  score = term_command_recovery_candidate_score(typed, token, history_bonus,
                                                &distance, &transposed);
  if (score < 0)
    return;
  for (i = 0; i < *count; i++) {
    if (strcmp(candidates[i].replacement, replacement) == 0) {
      if (score > candidates[i].score) {
        candidates[i].score = score;
        candidates[i].distance = distance;
        candidates[i].transposed = transposed;
        term_command_recovery_copy_text(candidates[i].token,
                                        sizeof(candidates[i].token), token);
      }
      return;
    }
  }
  if (*count >= cap)
    return;
  term_command_recovery_copy_text(candidates[*count].token,
                                  sizeof(candidates[*count].token), token);
  term_command_recovery_copy_text(candidates[*count].replacement,
                                  sizeof(candidates[*count].replacement), replacement);
  candidates[*count].score = score;
  candidates[*count].distance = distance;
  candidates[*count].transposed = transposed;
  (*count)++;
}

static void term_command_recovery_collect_path_dir(struct term_command_candidate *candidates,
                                                   int *count, int cap,
                                                   const char *typed,
                                                   const char *dir_path,
                                                   const char *replacement_prefix)
{
#ifdef TEST_BUILD
  DIR *dirp;
  struct dirent *de;
#else
  int fd;
  char dir_buf[TERM_COMMAND_RECOVERY_DIR_BUF_SIZE];
  int bytes_read;
  int offset = 0;
#endif

  if (dir_path == 0 || dir_path[0] == '\0')
    return;
#ifdef TEST_BUILD
  dirp = opendir(dir_path);
  if (dirp == 0)
    return;
  while ((de = readdir(dirp)) != 0) {
    char replacement[TERM_COMMAND_RECOVERY_NAME_MAX];

    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (typed[0] != '.' && de->d_name[0] == '.')
      continue;
    replacement[0] = '\0';
    if (replacement_prefix != 0)
      term_command_recovery_copy_text(replacement, sizeof(replacement),
                                      replacement_prefix);
    term_command_recovery_append_text(replacement, sizeof(replacement),
                                      (int)strlen(replacement), de->d_name);
    term_command_recovery_add_candidate(candidates, count, cap,
                                        typed, de->d_name, replacement, 0);
  }
  closedir(dirp);
#else
  fd = open(dir_path, O_RDONLY, 0);
  if (fd < 0)
    return;
  bytes_read = (int)read(fd, dir_buf, sizeof(dir_buf));
  close(fd);
  if (bytes_read <= 0)
    return;
  while (offset < bytes_read) {
    struct term_command_recovery_dir_entry *de =
        (struct term_command_recovery_dir_entry *)(dir_buf + offset);
    char name[256];
    int name_len;
    char replacement[TERM_COMMAND_RECOVERY_NAME_MAX];

    if (de->rec_len == 0 || de->rec_len < 8)
      break;
    if (offset + de->rec_len > bytes_read)
      break;
    if (de->inode == 0 || de->name_len == 0) {
      offset += de->rec_len;
      continue;
    }
    name_len = de->name_len;
    if (name_len >= (int)sizeof(name))
      name_len = sizeof(name) - 1;
    memcpy(name, de->name, (size_t)name_len);
    name[name_len] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      offset += de->rec_len;
      continue;
    }
    if (typed[0] != '.' && name[0] == '.') {
      offset += de->rec_len;
      continue;
    }
    replacement[0] = '\0';
    if (replacement_prefix != 0)
      term_command_recovery_copy_text(replacement, sizeof(replacement),
                                      replacement_prefix);
    term_command_recovery_append_text(replacement, sizeof(replacement),
                                      (int)strlen(replacement), name);
    term_command_recovery_add_candidate(candidates, count, cap,
                                        typed, name, replacement, 0);
    offset += de->rec_len;
  }
#endif
}

static void term_command_recovery_collect_command_candidates(const struct shell_state *state,
                                                             const char *typed,
                                                             struct term_command_candidate *candidates,
                                                             int *count, int cap)
{
  const char *path_env;
  const char *segment;
  int i;

  if (state == 0 || typed == 0 || typed[0] == '\0' || candidates == 0 || count == 0)
    return;

  for (i = 0; i < (int)(sizeof(g_term_command_builtins) / sizeof(g_term_command_builtins[0])); i++) {
    term_command_recovery_add_candidate(candidates, count, cap,
                                        typed, g_term_command_builtins[i],
                                        g_term_command_builtins[i], 0);
  }

  for (i = 0; i < state->alias_count; i++) {
    term_command_recovery_add_candidate(candidates, count, cap,
                                        typed, state->aliases[i].name,
                                        state->aliases[i].name, 0);
  }

  for (i = 0; i < shell_history_count(state); i++) {
    const char *entry = shell_history_get(state, i);
    int start;
    int len;
    char token[TERM_COMMAND_RECOVERY_NAME_MAX];

    if (entry == 0)
      continue;
    if (term_command_recovery_find_token(entry, 0, &start, &len) == 0)
      continue;
    term_command_recovery_token_text(entry, start, len, token, sizeof(token));
    if (strchr(token, '/') != 0)
      continue;
    if (strcmp(token, typed) == 0)
      continue;
    term_command_recovery_add_candidate(candidates, count, cap,
                                        typed, token, token, 18);
  }

  path_env = shell_var_get(state, "PATH");
  if (path_env == 0 || path_env[0] == '\0')
    path_env = "/usr/bin";
  segment = path_env;
  while (1) {
    char dir_path[TERM_COMMAND_RECOVERY_PATH_MAX];
    int seg_len = 0;

    while (segment[seg_len] != '\0' && segment[seg_len] != ':')
      seg_len++;
    if (seg_len <= 0) {
      term_command_recovery_copy_text(dir_path, sizeof(dir_path), ".");
    } else {
      if (seg_len >= (int)sizeof(dir_path))
        seg_len = sizeof(dir_path) - 1;
      memcpy(dir_path, segment, (size_t)seg_len);
      dir_path[seg_len] = '\0';
    }
    term_command_recovery_collect_path_dir(candidates, count, cap,
                                           typed, dir_path, "");
    if (segment[seg_len] == '\0')
      break;
    segment += seg_len + 1;
  }

  term_command_recovery_collect_path_dir(candidates, count, cap,
                                         typed, ".", "./");
}

static int term_command_recovery_best_candidate(const struct term_command_candidate *candidates,
                                                int count)
{
  int best = -1;
  int i;

  for (i = 0; i < count; i++) {
    if (best < 0 || candidates[i].score > candidates[best].score)
      best = i;
  }
  return best;
}

static int term_command_recovery_build_command_typo(const struct shell_state *state,
                                                    const char *command_text,
                                                    int status,
                                                    struct term_command_recovery_result *result)
{
  struct term_command_candidate candidates[TERM_COMMAND_RECOVERY_CANDIDATE_MAX];
  char typed[TERM_COMMAND_RECOVERY_NAME_MAX];
  int start;
  int len;
  int count = 0;
  int best;

  if (state == 0 || command_text == 0 || result == 0)
    return 0;
  if (status != 127 &&
      term_command_recovery_contains(shell_state_last_error(state),
                                     "command not found") == 0)
    return 0;
  if (term_command_recovery_find_token(command_text, 0, &start, &len) == 0)
    return 0;
  term_command_recovery_token_text(command_text, start, len, typed, sizeof(typed));
  if (typed[0] == '\0' || strchr(typed, '/') != 0)
    return 0;

  term_command_recovery_collect_command_candidates(state, typed, candidates,
                                                   &count,
                                                   TERM_COMMAND_RECOVERY_CANDIDATE_MAX);
  best = term_command_recovery_best_candidate(candidates, count);
  if (best < 0)
    return 0;
  if (candidates[best].score < 120)
    return 0;
  if (term_command_recovery_replace_span(result->replacement,
                                         sizeof(result->replacement),
                                         command_text, start, len,
                                         candidates[best].replacement) < 0)
    return 0;
  term_command_recovery_copy_text(result->display, sizeof(result->display),
                                  result->replacement);
  term_command_recovery_copy_text(result->reason, sizeof(result->reason),
                                  "command-typo");
  result->kind = TERM_COMMAND_RECOVERY_SUGGEST;
  result->destructive =
      term_command_recovery_command_is_destructive(result->replacement);
  result->auto_apply = result->destructive == 0 &&
                       (candidates[best].distance <= 1 ||
                        candidates[best].transposed != 0);
  return 1;
}

static int term_command_recovery_next_segment(const char *path, int *offset,
                                              char *segment, int cap)
{
  int index;
  int len = 0;

  if (path == 0 || offset == 0 || segment == 0 || cap <= 0)
    return 0;
  index = *offset;
  while (path[index] == '/')
    index++;
  if (path[index] == '\0') {
    *offset = index;
    segment[0] = '\0';
    return 0;
  }
  while (path[index] != '\0' && path[index] != '/' && len < cap - 1)
    segment[len++] = path[index++];
  segment[len] = '\0';
  while (path[index] != '\0' && path[index] != '/')
    index++;
  *offset = index;
  return 1;
}

static int term_command_recovery_append_segment(char *path, int cap,
                                                const char *segment)
{
  int len;

  if (path == 0 || cap <= 0 || segment == 0)
    return -1;
  len = (int)strlen(path);
  if (len > 0 && path[len - 1] != '/')
    len = term_command_recovery_append_char(path, cap, len, '/');
  if (len < 0)
    return -1;
  return term_command_recovery_append_text(path, cap, len, segment);
}

static int term_command_recovery_correct_path_component(const char *base_dir,
                                                        const char *typed,
                                                        char *out, int cap)
{
#ifdef TEST_BUILD
  DIR *dirp;
  struct dirent *de;
#else
  int fd;
  char dir_buf[TERM_COMMAND_RECOVERY_DIR_BUF_SIZE];
  int bytes_read;
  int offset = 0;
#endif
  char best_name[TERM_COMMAND_RECOVERY_NAME_MAX];
  int best_score = -1;
  int best_distance = 0;
  int best_transposed = 0;

  if (base_dir == 0 || typed == 0 || out == 0 || cap <= 0)
    return 0;
#ifdef TEST_BUILD
  dirp = opendir(base_dir);
  if (dirp == 0)
    return 0;
  best_name[0] = '\0';
  while ((de = readdir(dirp)) != 0) {
    int distance = 0;
    int transposed = 0;
    int score;

    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (typed[0] != '.' && de->d_name[0] == '.')
      continue;
    score = term_command_recovery_candidate_score(typed, de->d_name, 0,
                                                  &distance, &transposed);
    if (score > best_score) {
      best_score = score;
      best_distance = distance;
      best_transposed = transposed;
      term_command_recovery_copy_text(best_name, sizeof(best_name), de->d_name);
    }
  }
  closedir(dirp);
#else
  fd = open(base_dir, O_RDONLY, 0);
  if (fd < 0)
    return 0;
  bytes_read = (int)read(fd, dir_buf, sizeof(dir_buf));
  close(fd);
  if (bytes_read <= 0)
    return 0;
  while (offset < bytes_read) {
    struct term_command_recovery_dir_entry *de =
        (struct term_command_recovery_dir_entry *)(dir_buf + offset);
    char name[256];
    int name_len;
    int distance = 0;
    int transposed = 0;
    int score;

    if (de->rec_len == 0 || de->rec_len < 8)
      break;
    if (offset + de->rec_len > bytes_read)
      break;
    if (de->inode == 0 || de->name_len == 0) {
      offset += de->rec_len;
      continue;
    }
    name_len = de->name_len;
    if (name_len >= (int)sizeof(name))
      name_len = sizeof(name) - 1;
    memcpy(name, de->name, (size_t)name_len);
    name[name_len] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      offset += de->rec_len;
      continue;
    }
    if (typed[0] != '.' && name[0] == '.') {
      offset += de->rec_len;
      continue;
    }
    score = term_command_recovery_candidate_score(typed, name, 0,
                                                  &distance, &transposed);
    if (score > best_score) {
      best_score = score;
      best_distance = distance;
      best_transposed = transposed;
      term_command_recovery_copy_text(best_name, sizeof(best_name), name);
    }
    offset += de->rec_len;
  }
#endif
  if (best_score < 120)
    return 0;
  if (best_distance > 1 && best_transposed == 0 &&
      term_command_recovery_common_prefix(typed, best_name) < 2)
    return 0;
  term_command_recovery_copy_text(out, cap, best_name);
  return 1;
}

static int term_command_recovery_correct_path(const char *input_path,
                                              char *out, int cap)
{
  char current[TERM_COMMAND_RECOVERY_PATH_MAX];
  char display[TERM_COMMAND_RECOVERY_PATH_MAX];
  char segment[TERM_COMMAND_RECOVERY_NAME_MAX];
  char corrected[TERM_COMMAND_RECOVERY_NAME_MAX];
  const char *scan_path;
  int changed = 0;
  int offset = 0;

  if (input_path == 0 || out == 0 || cap <= 0 || input_path[0] == '\0')
    return 0;

  if (input_path[0] == '/') {
    term_command_recovery_copy_text(current, sizeof(current), "/");
    term_command_recovery_copy_text(display, sizeof(display), "/");
    scan_path = input_path;
  } else {
    if (term_command_recovery_current_dir(current, sizeof(current)) == 0)
      return 0;
    term_command_recovery_copy_text(display, sizeof(display), "");
    scan_path = input_path;
  }

  while (term_command_recovery_next_segment(scan_path, &offset,
                                            segment, sizeof(segment)) != 0) {
    char candidate_path[TERM_COMMAND_RECOVERY_PATH_MAX];

    if (segment[0] == '\0' || strcmp(segment, ".") == 0)
      continue;
    if (strcmp(segment, "..") == 0)
      return 0;
    term_command_recovery_copy_text(corrected, sizeof(corrected), segment);
    if (term_command_recovery_path_join(candidate_path, sizeof(candidate_path),
                                        current, segment) < 0)
      return 0;
    if (term_command_recovery_path_exists(candidate_path) == 0) {
      if (term_command_recovery_correct_path_component(current, segment,
                                                       corrected,
                                                       sizeof(corrected)) == 0)
        return 0;
      changed = 1;
      if (term_command_recovery_path_join(candidate_path, sizeof(candidate_path),
                                          current, corrected) < 0)
        return 0;
      if (term_command_recovery_path_exists(candidate_path) == 0)
        return 0;
    }
    if (strcmp(display, "/") == 0)
      term_command_recovery_copy_text(display, sizeof(display), "");
    if (display[0] == '\0')
      term_command_recovery_copy_text(display, sizeof(display), corrected);
    else if (term_command_recovery_append_segment(display, sizeof(display),
                                                  corrected) < 0)
      return 0;
    term_command_recovery_copy_text(current, sizeof(current), candidate_path);
  }

  if (changed == 0 || term_command_recovery_path_exists(current) == 0)
    return 0;
  if (input_path[0] == '/')
    snprintf(out, (size_t)cap, "/%s", display);
  else
    term_command_recovery_copy_text(out, cap, display);
  return out[0] != '\0';
}

static int term_command_recovery_build_path_fix(const struct shell_state *state,
                                                const char *command_text,
                                                struct term_command_recovery_result *result)
{
  char command[TERM_COMMAND_RECOVERY_NAME_MAX];
  char path[TERM_COMMAND_RECOVERY_PATH_MAX];
  char corrected[TERM_COMMAND_RECOVERY_PATH_MAX];
  int command_start;
  int command_len;
  int path_start;
  int path_len;

  (void)state;
  if (command_text == 0 || result == 0)
    return 0;
  if (term_command_recovery_find_token(command_text, 0,
                                       &command_start, &command_len) == 0)
    return 0;
  term_command_recovery_token_text(command_text, command_start, command_len,
                                   command, sizeof(command));
  if (term_command_recovery_token_match(command, "cd") == 0 &&
      term_command_recovery_token_match(command, "cat") == 0 &&
      term_command_recovery_token_match(command, "ls") == 0 &&
      term_command_recovery_token_match(command, "vi") == 0 &&
      term_command_recovery_token_match(command, "grep") == 0)
    return 0;
  if (term_command_recovery_find_token(command_text, 1,
                                       &path_start, &path_len) == 0)
    return 0;
  term_command_recovery_token_text(command_text, path_start, path_len,
                                   path, sizeof(path));
  if (path[0] == '\0')
    return 0;
  if (term_command_recovery_correct_path(path, corrected, sizeof(corrected)) == 0)
    return 0;
  if (term_command_recovery_replace_span(result->replacement,
                                         sizeof(result->replacement),
                                         command_text, path_start, path_len,
                                         corrected) < 0)
    return 0;
  term_command_recovery_copy_text(result->display, sizeof(result->display),
                                  result->replacement);
  term_command_recovery_copy_text(result->reason, sizeof(result->reason),
                                  "path");
  result->kind = TERM_COMMAND_RECOVERY_SUGGEST;
  result->destructive =
      term_command_recovery_command_is_destructive(result->replacement);
  result->auto_apply = result->destructive == 0;
  return 1;
}

static int term_command_recovery_build_git_push_hint(const struct shell_state *state,
                                                     const char *command_text,
                                                     struct term_command_recovery_result *result)
{
  const char *error_text;

  if (state == 0 || command_text == 0 || result == 0)
    return 0;
  error_text = shell_state_last_error(state);
  if (term_command_recovery_starts_with(command_text, "git push") == 0)
    return 0;
  if (term_command_recovery_contains(error_text, "upstream") == 0 &&
      term_command_recovery_contains(error_text, "has no upstream") == 0)
    return 0;
  result->kind = TERM_COMMAND_RECOVERY_SUGGEST;
  result->auto_apply = 0;
  result->destructive = 1;
  term_command_recovery_copy_text(result->replacement,
                                  sizeof(result->replacement),
                                  "git push --set-upstream origin main");
  term_command_recovery_copy_text(result->display, sizeof(result->display),
                                  result->replacement);
  term_command_recovery_copy_text(result->reason, sizeof(result->reason),
                                  "git-upstream");
  return 1;
}

static int term_command_recovery_build_permission_hint(const struct shell_state *state,
                                                       struct term_command_recovery_result *result)
{
  const char *error_text;

  if (state == 0 || result == 0)
    return 0;
  error_text = shell_state_last_error(state);
  if (term_command_recovery_contains(error_text, "permission denied") == 0 &&
      term_command_recovery_contains(error_text, "Permission denied") == 0)
    return 0;
  result->kind = TERM_COMMAND_RECOVERY_HINT;
  result->auto_apply = 0;
  result->destructive = 0;
  term_command_recovery_copy_text(result->display, sizeof(result->display),
                                  "権限不足の可能性があります。実行権限や出力先の書き込み権限を確認してください");
  term_command_recovery_copy_text(result->reason, sizeof(result->reason),
                                  "permission");
  return 1;
}

void term_command_recovery_reset(struct term_command_recovery_result *result)
{
  if (result == 0)
    return;
  memset(result, 0, sizeof(*result));
}

int term_command_recovery_build(const struct shell_state *state,
                                const char *command_text,
                                int status,
                                struct term_command_recovery_result *result)
{
  term_command_recovery_reset(result);
  if (state == 0 || command_text == 0 || result == 0 || status == 0)
    return 0;
  if (term_command_recovery_build_git_push_hint(state, command_text, result) != 0)
    return 1;
  if (term_command_recovery_build_command_typo(state, command_text,
                                               status, result) != 0)
    return 1;
  if (term_command_recovery_build_path_fix(state, command_text, result) != 0)
    return 1;
  if (term_command_recovery_build_permission_hint(state, result) != 0)
    return 1;
  return 0;
}

int term_command_recovery_write(const struct term_command_recovery_result *result)
{
  int written = 0;

  if (result == 0 || result->kind == TERM_COMMAND_RECOVERY_NONE)
    return 0;
  if (result->kind == TERM_COMMAND_RECOVERY_SUGGEST)
    written += (int)write(STDOUT_FILENO, "suggest: ", 9);
  else
    written += (int)write(STDOUT_FILENO, "hint: ", 6);
  written += (int)write(STDOUT_FILENO, result->display, strlen(result->display));
  written += (int)write(STDOUT_FILENO, "\n", 1);
  return written;
}
