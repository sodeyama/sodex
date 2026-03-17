/*
 * session.h - Session persistence for agent conversations
 */
#ifndef _AGENT_SESSION_H
#define _AGENT_SESSION_H

#include <agent/conversation.h>

#define SESSION_ID_LEN       32
#define SESSION_MAX_SESSIONS 32
#define SESSION_DIR          "/var/agent/sessions"
#define SESSION_MAX_FILE     (64 * 1024)   /* 64KB per session */
#define SESSION_MAX_TOTAL    (512 * 1024)  /* 512KB total */

struct session_meta {
    char id[SESSION_ID_LEN + 1];
    int  created_at;
    int  turn_count;
    int  total_tokens;
    char model[64];
};

struct session_index {
    struct session_meta entries[SESSION_MAX_SESSIONS];
    int count;
};

/* Generate a session ID */
void session_generate_id(char *id_buf);

/* Create a new session */
int session_create(struct session_meta *meta, const char *model);

/* Append a turn to session file (JSONL) */
int session_append_turn(const char *session_id,
                         const struct conv_turn *turn,
                         int input_tokens, int output_tokens);

/* Load session into conversation */
int session_load(const char *session_id, struct conversation *conv);

/* List all sessions */
int session_list(struct session_index *index);

/* Delete a session */
int session_delete(const char *session_id);

/* Cleanup old sessions */
int session_cleanup(int max_sessions);

#endif /* _AGENT_SESSION_H */
