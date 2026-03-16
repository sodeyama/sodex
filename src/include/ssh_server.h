#ifndef _SSH_SERVER_H
#define _SSH_SERVER_H

#ifndef PUBLIC
#define PUBLIC
#endif

PUBLIC void ssh_server_init(void);
PUBLIC void ssh_server_tick(void);
PUBLIC void ssh_userland_bootstrap(void);
PUBLIC void ssh_userland_sync_tick(void);
PUBLIC void ssh_userland_refresh_runtime(void);
PUBLIC int ssh_userland_connection_pending(void);
PUBLIC int ssh_userland_listener_pending(void);
PUBLIC int ssh_userland_channel_fd(void);

#endif
