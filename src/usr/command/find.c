#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

#ifdef TEST_BUILD
#include <dirent.h>
#include <sys/stat.h>
#else
struct utt_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};

#define UTT_DIR_BUF_SIZE 4096
#define UTT_FTYPE_FILE 1
#define UTT_FTYPE_DIR 2
#endif

struct utt_find_options {
  int mindepth;
  int maxdepth;
  int has_maxdepth;
  char name_pattern[128];
  char type_filter;
};

static void utt_find_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: find [path ...] [-name pattern] [-type f|d] "
                 "[-mindepth n] [-maxdepth n] [-print]\n");
}

#ifndef TEST_BUILD
static int utt_build_dentry_path(ext3_dentry *dentry, char *buf, int cap)
{
  int pos;
  int name_len;

  if (dentry == 0 || buf == 0 || cap <= 1)
    return -1;
  if (dentry->d_parent == 0 ||
      (dentry->d_namelen == 1 && dentry->d_name[0] == '/')) {
    buf[0] = '/';
    buf[1] = '\0';
    return 1;
  }

  pos = utt_build_dentry_path(dentry->d_parent, buf, cap);
  if (pos < 0)
    return -1;
  if (pos > 1) {
    if (pos >= cap - 1)
      return -1;
    buf[pos++] = '/';
    buf[pos] = '\0';
  }

  name_len = dentry->d_namelen;
  if (name_len <= 0)
    return pos;
  if (pos + name_len >= cap)
    name_len = cap - pos - 1;
  if (name_len <= 0)
    return -1;

  memcpy(buf + pos, dentry->d_name, (size_t)name_len);
  pos += name_len;
  buf[pos] = '\0';
  return pos;
}
#endif

static int utt_is_dir_path(const char *path)
{
#ifdef TEST_BUILD
  struct stat st;

  if (stat(path, &st) < 0)
    return 0;
  return S_ISDIR(st.st_mode);
#else
  char cwd[UTT_PATH_MAX];
  ext3_dentry *dentry;

  if (path == 0 || path[0] == '\0')
    return 0;
  if (strcmp(path, "/") == 0)
    return 1;

  dentry = getdentry();
  if (dentry == 0 || utt_build_dentry_path(dentry, cwd, sizeof(cwd)) < 0)
    return 0;
  if (chdir(path) != 0)
    return 0;
  chdir(cwd);
  return 1;
#endif
}

static int utt_path_basename(const char *path)
{
  int len = utt_strlen_int(path);
  int pos = len - 1;

  while (pos >= 0 && path[pos] == '/')
    pos--;
  while (pos >= 0 && path[pos] != '/')
    pos--;
  return pos + 1;
}

static int utt_find_name_match(const char *path, const struct utt_find_options *opts)
{
  const char *base;

  if (opts->name_pattern[0] == '\0')
    return 1;
  base = path + utt_path_basename(path);
  return utt_wildcard_match(opts->name_pattern, base);
}

static int utt_find_type_match(int is_dir, const struct utt_find_options *opts)
{
  if (opts->type_filter == '\0')
    return 1;
  if (opts->type_filter == 'd')
    return is_dir != 0;
  if (opts->type_filter == 'f')
    return is_dir == 0;
  return 0;
}

static int utt_find_depth_match(int depth, const struct utt_find_options *opts)
{
  if (depth < opts->mindepth)
    return 0;
  if (opts->has_maxdepth != 0 && depth > opts->maxdepth)
    return 0;
  return 1;
}

static int utt_path_join(char *buf, int cap, const char *left, const char *right)
{
  int len = 0;
  int left_len = utt_strlen_int(left);

  if (buf == 0 || cap <= 1 || left == 0 || right == 0)
    return -1;
  buf[0] = '\0';
  if (left_len == 0)
    left = ".";
  if (utt_strlen_int(left) >= cap - 1)
    return -1;
  strcpy(buf, left);
  len = utt_strlen_int(buf);
  if (len > 0 && buf[len - 1] != '/') {
    if (len + 1 >= cap)
      return -1;
    buf[len++] = '/';
    buf[len] = '\0';
  }
  if (len + utt_strlen_int(right) >= cap)
    return -1;
  strcat(buf, right);
  return 0;
}

static int utt_find_walk(const char *path,
                         int depth,
                         const struct utt_find_options *opts);
static int utt_find_walk_known(const char *path,
                               int depth,
                               const struct utt_find_options *opts,
                               int has_is_dir,
                               int is_dir);

static int utt_find_walk_dir(const char *path,
                             int depth,
                             const struct utt_find_options *opts)
{
#ifdef TEST_BUILD
  DIR *dirp;
  struct dirent *de;

  dirp = opendir(path);
  if (dirp == 0)
    return utt_print_error("find", "open failed", path);
  while ((de = readdir(dirp)) != 0) {
    char child[UTT_PATH_MAX];

    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (utt_path_join(child, sizeof(child), path, de->d_name) < 0)
      continue;
    if (utt_find_walk_known(child, depth + 1, opts,
                            1, utt_is_dir_path(child)) != 0) {
      closedir(dirp);
      return 1;
    }
  }
  closedir(dirp);
#else
  struct utt_dir_entry *de;
  int fd;
  char buf[UTT_DIR_BUF_SIZE];
  int bytes_read;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return utt_print_error("find", "open failed", path);

  while ((bytes_read = (int)read(fd, buf, sizeof(buf))) > 0) {
    int offset = 0;

    while (offset < bytes_read) {
      char name[256];
      char child[UTT_PATH_MAX];
      int nlen;

      de = (void *)(buf + offset);
      if (de->rec_len < 8 || offset + de->rec_len > bytes_read)
        break;
      if (de->inode == 0 || de->name_len == 0) {
        offset += de->rec_len;
        continue;
      }
      nlen = (int)de->name_len;
      if (nlen >= (int)sizeof(name))
        nlen = (int)sizeof(name) - 1;
      memcpy(name, de->name, (size_t)nlen);
      name[nlen] = '\0';
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        offset += de->rec_len;
        continue;
      }
      if (utt_path_join(child, sizeof(child), path, name) == 0) {
        if (utt_find_walk_known(child, depth + 1, opts,
                                1, de->file_type == UTT_FTYPE_DIR) != 0) {
          close(fd);
          return 1;
        }
      }
      offset += de->rec_len;
    }
  }
  close(fd);
  if (bytes_read < 0)
    return utt_print_error("find", "open failed", path);
#endif
  return 0;
}

static int utt_find_walk(const char *path,
                         int depth,
                         const struct utt_find_options *opts)
{
  return utt_find_walk_known(path, depth, opts, 0, 0);
}

static int utt_find_walk_known(const char *path,
                               int depth,
                               const struct utt_find_options *opts,
                               int has_is_dir,
                               int is_dir)
{
  if (has_is_dir == 0)
    is_dir = utt_is_dir_path(path);

  if (utt_find_depth_match(depth, opts) &&
      utt_find_name_match(path, opts) &&
      utt_find_type_match(is_dir, opts)) {
    utt_write_text(STDOUT_FILENO, path);
    utt_write_text(STDOUT_FILENO, "\n");
  }

  if (!is_dir)
    return 0;
  if (opts->has_maxdepth != 0 && depth >= opts->maxdepth)
    return 0;
  return utt_find_walk_dir(path, depth, opts);
}

int unix_find_main(int argc, char **argv)
{
  struct utt_find_options opts;
  char *paths[32];
  int path_count = 0;
  int i;

  memset(&opts, 0, sizeof(opts));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (utt_is_help_option(argv[i])) {
      utt_find_print_usage();
      return 0;
    } else if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if ((strcmp(argv[i], "-name") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "name", &value)) {
      const char *pattern = value != 0 ? value : argv[++i];
      strcpy(opts.name_pattern, pattern);
    } else if ((strcmp(argv[i], "-type") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "type", &value)) {
      const char *type = value != 0 ? value : argv[++i];
      opts.type_filter = type[0];
    } else if ((strcmp(argv[i], "-maxdepth") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "maxdepth", &value)) {
      const char *depth = value != 0 ? value : argv[++i];
      opts.maxdepth = atoi(depth);
      opts.has_maxdepth = 1;
    } else if ((strcmp(argv[i], "-mindepth") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "mindepth", &value)) {
      const char *depth = value != 0 ? value : argv[++i];
      opts.mindepth = atoi(depth);
    } else if (strcmp(argv[i], "-print") == 0 ||
               utt_match_long_option(argv[i], "print", 0)) {
      continue;
    } else if (argv[i][0] == '-') {
      return utt_print_error("find", "unsupported option", argv[i]);
    } else {
      if (path_count < (int)(sizeof(paths) / sizeof(paths[0])))
        paths[path_count++] = argv[i];
    }
  }

  for (; i < argc; i++) {
    if (path_count < (int)(sizeof(paths) / sizeof(paths[0])))
      paths[path_count++] = argv[i];
  }

  if (path_count == 0)
    return utt_find_walk(".", 0, &opts);

  for (i = 0; i < path_count; i++) {
    if (utt_find_walk(paths[i], 0, &opts) != 0)
      return 1;
  }
  return 0;
}

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_find_main(argc, argv);
}
#endif
