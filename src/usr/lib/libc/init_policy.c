#include <init_policy.h>
#include <string.h>

static void policy_copy_text(char *dst, int cap, const char *src)
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

static int policy_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' ||
         ch == '\r' || ch == '\f' || ch == '\v';
}

static void policy_trim_copy(char *dst, int cap, const char *src, int len)
{
  int start = 0;
  int end = len;
  int i;
  int out_len = 0;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0 || len <= 0) {
    dst[0] = '\0';
    return;
  }

  while (start < end && policy_is_space(src[start]))
    start++;
  while (end > start && policy_is_space(src[end - 1]))
    end--;

  for (i = start; i < end && out_len < cap - 1; i++)
    dst[out_len++] = src[i];
  dst[out_len] = '\0';
}

static void policy_basename(char *dst, int cap, const char *path)
{
  int i;
  int start = 0;

  if (path == 0) {
    policy_copy_text(dst, cap, "");
    return;
  }

  for (i = 0; path[i] != '\0'; i++) {
    if (path[i] == '/')
      start = i + 1;
  }
  policy_copy_text(dst, cap, path + start);
}

static void policy_parse_tokens(char tokens[][INIT_POLICY_NAME_MAX], int *count,
                                int max_count, const char *text)
{
  int i = 0;

  if (tokens == 0 || count == 0 || text == 0)
    return;

  *count = 0;
  while (text[i] != '\0' && *count < max_count) {
    char value[INIT_POLICY_NAME_MAX];
    int start;

    while (policy_is_space(text[i]))
      i++;
    if (text[i] == '\0')
      break;

    start = i;
    while (text[i] != '\0' && policy_is_space(text[i]) == 0)
      i++;
    policy_trim_copy(value, sizeof(value), text + start, i - start);
    if (value[0] == '\0')
      continue;
    policy_copy_text(tokens[*count], INIT_POLICY_NAME_MAX, value);
    (*count)++;
    while (text[i] != '\0' && policy_is_space(text[i]))
      i++;
    if (text[i] == '\0')
      break;
  }
}

static int policy_line_equals(const char *line, const char *text)
{
  if (line == 0 || text == 0)
    return 0;
  return strcmp(line, text) == 0;
}

static int policy_field_value(const char *line, const char *field,
                              char *out, int out_cap)
{
  int field_len;
  int i;

  if (line == 0 || field == 0 || out == 0 || out_cap <= 0)
    return 0;

  field_len = (int)strlen(field);
  for (i = 0; i < field_len; i++) {
    if (line[i] != field[i])
      return 0;
  }
  policy_trim_copy(out, out_cap, line + field_len,
                   (int)strlen(line + field_len));
  return 1;
}

static int policy_service_provides(const struct init_service_info *service,
                                   const char *name)
{
  int i;

  if (service == 0 || name == 0 || name[0] == '\0')
    return 0;

  for (i = 0; i < service->provide_count; i++) {
    if (strcmp(service->provides[i], name) == 0)
      return 1;
  }
  return strcmp(service->name, name) == 0;
}

static int policy_any_service_provides(const struct init_service_info *services,
                                       int count, const char *runlevel,
                                       const char *name)
{
  int i;

  for (i = 0; i < count; i++) {
    if (init_policy_service_matches_runlevel(&services[i], runlevel) == 0)
      continue;
    if (policy_service_provides(&services[i], name) != 0)
      return 1;
  }
  return 0;
}

static int policy_selected_provide(const struct init_service_info *services,
                                   int *order, int order_count,
                                   const char *name)
{
  int i;

  for (i = 0; i < order_count; i++) {
    if (policy_service_provides(&services[order[i]], name) != 0)
      return 1;
  }
  return 0;
}

void init_service_info_init(struct init_service_info *info)
{
  if (info == 0)
    return;
  memset(info, 0, sizeof(*info));
}

void init_inittab_init(struct init_inittab *inittab)
{
  if (inittab == 0)
    return;

  memset(inittab, 0, sizeof(*inittab));
  policy_copy_text(inittab->runlevel, sizeof(inittab->runlevel), "default");
  policy_copy_text(inittab->sysinit, sizeof(inittab->sysinit), "/etc/init.d/rcS");
  policy_copy_text(inittab->respawns[0].runlevel,
                   sizeof(inittab->respawns[0].runlevel), "default");
  policy_copy_text(inittab->respawns[0].command,
                   sizeof(inittab->respawns[0].command), "/usr/bin/term");
  inittab->respawn_count = 1;
}

int init_policy_parse_service(const char *path, const char *text,
                              struct init_service_info *info)
{
  int index = 0;
  int in_block = 0;
  char basename[INIT_POLICY_NAME_MAX];

  if (text == 0 || info == 0)
    return -1;

  init_service_info_init(info);
  policy_copy_text(info->path, sizeof(info->path), path);
  policy_basename(basename, sizeof(basename), path);
  policy_copy_text(info->name, sizeof(info->name), basename);
  policy_copy_text(info->provides[0], sizeof(info->provides[0]), basename);
  info->provide_count = 1;

  while (text[index] != '\0') {
    char line[256];
    int start = index;
    int len;

    while (text[index] != '\0' && text[index] != '\n')
      index++;
    len = index - start;
    if (text[index] == '\n')
      index++;
    policy_trim_copy(line, sizeof(line), text + start, len);
    if (policy_line_equals(line, "### BEGIN INIT INFO")) {
      in_block = 1;
      continue;
    }
    if (policy_line_equals(line, "### END INIT INFO"))
      break;
    if (in_block == 0 || line[0] != '#')
      continue;

    policy_trim_copy(line, sizeof(line), line + 1, (int)strlen(line + 1));
    if (policy_field_value(line, "Provides:", basename, sizeof(basename)) != 0) {
      policy_parse_tokens(info->provides, &info->provide_count,
                          INIT_POLICY_MAX_TOKENS, basename);
      if (info->provide_count > 0)
        policy_copy_text(info->name, sizeof(info->name), info->provides[0]);
    } else if (policy_field_value(line, "Required-Start:", basename,
                                  sizeof(basename)) != 0) {
      policy_parse_tokens(info->required_start, &info->required_start_count,
                          INIT_POLICY_MAX_TOKENS, basename);
    } else if (policy_field_value(line, "Default-Start:", basename,
                                  sizeof(basename)) != 0) {
      policy_parse_tokens(info->default_start, &info->default_start_count,
                          INIT_POLICY_MAX_TOKENS, basename);
    }
  }

  return 0;
}

int init_policy_service_matches_runlevel(const struct init_service_info *info,
                                         const char *runlevel)
{
  int i;

  if (info == 0 || runlevel == 0 || runlevel[0] == '\0')
    return 0;
  if (info->default_start_count <= 0)
    return 0;

  for (i = 0; i < info->default_start_count; i++) {
    if (strcmp(info->default_start[i], runlevel) == 0)
      return 1;
  }
  return 0;
}

int init_policy_order_services(const struct init_service_info *services,
                               int count, const char *runlevel,
                               int *order, int max_order)
{
  int selected[INIT_POLICY_MAX_SERVICES];
  int order_count = 0;
  int i;

  if (services == 0 || order == 0 || runlevel == 0)
    return -1;
  if (count > INIT_POLICY_MAX_SERVICES)
    count = INIT_POLICY_MAX_SERVICES;

  memset(selected, 0, sizeof(selected));
  while (1) {
    int progress = 0;
    int remaining = 0;

    for (i = 0; i < count; i++) {
      int dep_index;
      int ready = 1;

      if (selected[i] != 0)
        continue;
      if (init_policy_service_matches_runlevel(&services[i], runlevel) == 0)
        continue;

      remaining = 1;
      for (dep_index = 0; dep_index < services[i].required_start_count; dep_index++) {
        const char *dep = services[i].required_start[dep_index];

        if (dep[0] == '$')
          continue;
        if (policy_selected_provide(services, order, order_count, dep) != 0)
          continue;
        if (policy_any_service_provides(services, count, runlevel, dep) != 0) {
          ready = 0;
          break;
        }
      }
      if (ready == 0)
        continue;
      if (order_count >= max_order)
        return order_count;
      order[order_count++] = i;
      selected[i] = 1;
      progress = 1;
    }

    if (remaining == 0)
      break;
    if (progress != 0)
      continue;

    for (i = 0; i < count; i++) {
      if (selected[i] != 0)
        continue;
      if (init_policy_service_matches_runlevel(&services[i], runlevel) == 0)
        continue;
      if (order_count >= max_order)
        return order_count;
      order[order_count++] = i;
      selected[i] = 1;
    }
    break;
  }

  return order_count;
}

int init_policy_parse_inittab(const char *text, struct init_inittab *inittab)
{
  int index = 0;

  if (text == 0 || inittab == 0)
    return -1;

  init_inittab_init(inittab);
  inittab->respawn_count = 0;
  while (text[index] != '\0') {
    char line[256];
    int start = index;
    int len;

    while (text[index] != '\0' && text[index] != '\n')
      index++;
    len = index - start;
    if (text[index] == '\n')
      index++;
    policy_trim_copy(line, sizeof(line), text + start, len);
    if (line[0] == '\0' || line[0] == '#')
      continue;

    if (policy_field_value(line, "initdefault:", inittab->runlevel,
                           sizeof(inittab->runlevel)) != 0) {
      continue;
    }
    if (policy_field_value(line, "sysinit:", inittab->sysinit,
                           sizeof(inittab->sysinit)) != 0) {
      continue;
    }
    if (strncmp(line, "respawn:", 8) == 0) {
      char *rest = line + 8;
      char *sep = strchr(rest, ':');

      if (sep == 0)
        continue;
      *sep = '\0';
      if (inittab->respawn_count >= INIT_POLICY_MAX_RESPAWNS)
        continue;
      policy_trim_copy(inittab->respawns[inittab->respawn_count].runlevel,
                       sizeof(inittab->respawns[inittab->respawn_count].runlevel),
                       rest, (int)strlen(rest));
      policy_trim_copy(inittab->respawns[inittab->respawn_count].command,
                       sizeof(inittab->respawns[inittab->respawn_count].command),
                       sep + 1, (int)strlen(sep + 1));
      inittab->respawn_count++;
    }
  }

  return 0;
}

const char *init_policy_find_respawn(const struct init_inittab *inittab,
                                     const char *runlevel)
{
  int i;

  if (inittab == 0 || runlevel == 0)
    return "";

  for (i = 0; i < inittab->respawn_count; i++) {
    if (strcmp(inittab->respawns[i].runlevel, runlevel) == 0)
      return inittab->respawns[i].command;
  }
  return "";
}
