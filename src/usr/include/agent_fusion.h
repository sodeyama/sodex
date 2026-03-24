#ifndef _USR_AGENT_FUSION_H
#define _USR_AGENT_FUSION_H

#define AGENT_FUSION_MAX_ARGS 16
#define AGENT_FUSION_TEXT_MAX 512

enum agent_fusion_mode {
  AGENT_FUSION_MODE_AUTO = 0,
  AGENT_FUSION_MODE_SHELL = 1,
  AGENT_FUSION_MODE_AGENT = 2
};

int agent_fusion_enabled(int argc, char **argv);
int agent_fusion_mode_from_argv(int argc, char **argv, int fallback_mode);
int agent_fusion_parse_mode_text(const char *text, int *mode_out);
const char *agent_fusion_mode_name(int mode);
int agent_fusion_build_argv(const char *input,
                            char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                            char *argv[AGENT_FUSION_MAX_ARGS + 1]);
int agent_fusion_build_mode_argv(const char *input, int force_agent,
                                 char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                                 char *argv[AGENT_FUSION_MAX_ARGS + 1]);

#endif /* _USR_AGENT_FUSION_H */
