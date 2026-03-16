#ifndef _USR_INIT_POLICY_H
#define _USR_INIT_POLICY_H

#define INIT_POLICY_NAME_MAX 32
#define INIT_POLICY_PATH_MAX 128
#define INIT_POLICY_CMD_MAX 128
#define INIT_POLICY_MAX_TOKENS 8
#define INIT_POLICY_MAX_SERVICES 16
#define INIT_POLICY_MAX_RESPAWNS 4

struct init_service_info {
  char name[INIT_POLICY_NAME_MAX];
  char path[INIT_POLICY_PATH_MAX];
  char provides[INIT_POLICY_MAX_TOKENS][INIT_POLICY_NAME_MAX];
  int provide_count;
  char required_start[INIT_POLICY_MAX_TOKENS][INIT_POLICY_NAME_MAX];
  int required_start_count;
  char default_start[INIT_POLICY_MAX_TOKENS][INIT_POLICY_NAME_MAX];
  int default_start_count;
};

struct init_respawn_rule {
  char runlevel[INIT_POLICY_NAME_MAX];
  char command[INIT_POLICY_CMD_MAX];
};

struct init_inittab {
  char runlevel[INIT_POLICY_NAME_MAX];
  char sysinit[INIT_POLICY_PATH_MAX];
  struct init_respawn_rule respawns[INIT_POLICY_MAX_RESPAWNS];
  int respawn_count;
};

void init_service_info_init(struct init_service_info *info);
void init_inittab_init(struct init_inittab *inittab);
int init_policy_parse_service(const char *path, const char *text,
                              struct init_service_info *info);
int init_policy_service_matches_runlevel(const struct init_service_info *info,
                                         const char *runlevel);
int init_policy_order_services(const struct init_service_info *services,
                               int count, const char *runlevel,
                               int *order, int max_order);
int init_policy_parse_inittab(const char *text, struct init_inittab *inittab);
const char *init_policy_find_respawn(const struct init_inittab *inittab,
                                     const char *runlevel);

#endif /* _USR_INIT_POLICY_H */
