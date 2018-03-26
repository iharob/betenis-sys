#ifndef __BETENIS_COMMAND_HANDLER_H__
#define __BETENIS_COMMAND_HANDLER_H__

typedef struct bt_context bt_context;
int bt_start_daemon(bt_context *const context);
void bt_stop_daemon(void);
int bt_is_daemon_running(void);

#endif // __BETENIS_COMMAND_HANDLER_H__
