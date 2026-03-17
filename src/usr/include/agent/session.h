/*
 * session.h - Session persistence for agent conversations
 */
#ifndef _AGENT_SESSION_H
#define _AGENT_SESSION_H

#include <agent/conversation.h>

#define SESSION_ID_LEN       32
#define SESSION_NAME_LEN     64
#define SESSION_CWD_LEN      256
#define SESSION_SUMMARY_LEN  160
#define SESSION_MAX_SESSIONS 32
#ifndef SESSION_DIR
#ifdef TEST_BUILD
#define SESSION_DIR          "/tmp/agent_test_sessions"
#else
#define SESSION_DIR          "/var/agent/sessions"
#endif
#endif
#define SESSION_MAX_FILE     (64 * 1024)   /* 64KB per session */
#define SESSION_MAX_TOTAL    (512 * 1024)  /* 512KB total */

struct session_meta {
    char id[SESSION_ID_LEN + 1];
    char name[SESSION_NAME_LEN];
    char cwd[SESSION_CWD_LEN];
    unsigned int cwd_hash;
    int  created_at;
    int  last_active_at;
    int  turn_count;
    int  total_tokens;
    int  compact_count;
    char model[64];
    char summary[SESSION_SUMMARY_LEN];
};

struct session_index {
    struct session_meta entries[SESSION_MAX_SESSIONS];
    int count;
};

/* Generate a session ID */
void session_generate_id(char *id_buf);

/* Create a new session */
int session_create(struct session_meta *meta,
                    const char *model,
                    const char *cwd);

/* Append a turn to session file (JSONL) */
int session_append_turn(const char *session_id,
                         const struct conv_turn *turn,
                         int input_tokens, int output_tokens);

/* Append compact event */
int session_append_compact(const char *session_id,
                            const char *summary,
                            int from_turn, int to_turn);

/* Append rename event */
int session_append_rename(const char *session_id, const char *name);

/* Load session into conversation */
int session_load(const char *session_id, struct conversation *conv);

/* Read effective metadata for a session */
int session_read_meta(const char *session_id, struct session_meta *meta);

/* List all sessions */
int session_list(struct session_index *index);

/* Delete a session */
int session_delete(const char *session_id);

/* Cleanup old sessions */
int session_cleanup(int max_sessions);

#endif /* _AGENT_SESSION_H */
