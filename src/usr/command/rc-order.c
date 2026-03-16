#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>
#include <init_policy.h>

static void copy_text(char *dst, int cap, const char *src)
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

static char *read_fd_all(int fd)
{
  char *buf;
  int cap = 256;
  int len = 0;

  buf = (char *)malloc((size_t)cap);
  if (buf == 0)
    return 0;

  while (1) {
    int read_len;

    if (len >= cap - 1) {
      char *next;
      int next_cap = cap * 2;

      next = (char *)malloc((size_t)next_cap);
      if (next == 0) {
        free(buf);
        return 0;
      }
      memset(next, 0, (size_t)next_cap);
      memcpy(next, buf, (size_t)len);
      free(buf);
      buf = next;
      cap = next_cap;
    }

    read_len = (int)read(fd, buf + len, (size_t)(cap - len - 1));
    if (read_len <= 0)
      break;
    len += read_len;
  }

  buf[len] = '\0';
  return buf;
}

static int read_text_file(const char *path, char **out_text)
{
  int fd;

  if (path == 0 || out_text == 0)
    return -1;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  *out_text = read_fd_all(fd);
  close(fd);
  return *out_text == 0 ? -1 : 0;
}

static int should_skip_service(const char *name)
{
  if (name == 0 || name[0] == '\0' || name[0] == '.')
    return 1;
  if (strcmp(name, "rcS") == 0)
    return 1;
  if (strcmp(name, "rc.common") == 0)
    return 1;
  return 0;
}

static void sort_services(struct init_service_info *services, int count)
{
  int i;
  int j;

  for (i = 0; i < count; i++) {
    for (j = i + 1; j < count; j++) {
      if (strcmp(services[i].name, services[j].name) > 0) {
        struct init_service_info tmp = services[i];

        services[i] = services[j];
        services[j] = tmp;
      }
    }
  }
}

static int scan_services(struct init_service_info *services, int max_services)
{
  ext3_dentry *dentry;
  struct dlist_set *plist;
  int count = 0;

  if (chdir("/etc/init.d") < 0)
    return 0;
  dentry = (ext3_dentry *)getdentry();
  if (dentry == 0) {
    chdir("/");
    return 0;
  }

  plist = (&(dentry->d_subdirs))->prev;
  while (1) {
    ext3_dentry *entry = dlist_entry(plist, ext3_dentry, d_child);

    if (entry->d_filetype == FTYPE_FILE &&
        should_skip_service(entry->d_name) == 0 &&
        count < max_services) {
      char path[INIT_POLICY_PATH_MAX];
      char *text = 0;

      copy_text(path, sizeof(path), "/etc/init.d/");
      copy_text(path + strlen(path), (int)(sizeof(path) - strlen(path)),
                entry->d_name);
      if (read_text_file(path, &text) == 0) {
        init_policy_parse_service(path, text, &services[count]);
        count++;
        free(text);
      }
    }

    plist = plist->prev;
    if (plist == &(dentry->d_subdirs))
      break;
  }

  chdir("/");
  sort_services(services, count);
  return count;
}

static int run_service_action(const struct init_service_info *service,
                              const char *action)
{
  char *argv[4];
  int status = 0;
  pid_t pid;

  argv[0] = "sh";
  argv[1] = (char *)service->path;
  argv[2] = (char *)action;
  argv[3] = 0;
  pid = execve("/usr/bin/sh", argv, 0);
  if (pid < 0)
    return 1;
  if (waitpid(pid, &status, 0) < 0)
    return 1;
  return status;
}

int main(int argc, char **argv)
{
  struct init_service_info services[INIT_POLICY_MAX_SERVICES];
  int order[INIT_POLICY_MAX_SERVICES];
  const char *runlevel;
  const char *action;
  int service_count;
  int order_count;
  int i;
  int status = 0;

  if (argc < 2) {
    printf("usage: rc-order <runlevel> [action]\n");
    exit(1);
    return 1;
  }

  runlevel = argv[1];
  action = argc >= 3 ? argv[2] : "start";
  service_count = scan_services(services, INIT_POLICY_MAX_SERVICES);
  order_count = init_policy_order_services(services, service_count, runlevel,
                                           order, INIT_POLICY_MAX_SERVICES);
  if (order_count < 0) {
    exit(1);
    return 1;
  }

  for (i = 0; i < order_count; i++) {
    int current = run_service_action(&services[order[i]], action);

    if (current != 0)
      status = current;
  }

  exit(status);
  return status;
}
