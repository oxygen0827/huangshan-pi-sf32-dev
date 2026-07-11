#ifndef VB_RUNTIME_LUA_HOST_H
#define VB_RUNTIME_LUA_HOST_H

int vibeboard_lua_host_reset(void);
int vibeboard_lua_host_execute(const char *line);
void vibeboard_lua_host_set_active(int active);
void vibeboard_lua_host_stop(void);

#endif
