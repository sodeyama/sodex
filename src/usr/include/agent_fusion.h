#ifndef _USR_AGENT_FUSION_H
#define _USR_AGENT_FUSION_H

#define AGENT_FUSION_MAX_ARGS 16
#define AGENT_FUSION_TEXT_MAX 512

int agent_fusion_enabled(int argc, char **argv);
int agent_fusion_build_argv(const char *input,
                            char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                            char *argv[AGENT_FUSION_MAX_ARGS + 1]);

#endif /* _USR_AGENT_FUSION_H */
