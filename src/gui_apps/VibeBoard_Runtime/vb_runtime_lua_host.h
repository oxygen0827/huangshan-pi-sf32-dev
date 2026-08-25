#ifndef VB_RUNTIME_LUA_HOST_H
#define VB_RUNTIME_LUA_HOST_H

enum
{
    VIBEBOARD_LUA_STOP_IDLE = 0,
    VIBEBOARD_LUA_STOP_CLOSING,
    VIBEBOARD_LUA_STOP_COMPLETE,
};

int vibeboard_lua_host_reset(void);
int vibeboard_lua_host_execute(const char *line);
void vibeboard_lua_host_set_active(int active);
int vibeboard_lua_host_stop(void);

/* Lua owns no LVGL work after script startup.  Closing the VM can nevertheless
 * run arbitrary Lua cleanup, so Runtime polls this operation from its GUI timer
 * instead of allowing lua_close() to block that timer indefinitely. */
int vibeboard_lua_begin_stop_async(void);
int vibeboard_lua_stop_async_state(void);
int vibeboard_lua_finish_stop_async(void);
/* Stop the Lua VM without ever calling lua_close() on the calling thread.
 * Returns RT_EOK when the VM and host are fully stopped, or an error when the
 * asynchronous close does not finish within the bounded deadline.  Callers must
 * treat a timeout as a recovery condition and must not re-enter lua_close. */
int vibeboard_lua_stop_app(void);

#endif
