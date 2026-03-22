#include <stdlib.h>
#include <string.h>
#include <fs.h>

extern int chdir(char *path);

static int shell_build_dentry_path(ext3_dentry *dentry, char *buf, int cap)
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

  pos = shell_build_dentry_path(dentry->d_parent, buf, cap);
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

static int test_path_exists(const char *path)
{
  int fd = open(path, O_RDONLY, 0);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static int test_dir_exists(const char *path)
{
  char cwd[512];
  ext3_dentry *dentry;

  if (path == 0 || path[0] == '\0')
    return 0;
  if (strcmp(path, "/") == 0)
    return 1;

  dentry = getdentry();
  if (dentry == 0 || shell_build_dentry_path(dentry, cwd, sizeof(cwd)) < 0)
    return 0;
  if (chdir((char *)path) != 0)
    return 0;
  chdir(cwd);
  return 1;
}

static int test_is_nonempty(const char *text)
{
  return text != 0 && text[0] != '\0';
}

static int test_eval_primary(int argc, char **argv, int *pos)
{
  const char *tok;

  if (*pos >= argc)
    return 0;
  tok = argv[*pos];

  if (strcmp(tok, "!") == 0) {
    (*pos)++;
    return !test_eval_primary(argc, argv, pos);
  }

  if (strcmp(tok, "-f") == 0 || strcmp(tok, "-e") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_path_exists(argv[*pos - 1]);
  }

  if (strcmp(tok, "-d") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_dir_exists(argv[*pos - 1]);
  }

  if (strcmp(tok, "-n") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_is_nonempty(argv[*pos - 1]);
  }

  if (strcmp(tok, "-z") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 1;
    (*pos)++;
    return !test_is_nonempty(argv[*pos - 1]);
  }

  if (*pos + 2 < argc) {
    const char *op = argv[*pos + 1];

    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
      int result = strcmp(argv[*pos], argv[*pos + 2]) == 0;

      *pos += 3;
      return result;
    }
    if (strcmp(op, "!=") == 0) {
      int result = strcmp(argv[*pos], argv[*pos + 2]) != 0;

      *pos += 3;
      return result;
    }
  }

  (*pos)++;
  return test_is_nonempty(tok);
}

int main(int argc, char **argv)
{
  int pos = 1;
  int result;

  if (argc <= 1)
    return 1;

  result = test_eval_primary(argc, argv, &pos);
  if (pos != argc)
    return 2;
  return result ? 0 : 1;
}
