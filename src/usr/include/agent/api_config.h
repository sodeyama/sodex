/*
 * api_config.h - LLM API endpoint and header configuration
 *
 * Config layer: change endpoint/headers here without touching adapter code.
 */

#ifndef _AGENT_API_CONFIG_H
#define _AGENT_API_CONFIG_H

#include <sys/types.h>

struct api_endpoint {
    const char *host;
    const char *path;
    u_int16_t   port;
};

struct api_header {
    const char *name;
    const char *value;
};

#endif /* _AGENT_API_CONFIG_H */
