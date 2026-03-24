/*
 * term_session_surface.c - terminal 上の agent session surface
 */

#include <agent/term_session_surface.h>
#include <agent/conversation.h>
#include <agent/path_utils.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void surface_copy_text(char *dst, int cap, const char *src)
{
    int len;

    if (!dst || cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
}

static int surface_append(char *out, int out_cap, int pos, const char *text)
{
    int len;

    if (!out || out_cap <= 0 || !text)
        return pos;
    if (pos < 0)
        pos = 0;
    if (pos >= out_cap - 1)
        return out_cap - 1;

    len = strlen(text);
    if (pos + len >= out_cap)
        len = out_cap - pos - 1;
    if (len > 0)
        memcpy(out + pos, text, (size_t)len);
    pos += len;
    out[pos] = '\0';
    return pos;
}

static int surface_append_format(char *out, int out_cap, int pos,
                                 const char *fmt, ...)
{
    va_list ap;
    int written;

    if (!out || out_cap <= 0)
        return pos;
    if (pos < 0)
        pos = 0;
    if (pos >= out_cap - 1)
        return out_cap - 1;

    va_start(ap, fmt);
    written = vsnprintf(out + pos, (size_t)(out_cap - pos), fmt, ap);
    va_end(ap);
    if (written < 0)
        return pos;
    if (pos + written >= out_cap)
        return out_cap - 1;
    return pos + written;
}

const char *term_session_surface_permission_name(enum permission_mode mode)
{
    if (mode == PERM_STRICT)
        return "strict";
    if (mode == PERM_PERMISSIVE)
        return "permissive";
    return "standard";
}

int term_session_surface_permission_parse(const char *text,
                                         enum permission_mode *mode_out)
{
    if (!text || !mode_out)
        return -1;
    if (strcmp(text, "strict") == 0) {
        *mode_out = PERM_STRICT;
        return 0;
    }
    if (strcmp(text, "standard") == 0) {
        *mode_out = PERM_STANDARD;
        return 0;
    }
    if (strcmp(text, "permissive") == 0) {
        *mode_out = PERM_PERMISSIVE;
        return 0;
    }
    return -1;
}

void term_session_surface_init(struct term_session_surface *surface)
{
    if (!surface)
        return;
    memset(surface, 0, sizeof(*surface));
    surface->drawer_mode = TERM_SESSION_DRAWER_TRANSIENT;
    surface->permission_mode = PERM_STANDARD;
    surface_copy_text(surface->session_name, sizeof(surface->session_name), "main");
    surface_copy_text(surface->cwd, sizeof(surface->cwd), AGENT_DEFAULT_HOME);
}

void term_session_surface_set_drawer_mode(struct term_session_surface *surface,
                                          enum term_session_drawer_mode mode)
{
    if (!surface)
        return;
    surface->drawer_mode = mode;
    surface->transient_visible = (mode == TERM_SESSION_DRAWER_PINNED);
}

int term_session_surface_should_render(const struct term_session_surface *surface)
{
    if (!surface)
        return 0;
    if (surface->drawer_mode == TERM_SESSION_DRAWER_PINNED)
        return 1;
    if (surface->drawer_mode == TERM_SESSION_DRAWER_TRANSIENT &&
        surface->transient_visible != 0) {
        return 1;
    }
    return 0;
}

void term_session_surface_after_render(struct term_session_surface *surface)
{
    if (!surface)
        return;
    if (surface->drawer_mode == TERM_SESSION_DRAWER_TRANSIENT)
        surface->transient_visible = 0;
}

void term_session_surface_set_session(struct term_session_surface *surface,
                                      const struct session_meta *meta,
                                      const char *cwd,
                                      int total_tokens)
{
  if (!surface)
    return;

  if (meta) {
        surface_copy_text(surface->session_id, sizeof(surface->session_id), meta->id);
        surface_copy_text(surface->session_name, sizeof(surface->session_name),
                          meta->name[0] ? meta->name : "main");
        surface_copy_text(surface->cwd, sizeof(surface->cwd),
                          meta->cwd[0] ? meta->cwd : AGENT_DEFAULT_HOME);
        surface_copy_text(surface->model, sizeof(surface->model), meta->model);
        surface->turn_count = meta->turn_count;
        surface->total_tokens = meta->total_tokens;
    } else {
        surface->session_id[0] = '\0';
        surface_copy_text(surface->session_name, sizeof(surface->session_name),
                          "main");
        surface->model[0] = '\0';
        surface->turn_count = 0;
        surface->total_tokens = 0;
    }
    if (cwd && cwd[0] != '\0')
        surface_copy_text(surface->cwd, sizeof(surface->cwd), cwd);
    if (total_tokens >= 0)
        surface->total_tokens = total_tokens;

    surface->context_percent = 0;
    if (surface->total_tokens > 0)
        surface->context_percent =
            (surface->total_tokens * 100) / CONV_TOKEN_LIMIT;
    if (surface->context_percent < 0)
        surface->context_percent = 0;
    if (surface->context_percent > 999)
        surface->context_percent = 999;
    surface->transient_visible = 1;
}

void term_session_surface_set_permission_mode(struct term_session_surface *surface,
                                              enum permission_mode mode)
{
    if (!surface)
        return;
    surface->permission_mode = mode;
    surface->transient_visible = 1;
}

void term_session_surface_set_route(struct term_session_surface *surface,
                                    const char *route)
{
    if (!surface)
        return;
    surface_copy_text(surface->last_route, sizeof(surface->last_route), route);
    surface->transient_visible = 1;
}

void term_session_surface_set_transcript(struct term_session_surface *surface,
                                         const char *user_text,
                                         const char *assistant_text)
{
    if (!surface)
        return;
    surface_copy_text(surface->last_user, sizeof(surface->last_user), user_text);
    surface_copy_text(surface->last_assistant, sizeof(surface->last_assistant),
                      assistant_text);
    surface->transient_visible = 1;
}

void term_session_surface_set_audit(struct term_session_surface *surface,
                                    const char *audit_text)
{
    if (!surface)
        return;
    surface_copy_text(surface->last_audit, sizeof(surface->last_audit), audit_text);
    surface->transient_visible = 1;
}

void term_session_surface_set_recent_command(struct term_session_surface *surface,
                                             const char *command,
                                             int exit_status,
                                             const char *stdout_tail,
                                             const char *stderr_tail,
                                             int timestamp)
{
    if (!surface)
        return;
    surface->recent.valid = (command && command[0] != '\0');
    surface->recent.exit_status = exit_status;
    surface->recent.timestamp = timestamp;
    surface_copy_text(surface->recent.command, sizeof(surface->recent.command),
                      command);
    surface_copy_text(surface->recent.stdout_tail,
                      sizeof(surface->recent.stdout_tail), stdout_tail);
    surface_copy_text(surface->recent.stderr_tail,
                      sizeof(surface->recent.stderr_tail), stderr_tail);
}

void term_session_surface_set_command_block(struct term_session_surface *surface,
                                            const struct term_command_block *block)
{
    if (!surface)
        return;
    if (!block) {
        term_command_block_clear(&surface->command_block);
        return;
    }
    memcpy(&surface->command_block, block, sizeof(surface->command_block));
    surface->transient_visible = 1;
}

int term_session_surface_format_drawer(const struct term_session_surface *surface,
                                       char *out, int out_cap)
{
    int pos = 0;

    if (!surface || !out || out_cap <= 0)
        return -1;

    out[0] = '\0';
    pos = surface_append_format(
        out, out_cap, pos,
        "session=%s name=%s cwd=%s ctx=%d%% perm=%s route=%s\n",
        surface->session_id[0] ? surface->session_id : "-",
        surface->session_name[0] ? surface->session_name : "main",
        surface->cwd[0] ? surface->cwd : AGENT_DEFAULT_HOME,
        surface->context_percent,
        term_session_surface_permission_name(surface->permission_mode),
        surface->last_route[0] ? surface->last_route : "-");
    if (surface->recent.valid) {
        pos = surface_append_format(out, out_cap, pos,
                                    "recent=%s exit=%d\n",
                                    surface->recent.command,
                                    surface->recent.exit_status);
    }
    if (surface->last_user[0] != '\0')
        pos = surface_append_format(out, out_cap, pos,
                                    "user: %s\n", surface->last_user);
    if (surface->last_assistant[0] != '\0')
        pos = surface_append_format(out, out_cap, pos,
                                    "assistant: %s\n", surface->last_assistant);
    if (surface->last_audit[0] != '\0')
        pos = surface_append_format(out, out_cap, pos,
                                    "audit: %s\n", surface->last_audit);
    if (surface->command_block.active != 0)
        pos += term_command_block_format(&surface->command_block,
                                         out + pos, out_cap - pos);
    return pos;
}

int term_session_surface_build_prompt(const struct term_session_surface *surface,
                                      const char *user_prompt,
                                      char *out, int out_cap)
{
    int pos = 0;

    if (!user_prompt || !out || out_cap <= 0)
        return -1;
    if (!surface || !surface->recent.valid)
        return snprintf(out, (size_t)out_cap, "%s", user_prompt);

    out[0] = '\0';
    pos = surface_append(out, out_cap, pos, "Recent shell context:\n");
    pos = surface_append_format(out, out_cap, pos,
                                "command: %s\n", surface->recent.command);
    pos = surface_append_format(out, out_cap, pos,
                                "exit_status: %d\n", surface->recent.exit_status);
    if (surface->recent.stdout_tail[0] != '\0')
        pos = surface_append_format(out, out_cap, pos,
                                    "stdout_tail: %s\n",
                                    surface->recent.stdout_tail);
    if (surface->recent.stderr_tail[0] != '\0')
        pos = surface_append_format(out, out_cap, pos,
                                    "stderr_tail: %s\n",
                                    surface->recent.stderr_tail);
    pos = surface_append(out, out_cap, pos, "User request:\n");
    pos = surface_append(out, out_cap, pos, user_prompt);
    return pos;
}
