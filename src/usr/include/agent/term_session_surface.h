/*
 * term_session_surface.h - terminal 上の agent session surface
 */
#ifndef _AGENT_TERM_SESSION_SURFACE_H
#define _AGENT_TERM_SESSION_SURFACE_H

#include <agent/term_command_block.h>
#include <agent/permissions.h>
#include <agent/session.h>

#define TERM_SESSION_SURFACE_TEXT_MAX   192
#define TERM_SESSION_SURFACE_LINE_MAX  1024
#define TERM_SESSION_SURFACE_PROMPT_MAX 1024

enum term_session_drawer_mode {
    TERM_SESSION_DRAWER_HIDDEN = 0,
    TERM_SESSION_DRAWER_TRANSIENT,
    TERM_SESSION_DRAWER_PINNED,
};

struct term_session_recent_block {
    int valid;
    int exit_status;
    int timestamp;
    char command[TERM_SESSION_SURFACE_TEXT_MAX];
    char stdout_tail[TERM_SESSION_SURFACE_TEXT_MAX];
    char stderr_tail[TERM_SESSION_SURFACE_TEXT_MAX];
};

struct term_session_surface {
    enum term_session_drawer_mode drawer_mode;
    int transient_visible;
    enum permission_mode permission_mode;
    char session_id[SESSION_ID_LEN + 1];
    char session_name[SESSION_NAME_LEN];
    char cwd[SESSION_CWD_LEN];
    char model[64];
    int turn_count;
    int total_tokens;
    int context_percent;
    char last_route[32];
    char last_user[TERM_SESSION_SURFACE_TEXT_MAX];
    char last_assistant[TERM_SESSION_SURFACE_TEXT_MAX];
    char last_audit[TERM_SESSION_SURFACE_TEXT_MAX];
    struct term_session_recent_block recent;
    struct term_command_block command_block;
};

void term_session_surface_init(struct term_session_surface *surface);
void term_session_surface_set_drawer_mode(struct term_session_surface *surface,
                                          enum term_session_drawer_mode mode);
int term_session_surface_should_render(const struct term_session_surface *surface);
void term_session_surface_after_render(struct term_session_surface *surface);
void term_session_surface_set_session(struct term_session_surface *surface,
                                      const struct session_meta *meta,
                                      const char *cwd,
                                      int total_tokens);
void term_session_surface_set_permission_mode(struct term_session_surface *surface,
                                              enum permission_mode mode);
void term_session_surface_set_route(struct term_session_surface *surface,
                                    const char *route);
void term_session_surface_set_transcript(struct term_session_surface *surface,
                                         const char *user_text,
                                         const char *assistant_text);
void term_session_surface_set_audit(struct term_session_surface *surface,
                                    const char *audit_text);
void term_session_surface_set_recent_command(struct term_session_surface *surface,
                                             const char *command,
                                             int exit_status,
                                             const char *stdout_tail,
                                             const char *stderr_tail,
                                             int timestamp);
void term_session_surface_set_command_block(struct term_session_surface *surface,
                                            const struct term_command_block *block);
const char *term_session_surface_permission_name(enum permission_mode mode);
int term_session_surface_permission_parse(const char *text,
                                         enum permission_mode *mode_out);
int term_session_surface_format_drawer(const struct term_session_surface *surface,
                                       char *out, int out_cap);
int term_session_surface_build_prompt(const struct term_session_surface *surface,
                                      const char *user_prompt,
                                      char *out, int out_cap);

#endif /* _AGENT_TERM_SESSION_SURFACE_H */
