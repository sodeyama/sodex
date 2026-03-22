#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>

#define LS_DIR_BUF_SIZE 4096
#define LS_PATH_MAX     512

extern int chdir(const char *pathname);

struct ls_dir_entry {
  unsigned int inode;
  unsigned short rec_len;
  unsigned char name_len;
  unsigned char file_type;
  char name[255];
};

static void print_usage(void);
static int build_dentry_path(ext3_dentry *dentry, char *buf, int cap);
static int is_directory_path(const char *path);
static int list_directory(const char *path);
static int print_file_path(const char *path);

int main(int argc, char **argv)
{
  int argi;
  int status = 0;
  int path_count = 0;

  for (argi = 1; argi < argc; argi++) {
    const char *arg = argv[argi];
    int opti;

    if (arg == 0 || arg[0] == '\0')
      continue;
    if (arg[0] != '-' || strcmp(arg, "-") == 0)
      break;
    for (opti = 1; arg[opti] != '\0'; opti++) {
      if (arg[opti] == '1')
        continue;
      print_usage();
      exit(1);
      return 1;
    }
  }

  if (argi >= argc) {
    status = list_directory(".");
    exit(status);
    return status;
  }

  for (; argi < argc; argi++) {
    const char *path = argv[argi];

    if (path == 0 || path[0] == '\0')
      continue;
    path_count++;
    if (is_directory_path(path) != 0) {
      if (list_directory(path) != 0)
        status = 1;
      continue;
    }
    if (print_file_path(path) != 0)
      status = 1;
  }

  if (path_count == 0)
    status = list_directory(".");

  exit(status);
  return status;
}

static void print_usage(void)
{
  printf("usage: ls [-1] [path ...]\n");
}

static int build_dentry_path(ext3_dentry *dentry, char *buf, int cap)
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

  pos = build_dentry_path(dentry->d_parent, buf, cap);
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

static int is_directory_path(const char *path)
{
  char cwd[LS_PATH_MAX];
  ext3_dentry *dentry;

  if (path == 0 || path[0] == '\0')
    return 0;
  if (strcmp(path, "/") == 0)
    return 1;

  dentry = getdentry();
  if (dentry == 0 || build_dentry_path(dentry, cwd, sizeof(cwd)) < 0)
    return 0;
  if (chdir((char *)path) != 0)
    return 0;
  chdir(cwd);
  return 1;
}

static int list_directory(const char *path)
{
  char buf[LS_DIR_BUF_SIZE];
  int fd;
  int bytes_read;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    printf("ls: cannot access %s\n", path);
    return 1;
  }

  while ((bytes_read = (int)read(fd, buf, sizeof(buf))) > 0) {
    int offset = 0;

    while (offset < bytes_read) {
      struct ls_dir_entry *de = (struct ls_dir_entry *)(buf + offset);
      char name[256];
      int name_len;

      if (de->rec_len < 8 || offset + de->rec_len > bytes_read)
        break;
      if (de->inode == 0 || de->name_len == 0) {
        offset += de->rec_len;
        continue;
      }

      name_len = (int)de->name_len;
      if (name_len >= (int)sizeof(name))
        name_len = sizeof(name) - 1;
      memcpy(name, de->name, (size_t)name_len);
      name[name_len] = '\0';

      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        offset += de->rec_len;
        continue;
      }

      printf("%s\n", name);
      offset += de->rec_len;
    }
  }

  close(fd);
  if (bytes_read < 0) {
    printf("ls: cannot access %s\n", path);
    return 1;
  }
  return 0;
}

static int print_file_path(const char *path)
{
  int fd;

  if (path == 0 || path[0] == '\0')
    return 1;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    printf("ls: cannot access %s\n", path);
    return 1;
  }
  close(fd);
  printf("%s\n", path);
  return 0;
}
