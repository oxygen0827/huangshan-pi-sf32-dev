#include <rtthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "vb_runtime_lua_host.h"

#define VB_LUA_ENGINE_NAME "lua-5.5-full"
#define VB_LUA_MEMORY_LIMIT (384u * 1024u)
#define VB_LUA_SCRIPT_MAX (64u * 1024u)
#define VB_LUA_INSTRUCTION_LIMIT 500000u
#define VB_LUA_HOOK_GRANULARITY 1000
#define VB_LUA_LINE_MAX 768
#define VB_LUA_OBJECT_NAME_MAX 24
#define VB_LUA_APP_DIR_MAX 160
#define VB_LUA_OBJECT_METATABLE "vibeboard.lvgl.object"

typedef struct
{
    rt_size_t size;
} vb_lua_alloc_header_t;

typedef struct
{
    char name[VB_LUA_OBJECT_NAME_MAX];
} vb_lua_object_t;

typedef struct
{
    lua_State *state;
    rt_size_t memory_used;
    rt_size_t memory_peak;
    rt_size_t memory_limit;
    uint32_t instruction_count;
    uint32_t instruction_limit;
    uint32_t object_sequence;
    char app_dir[VB_LUA_APP_DIR_MAX];
} vb_lua_runtime_t;

static vb_lua_runtime_t g_vb_lua;

void vibeboard_lua_stop_app(void);

static void vb_lua_copy(char *dst, rt_size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    rt_strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static void *vb_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    vb_lua_runtime_t *runtime = (vb_lua_runtime_t *)ud;
    vb_lua_alloc_header_t *old_header = RT_NULL;
    vb_lua_alloc_header_t *new_header;
    rt_size_t old_size = 0;
    (void)osize;

    if (ptr)
    {
        old_header = ((vb_lua_alloc_header_t *)ptr) - 1;
        old_size = old_header->size;
    }
    if (nsize == 0)
    {
        if (old_header)
        {
            runtime->memory_used -= old_size;
            rt_free(old_header);
        }
        return RT_NULL;
    }
    if (nsize > runtime->memory_limit ||
        runtime->memory_used - old_size > runtime->memory_limit - nsize)
    {
        return RT_NULL;
    }
    new_header = (vb_lua_alloc_header_t *)rt_malloc(sizeof(*new_header) + nsize);
    if (!new_header) return RT_NULL;
    new_header->size = nsize;
    if (ptr)
    {
        rt_memcpy(new_header + 1, ptr, old_size < nsize ? old_size : nsize);
        rt_free(old_header);
    }
    runtime->memory_used = runtime->memory_used - old_size + nsize;
    if (runtime->memory_used > runtime->memory_peak)
    {
        runtime->memory_peak = runtime->memory_used;
    }
    return new_header + 1;
}

static void vb_lua_instruction_hook(lua_State *L, lua_Debug *debug)
{
    (void)debug;
    g_vb_lua.instruction_count += VB_LUA_HOOK_GRANULARITY;
    if (g_vb_lua.instruction_count > g_vb_lua.instruction_limit)
    {
        luaL_error(L, "Runtime instruction limit exceeded");
    }
}

static int vb_lua_append(char *dst, rt_size_t cap, rt_size_t *used, const char *text)
{
    rt_size_t len;
    if (!dst || !used || !text) return -RT_EINVAL;
    len = rt_strlen(text);
    if (*used + len + 1 > cap) return -RT_EFULL;
    rt_memcpy(dst + *used, text, len);
    *used += len;
    dst[*used] = '\0';
    return RT_EOK;
}

static int vb_lua_append_quoted(char *dst, rt_size_t cap, rt_size_t *used, const char *text)
{
    const char *p = text ? text : "";
    if (vb_lua_append(dst, cap, used, "\"") != RT_EOK) return -RT_EFULL;
    while (*p)
    {
        char escaped[3] = {0, 0, 0};
        if (*p == '\\' || *p == '"')
        {
            escaped[0] = '\\';
            escaped[1] = *p;
        }
        else if (*p == '\n')
        {
            escaped[0] = '\\';
            escaped[1] = 'n';
        }
        else if (*p == '\r')
        {
            escaped[0] = '\\';
            escaped[1] = 'r';
        }
        else if (*p == '\t')
        {
            escaped[0] = '\\';
            escaped[1] = 't';
        }
        else
        {
            escaped[0] = *p;
        }
        if (vb_lua_append(dst, cap, used, escaped) != RT_EOK) return -RT_EFULL;
        p++;
    }
    return vb_lua_append(dst, cap, used, "\"");
}

static vb_lua_object_t *vb_lua_test_object(lua_State *L, int index)
{
    return (vb_lua_object_t *)luaL_testudata(L, index, VB_LUA_OBJECT_METATABLE);
}

static int vb_lua_append_argument(lua_State *L, int index, char *dst,
                                  rt_size_t cap, rt_size_t *used)
{
    vb_lua_object_t *object = vb_lua_test_object(L, index);
    char number[40];
    if (object) return vb_lua_append(dst, cap, used, object->name);
    switch (lua_type(L, index))
    {
    case LUA_TSTRING:
        return vb_lua_append_quoted(dst, cap, used, lua_tostring(L, index));
    case LUA_TNUMBER:
        vb_lua_copy(number, sizeof(number), lua_tostring(L, index));
        return vb_lua_append(dst, cap, used, number);
    case LUA_TBOOLEAN:
        return vb_lua_append(dst, cap, used, lua_toboolean(L, index) ? "1" : "0");
    case LUA_TNIL:
        return vb_lua_append_quoted(dst, cap, used, "");
    default:
        return -RT_EINVAL;
    }
}

static int vb_lua_build_call(lua_State *L, const char *name, char *line,
                             rt_size_t cap, int first_arg)
{
    rt_size_t used = 0;
    int index;
    if (vb_lua_append(line, cap, &used, name) != RT_EOK ||
        vb_lua_append(line, cap, &used, "(") != RT_EOK)
    {
        return -RT_EFULL;
    }
    for (index = first_arg; index <= lua_gettop(L); index++)
    {
        if (index > first_arg && vb_lua_append(line, cap, &used, ",") != RT_EOK)
        {
            return -RT_EFULL;
        }
        if (vb_lua_append_argument(L, index, line, cap, &used) != RT_EOK)
        {
            return -RT_EINVAL;
        }
    }
    return vb_lua_append(line, cap, &used, ")");
}

static int vb_lua_host_call(lua_State *L)
{
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    char line[VB_LUA_LINE_MAX];
    int result;
    if (!name) return luaL_error(L, "missing Runtime function name");
    result = vb_lua_build_call(L, name, line, sizeof(line), 1);
    if (result != RT_EOK) return luaL_error(L, "%s arguments are invalid", name);
    result = vibeboard_lua_host_execute(line);
    if (result != RT_EOK) return luaL_error(L, "unsupported Runtime call: %s", name);
    return 0;
}

static vb_lua_object_t *vb_lua_push_object(lua_State *L, const char *name)
{
    vb_lua_object_t *object = (vb_lua_object_t *)lua_newuserdatauv(L, sizeof(*object), 0);
    vb_lua_copy(object->name, sizeof(object->name), name);
    luaL_setmetatable(L, VB_LUA_OBJECT_METATABLE);
    return object;
}

static int vb_lua_object_tostring(lua_State *L)
{
    vb_lua_object_t *object = (vb_lua_object_t *)luaL_checkudata(L, 1, VB_LUA_OBJECT_METATABLE);
    lua_pushfstring(L, "lvgl.object(%s)", object->name);
    return 1;
}

static int vb_lua_screen(lua_State *L)
{
    vb_lua_push_object(L, "root");
    return 1;
}

static int vb_lua_create_object(lua_State *L)
{
    const char *function_name = lua_tostring(L, lua_upvalueindex(1));
    vb_lua_object_t *parent = (vb_lua_object_t *)luaL_checkudata(L, 1, VB_LUA_OBJECT_METATABLE);
    char object_name[VB_LUA_OBJECT_NAME_MAX];
    char line[VB_LUA_LINE_MAX];
    g_vb_lua.object_sequence++;
    rt_snprintf(object_name, sizeof(object_name), "__lua%lu",
                (unsigned long)g_vb_lua.object_sequence);
    rt_snprintf(line, sizeof(line), "local %s = %s(%s)",
                object_name, function_name, parent->name);
    line[sizeof(line) - 1] = '\0';
    if (vibeboard_lua_host_execute(line) != RT_EOK)
    {
        return luaL_error(L, "cannot create Runtime object with %s", function_name);
    }
    vb_lua_push_object(L, object_name);
    return 1;
}

static int vb_lua_print(lua_State *L)
{
    int count = lua_gettop(L);
    int index;
    rt_kprintf("[vb_runtime][lua]");
    for (index = 1; index <= count; index++)
    {
        size_t len = 0;
        const char *text = luaL_tolstring(L, index, &len);
        rt_kprintf("%s%.*s", index == 1 ? " " : "\t", (int)len, text ? text : "");
        lua_pop(L, 1);
    }
    rt_kprintf("\n");
    return 0;
}

static char *vb_lua_read_file(const char *path, rt_size_t *length)
{
    int fd;
    off_t size;
    char *buffer;
    rt_size_t total = 0;
    if (length) *length = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) return RT_NULL;
    size = lseek(fd, 0, SEEK_END);
    if (size < 0 || size > (off_t)VB_LUA_SCRIPT_MAX || lseek(fd, 0, SEEK_SET) < 0)
    {
        close(fd);
        return RT_NULL;
    }
    buffer = (char *)rt_malloc((rt_size_t)size + 1);
    if (!buffer)
    {
        close(fd);
        return RT_NULL;
    }
    while (total < (rt_size_t)size)
    {
        int got = read(fd, buffer + total, (rt_size_t)size - total);
        if (got <= 0)
        {
            rt_free(buffer);
            close(fd);
            return RT_NULL;
        }
        total += (rt_size_t)got;
    }
    close(fd);
    if (memchr(buffer, '\0', total))
    {
        rt_free(buffer);
        return RT_NULL;
    }
    buffer[total] = '\0';
    if (length) *length = total;
    return buffer;
}

static int vb_lua_safe_relative_path(const char *path)
{
    if (!path || !path[0] || path[0] == '/' || strstr(path, "..") || strstr(path, "//")) return 0;
    return 1;
}

static int vb_lua_load_app_file(lua_State *L, const char *relative)
{
    char path[VB_LUA_APP_DIR_MAX + 96];
    char *source;
    rt_size_t length;
    if (!vb_lua_safe_relative_path(relative))
    {
        lua_pushfstring(L, "unsafe app path: %s", relative ? relative : "");
        return LUA_ERRFILE;
    }
    rt_snprintf(path, sizeof(path), "%s/%s", g_vb_lua.app_dir, relative);
    path[sizeof(path) - 1] = '\0';
    source = vb_lua_read_file(path, &length);
    if (!source)
    {
        lua_pushfstring(L, "cannot read app file: %s", relative);
        return LUA_ERRFILE;
    }
    if (luaL_loadbuffer(L, source, length, path) != LUA_OK)
    {
        rt_free(source);
        return LUA_ERRSYNTAX;
    }
    rt_free(source);
    return LUA_OK;
}

static int vb_lua_dofile(lua_State *L)
{
    const char *relative = luaL_checkstring(L, 1);
    int status;
    lua_settop(L, 1);
    status = vb_lua_load_app_file(L, relative);
    if (status != LUA_OK) return lua_error(L);
    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) return lua_error(L);
    return lua_gettop(L) - 1;
}

static int vb_lua_loadfile(lua_State *L)
{
    const char *relative = luaL_checkstring(L, 1);
    int status;
    lua_settop(L, 1);
    status = vb_lua_load_app_file(L, relative);
    if (status == LUA_OK) return 1;
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

static int vb_lua_require(lua_State *L)
{
    const char *module = luaL_checkstring(L, 1);
    char relative[96];
    rt_size_t index;
    int status;
    if (rt_strlen(module) + 5 >= sizeof(relative)) return luaL_error(L, "module name too long");
    for (index = 0; module[index]; index++)
    {
        char c = module[index];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.'))
        {
            return luaL_error(L, "unsafe module name: %s", module);
        }
        relative[index] = c == '.' ? '/' : c;
    }
    relative[index] = '\0';
    strcat(relative, ".lua");

    lua_getglobal(L, "__vibeboard_loaded");
    lua_getfield(L, -1, module);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    status = vb_lua_load_app_file(L, relative);
    if (status != LUA_OK) return lua_error(L);
    status = lua_pcall(L, 0, 1, 0);
    if (status != LUA_OK) return lua_error(L);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, module);
    return 1;
}

static void vb_lua_register_function(lua_State *L, const char *name, lua_CFunction fn)
{
    lua_pushstring(L, name);
    lua_pushcclosure(L, fn, 1);
    lua_setglobal(L, name);
}

static void vb_lua_register_host_api(lua_State *L)
{
    static const char *const creators[] = {
        "lv_obj_create", "lv_label_create", "lv_btn_create", "lv_img_create", RT_NULL
    };
    static const char *const calls[] = {
        "lv_obj_clean", "lv_obj_set_size", "lv_obj_set_width", "lv_obj_set_height",
        "lv_obj_set_pos", "lv_obj_align", "lv_obj_center", "lv_obj_set_style_bg_color",
        "lv_obj_set_style_text_color", "lv_obj_set_style_radius",
        "lv_obj_set_style_border_width", "lv_obj_set_style_border_color",
        "lv_obj_clear_flag", "lv_label_set_text", "lv_label_set_long_mode",
        "lv_img_set_src", "vibe_label", "vibe_button", "vibe_image",
        "vibe_read_file", "vibe_timer_label", "vibe_sensor_label",
        "vibe_touch_label", "vibe_gpio_label", "vibe_power_label",
        "vibe_display_label", "vibe_display_brightness", "vibe_voice_start",
        "vibe_voice_clear", "vibe_voice_label", "vibe_flow_label", "vibe_rgb",
        "vibe_audio_play", "vibe_audio_stop", "vibe_audio_volume", "vibe_audio_label",
        "vibe_snake_autoplay", "vibe_2048_game", "vibe_weather_pet", RT_NULL
    };
    static const char *const align_constants[] = {
        "LV_ALIGN_CENTER", "LV_ALIGN_TOP_LEFT", "LV_ALIGN_TOP_MID",
        "LV_ALIGN_TOP_RIGHT", "LV_ALIGN_BOTTOM_LEFT", "LV_ALIGN_BOTTOM_MID",
        "LV_ALIGN_BOTTOM_RIGHT", "LV_ALIGN_LEFT_MID", "LV_ALIGN_RIGHT_MID", RT_NULL
    };
    int index;

    luaL_newmetatable(L, VB_LUA_OBJECT_METATABLE);
    lua_pushcfunction(L, vb_lua_object_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    lua_pushcfunction(L, vb_lua_screen);
    lua_setglobal(L, "lv_scr_act");
    for (index = 0; creators[index]; index++)
    {
        vb_lua_register_function(L, creators[index], vb_lua_create_object);
    }
    for (index = 0; calls[index]; index++)
    {
        vb_lua_register_function(L, calls[index], vb_lua_host_call);
    }
    for (index = 0; align_constants[index]; index++)
    {
        lua_pushstring(L, align_constants[index]);
        lua_setglobal(L, align_constants[index]);
    }
    lua_pushstring(L, "LV_OBJ_FLAG_SCROLLABLE");
    lua_setglobal(L, "LV_OBJ_FLAG_SCROLLABLE");
    lua_pushstring(L, "LV_LABEL_LONG_WRAP");
    lua_setglobal(L, "LV_LABEL_LONG_WRAP");
    lua_pushstring(L, "LV_LABEL_LONG_CLIP");
    lua_setglobal(L, "LV_LABEL_LONG_CLIP");
    lua_pushstring(L, "LV_LABEL_LONG_SCROLL_CIRCULAR");
    lua_setglobal(L, "LV_LABEL_LONG_SCROLL_CIRCULAR");

    lua_pushcfunction(L, vb_lua_print);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, vb_lua_dofile);
    lua_setglobal(L, "dofile");
    lua_pushcfunction(L, vb_lua_loadfile);
    lua_setglobal(L, "loadfile");
    lua_pushcfunction(L, vb_lua_require);
    lua_setglobal(L, "require");
    lua_newtable(L);
    lua_setglobal(L, "__vibeboard_loaded");
}

static void vb_lua_open_libraries(lua_State *L)
{
    static const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {RT_NULL, RT_NULL}
    };
    const luaL_Reg *library;
    for (library = libraries; library->func; library++)
    {
        luaL_requiref(L, library->name, library->func, 1);
        lua_pop(L, 1);
    }
}

static void vb_lua_set_app_dir(const char *script_path)
{
    char *slash;
    vb_lua_copy(g_vb_lua.app_dir, sizeof(g_vb_lua.app_dir), script_path);
    slash = strrchr(g_vb_lua.app_dir, '/');
    if (slash) *slash = '\0';
}

int vibeboard_lua_runtime_available(void)
{
    return 1;
}

const char *vibeboard_lua_runtime_name(void)
{
    return VB_LUA_ENGINE_NAME;
}

int vibeboard_lua_start_script(const char *script_path, const char *manifest_path)
{
    char *source;
    rt_size_t length;
    int status;
    (void)manifest_path;
    if (!script_path) return -RT_EINVAL;

    vibeboard_lua_stop_app();
    if (vibeboard_lua_host_reset() != RT_EOK) return -RT_ERROR;
    source = vb_lua_read_file(script_path, &length);
    if (!source) return -RT_ERROR;

    rt_memset(&g_vb_lua, 0, sizeof(g_vb_lua));
    g_vb_lua.memory_limit = VB_LUA_MEMORY_LIMIT;
    g_vb_lua.instruction_limit = VB_LUA_INSTRUCTION_LIMIT;
    vb_lua_set_app_dir(script_path);
    g_vb_lua.state = lua_newstate(vb_lua_alloc, &g_vb_lua, 0);
    if (!g_vb_lua.state)
    {
        rt_free(source);
        return -RT_ENOMEM;
    }
    vb_lua_open_libraries(g_vb_lua.state);
    vb_lua_register_host_api(g_vb_lua.state);
    lua_sethook(g_vb_lua.state, vb_lua_instruction_hook, LUA_MASKCOUNT,
                VB_LUA_HOOK_GRANULARITY);
    status = luaL_loadbuffer(g_vb_lua.state, source, length, script_path);
    rt_free(source);
    if (status == LUA_OK)
    {
        status = lua_pcall(g_vb_lua.state, 0, 0, 0);
    }
    if (status != LUA_OK)
    {
        const char *message = lua_tostring(g_vb_lua.state, -1);
        rt_kprintf("[vb_runtime][lua] start failed status=%d error=%s\n",
                   status, message ? message : "unknown");
        vibeboard_lua_stop_app();
        return status == LUA_ERRMEM ? -RT_ENOMEM : -RT_ERROR;
    }
    vibeboard_lua_host_set_active(1);
    rt_kprintf("[vb_runtime][lua] started engine=%s bytes=%lu memory=%lu peak=%lu\n",
               VB_LUA_ENGINE_NAME, (unsigned long)length,
               (unsigned long)g_vb_lua.memory_used,
               (unsigned long)g_vb_lua.memory_peak);
    return RT_EOK;
}

void vibeboard_lua_stop_app(void)
{
    if (g_vb_lua.state)
    {
        lua_close(g_vb_lua.state);
        g_vb_lua.state = RT_NULL;
    }
    vibeboard_lua_host_stop();
    rt_memset(&g_vb_lua, 0, sizeof(g_vb_lua));
}
