/*
 * repl.c - REPL/session helpers
 */

#include <agent/agent.h>
#include <agent/session.h>
#include <string.h>

int agent_resume_latest_for_cwd(const char *cwd,
                                char *session_id_out,
                                int session_id_cap)
{
    struct session_index index;
    int i;

    if (!cwd || !session_id_out || session_id_cap <= 0)
        return -1;

    session_id_out[0] = '\0';
    if (session_list(&index) != 0)
        return -1;

    for (i = index.count - 1; i >= 0; i--) {
        if (strcmp(index.entries[i].cwd, cwd) == 0) {
            int len = strlen(index.entries[i].id);

            if (len >= session_id_cap)
                len = session_id_cap - 1;
            memcpy(session_id_out, index.entries[i].id, (size_t)len);
            session_id_out[len] = '\0';
            return 0;
        }
    }

    return -1;
}
