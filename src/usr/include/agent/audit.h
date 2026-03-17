/*
 * audit.h - Audit logging for tool execution
 */
#ifndef _AGENT_AUDIT_H
#define _AGENT_AUDIT_H

#ifdef TEST_BUILD
#define AUDIT_LOG_PATH  "/tmp/agent_test_audit/audit.log"
#else
#define AUDIT_LOG_PATH  "/var/agent/audit.log"
#endif
#define AUDIT_MAX_ENTRY 512

struct audit_entry {
    int  timestamp;
    char session_id[33];
    int  step;
    char tool_name[64];
    char action[16];     /* "execute", "blocked", "error" */
    char detail[256];
};

int audit_init(void);
int audit_log(const struct audit_entry *entry);
int audit_read_last(struct audit_entry *entries, int max_entries, int *count);
int audit_rotate(int max_size);

#endif /* _AGENT_AUDIT_H */
