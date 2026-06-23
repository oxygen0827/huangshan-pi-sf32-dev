#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <dfs_file.h>
#include <dfs_fs.h>
#include <dfs_posix.h>
#if defined(RT_USING_SENSOR)
#include "bf0_hal.h"
#include "drv_io.h"
#include "sensor.h"
#if defined(ASL_USING_LTR303)
#include "sensor_liteon_ltr303.h"
#endif
#if defined(MAG_USING_MMC56X3)
#include "sensor_memsic_mmc56x3.h"
#endif
#if defined(ACC_USING_LSM6DSL)
#include "st_lsm6dsl_sensor_v1.h"
#endif
#endif
#ifdef PKG_USING_WEBCLIENT
#include <webclient.h>
#endif
#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH)
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#if defined(CFG_PAN) && defined(RT_USING_LWIP)
#include "bts2_app_inc.h"
#include "bts2_app_demo.h"
#include "bt_connection_manager.h"
#endif
#include "ble_connection_manager.h"
#endif
#include "drv_flash.h"
#include "lvgl.h"
#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lv_ex_data.h"

#define APP_ID "vb_runtime"
#define VIBEBOARD_RUNTIME_API_VERSION "vibeboard-huangshan-runtime/v1"
#define VIBEBOARD_RUNTIME_NET_API_VERSION "vibeboard-huangshan-net/v1"
#define VIBEBOARD_RUNTIME_BLE_API_VERSION "vibeboard-huangshan-ble-install/v1"
#define VIBEBOARD_RUNTIME_SENSOR_API_VERSION "vibeboard-huangshan-sensors/v1"
#define VIBEBOARD_APP_ROOT "/sdcard/apps"
#define VIBEBOARD_ACTIVE_APP_FILE VIBEBOARD_APP_ROOT "/.active"
#define VIBEBOARD_STAGING_PREFIX ".__install_"
#define VIBEBOARD_BACKUP_PREFIX ".__backup_"
#define VIBEBOARD_DEFAULT_APP_ID "welcome"
#define VIBEBOARD_FS_DEVICE "vbfs"
#define VIBEBOARD_BT_PAN_NAME "VibeBoard-PAN"
#define VIBEBOARD_BLE_NAME "VibeBoard"

#define VB_MAX_APP_ID 16
#define VB_MAX_PATH 160
#define VB_MAX_TEXT 96
#define VB_MAX_VALUE 96
#define VB_MAX_URL 192
#define VB_MAX_MANIFEST 4096
#define VB_MAX_SCRIPT 8192
#define VB_MAX_SCRIPT_LINE 256
#define VB_MAX_COMPONENTS 8
#define VB_MAX_SCRIPT_OBJECTS 24
#define VB_MAX_SCRIPT_NAME 24
#define VB_MAX_SCRIPT_ARGS 6
#define VB_MAX_SCRIPT_ARG 96
#define VB_MAX_HEX_CHARS 512
#define VB_AUTORUN_DELAY_MS 2200
#define VB_TIMER_PERIOD_MS 200
#define VB_STATUS_TICK_REFRESH_MS 1000
#define VB_PAN_TIMER_MS 3000
#define VB_HTTP_HEADER_BUFSZ 1024
#define VB_HTTP_CHUNK_SIZE 512
#define VB_HTTP_MAX_FILE_SIZE 16384
#define VB_HTTP_MAX_RESOURCE_SIZE 131072
#define VB_HTTP_FILES_TXT_MAX 4096
#define VB_BLE_MAX_COMMAND 896
#define VB_BLE_STATUS_MAX 512
#define VB_SENSOR_JSON_MAX 512
#define VB_PAN_EVT_STACK_READY 1
#define VB_PAN_EVT_CONNECT_PAN 2
#define VB_PAN_EVT_OPEN_BT 3
#define VB_PAN_EVT_PROBE 4
#define VB_PAN_EVT_INQUIRY 5
#define VB_PAN_OPEN_RETRY_MS 1000
#define VB_PAN_OPEN_MAX_RETRY 30
#define VB_SNAKE_MAX_COLS 12
#define VB_SNAKE_MAX_ROWS 12
#define VB_SNAKE_MAX_CELLS (VB_SNAKE_MAX_COLS * VB_SNAKE_MAX_ROWS)
#define VB_SNAKE_MAX_DRAWN 28
#define VB_SNAKE_LOG_EVERY 120

#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH) && defined(CFG_PAN) && defined(RT_USING_LWIP)
#define VB_RUNTIME_HAS_BT_PAN 1
#else
#define VB_RUNTIME_HAS_BT_PAN 0
#endif

#if VB_RUNTIME_HAS_BT_PAN && defined(PKG_USING_WEBCLIENT)
#define VB_RUNTIME_HAS_HTTP_APP_OTA 1
#else
#define VB_RUNTIME_HAS_HTTP_APP_OTA 0
#endif

#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH) && defined(BSP_BLE_SIBLES)
#define VB_RUNTIME_HAS_BLE_INSTALL 1
#else
#define VB_RUNTIME_HAS_BLE_INSTALL 0
#endif

typedef struct
{
    char type[24];
    char capability[32];
    char label[VB_MAX_TEXT];
    char value[VB_MAX_VALUE];
    lv_obj_t *value_label;
} vb_component_t;

typedef enum
{
    VB_SCRIPT_OBJ_ROOT = 0,
    VB_SCRIPT_OBJ_CONTAINER,
    VB_SCRIPT_OBJ_LABEL,
    VB_SCRIPT_OBJ_BUTTON,
    VB_SCRIPT_OBJ_IMAGE
} vb_script_object_type_t;

typedef struct
{
    char name[VB_MAX_SCRIPT_NAME];
    lv_obj_t *obj;
    vb_script_object_type_t type;
} vb_script_object_t;

typedef struct
{
    int active;
    int cols;
    int rows;
    int cell;
    int length;
    int dir_x;
    int dir_y;
    int food_x;
    int food_y;
    int score;
    int best_score;
    int moves;
    int last_score;
    int last_best_score;
    int last_status_move;
    uint32_t last_run;
    uint32_t period_ticks;
    lv_obj_t *tiles[VB_SNAKE_MAX_CELLS];
    lv_obj_t *segments[VB_SNAKE_MAX_DRAWN];
    lv_obj_t *food;
    lv_obj_t *score_label;
    lv_obj_t *status_label;
    int snake_x[VB_SNAKE_MAX_CELLS];
    int snake_y[VB_SNAKE_MAX_CELLS];
} vb_snake_state_t;

typedef struct
{
    int initialized;
    int bt_opened;
    int stack_ready;
    int bt_connected;
    int pan_connected;
    int connecting;
    int last_error;
    int open_retries;
    int scan_mode;
    int target_scan_mode;
    int scan_mode_fsm;
    int scan_confirmed;
    int pairing_pending;
    int name_requested;
    int name_confirmed;
    int final_name_requested;
    int inquiry_running;
    int inquiry_count;
    uint32_t pair_code;
    char local_name[32];
#if VB_RUNTIME_HAS_BT_PAN
    bt_notify_device_mac_t local_addr;
    bt_notify_device_mac_t bd_addr;
    rt_timer_t connect_timer;
    rt_timer_t open_timer;
    rt_mailbox_t mailbox;
#endif
} vb_pan_state_t;

typedef struct
{
    int configured;
    int initialized;
    int init_result;
    int ready;
    uint32_t read_count;
    rt_device_t light;
    rt_device_t mag;
    rt_device_t acce;
    rt_device_t gyro;
    rt_device_t step;
    int have_light;
    int have_mag;
    int have_acce;
    int have_gyro;
    int have_step;
    struct rt_sensor_data light_data;
    struct rt_sensor_data mag_data;
    struct rt_sensor_data acce_data;
    struct rt_sensor_data gyro_data;
    struct rt_sensor_data step_data;
} vb_sensor_state_t;

#if VB_RUNTIME_HAS_BLE_INSTALL
typedef struct
{
    int initialized;
    int power_on;
    int service_ready;
    int advertising;
    int connected;
    uint16_t notify_cccd;
    uint8_t conn_idx;
    uint16_t mtu;
    sibles_hdl srv_handle;
    rt_mailbox_t mailbox;
    char rx_line[VB_BLE_MAX_COMMAND + 1];
    int rx_len;
    char status[VB_BLE_STATUS_MAX];
} vb_ble_install_state_t;
#endif

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *status_label;
    lv_obj_t *clock_label;
    lv_timer_t *timer;
    char active_app[VB_MAX_APP_ID];
    char app_name[VB_MAX_TEXT];
    char description[VB_MAX_TEXT];
    vb_component_t components[VB_MAX_COMPONENTS];
    int component_count;
    vb_script_object_t script_objects[VB_MAX_SCRIPT_OBJECTS];
    int script_object_count;
    lv_obj_t *script_last_label;
    lv_obj_t *script_tick_label;
    char script_tick_prefix[VB_MAX_TEXT];
    uint32_t script_tick_count;
    uint32_t script_tick_period_ticks;
    uint32_t script_tick_last_run;
    int script_runtime_active;
    vb_snake_state_t snake;
    volatile int pending_reload;
    int running;
    int fs_ready;
    int fs_mounted;
    int quiet_logs;
    uint32_t tick_count;
    uint32_t last_clock_update;
} vb_runtime_state_t;

static vb_runtime_state_t g_vb_runtime;
static vb_pan_state_t g_vb_pan;
static vb_sensor_state_t g_vb_sensor;
#if VB_RUNTIME_HAS_BLE_INSTALL
static vb_ble_install_state_t g_vb_ble;
#endif

static int vb_builtin_script_start(const char *script_path, const char *manifest_path);
static void vb_builtin_script_stop(void);
static lv_obj_t *vb_create_label(lv_obj_t *parent, const char *text, uint16_t font_size,
                                 lv_color_t color);
static int vb_pan_init(void);
static int vb_pan_open_now(void);
static int vb_pan_scan_now(void);
static int vb_pan_probe_now(void);
static int vb_pan_connect_now(void);
static int vb_pan_forget(void);
static int vb_runtime_install_begin_app(const char *app_id);
static int vb_runtime_install_file_chunk(const char *app_id, const char *path,
                                         const char *offset_text, const char *hex);
static int vb_runtime_install_end_app(const char *app_id);
static int vb_runtime_select_app(const char *app_id);
static void vb_runtime_request_reload(void);
static int vb_runtime_install_url_app(const char *app_id, const char *base_url);
static void vb_read_active_app(char *dst, rt_size_t cap);
static int vb_runtime_sensors_read_json(char *dst, rt_size_t cap);
static int vb_runtime_sensors_status_command(void);

#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH)
static int g_vb_bt_core_enable_requested;

static void vb_bt_core_enable_once(void)
{
    if (g_vb_bt_core_enable_requested)
    {
        rt_kprintf("[vb_runtime][bt] core enable already requested\n");
        return;
    }
    g_vb_bt_core_enable_requested = 1;
    sifli_ble_enable();
}
#endif

__attribute__((weak)) int vibeboard_lua_runtime_available(void)
{
    return 1;
}

__attribute__((weak)) const char *vibeboard_lua_runtime_name(void)
{
    return "script-subset";
}

__attribute__((weak)) int vibeboard_lua_start_script(const char *script_path, const char *manifest_path)
{
    return vb_builtin_script_start(script_path, manifest_path);
}

__attribute__((weak)) void vibeboard_lua_stop_app(void)
{
    vb_builtin_script_stop();
}

static void vb_safe_copy(char *dst, rt_size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    rt_strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static int vb_file_exists(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

static int vb_is_safe_app_id(const char *app_id)
{
    int i;
    int len;
    if (!app_id || app_id[0] < 'a' || app_id[0] > 'z') return 0;
    len = rt_strlen(app_id);
    if (len <= 0 || len >= VB_MAX_APP_ID) return 0;
    for (i = 0; i < len; i++)
    {
        char c = app_id[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') continue;
        return 0;
    }
    return 1;
}

static int vb_is_safe_package_path(const char *path)
{
    int i;
    int len;
    if (!path || !path[0] || path[0] == '/') return 0;
    if (strstr(path, "..") || strstr(path, "//")) return 0;
    len = rt_strlen(path);
    if (len <= 0 || len > 96) return 0;
    for (i = 0; i < len; i++)
    {
        char c = path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' ||
            c == '.' || c == '/')
        {
            continue;
        }
        return 0;
    }
    return 1;
}

static int vb_is_core_package_path(const char *path)
{
    return path &&
           (rt_strcmp(path, "manifest.json") == 0 ||
            rt_strcmp(path, "main.lua") == 0 ||
            rt_strcmp(path, "app.info") == 0 ||
            rt_strcmp(path, "files.txt") == 0);
}

static int vb_is_resource_package_path(const char *path)
{
    const char *dot;
    if (!vb_is_safe_package_path(path)) return 0;
    dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') return 0;
    if (rt_strcmp(dot, ".json") != 0 &&
        rt_strcmp(dot, ".txt") != 0 &&
        rt_strcmp(dot, ".png") != 0 &&
        rt_strcmp(dot, ".jpg") != 0 &&
        rt_strcmp(dot, ".jpeg") != 0 &&
        rt_strcmp(dot, ".bin") != 0 &&
        rt_strcmp(dot, ".ttf") != 0 &&
        rt_strcmp(dot, ".otf") != 0 &&
        rt_strcmp(dot, ".lua") != 0)
    {
        return 0;
    }
    return vb_is_safe_package_path(path) &&
           (rt_strncmp(path, "assets/", 7) == 0 ||
            rt_strncmp(path, "images/", 7) == 0 ||
            rt_strncmp(path, "fonts/", 6) == 0 ||
            rt_strncmp(path, "lib/", 4) == 0);
}

static int vb_is_runtime_package_path(const char *path)
{
    return vb_is_core_package_path(path) ||
           rt_strcmp(path, "README.md") == 0 ||
           vb_is_resource_package_path(path);
}

static int vb_mkdir_recursive(const char *path)
{
    char work[VB_MAX_PATH];
    char *cursor;
    if (!path || !path[0]) return -RT_EINVAL;
    vb_safe_copy(work, sizeof(work), path);
    cursor = work;
    if (*cursor == '/') cursor++;
    for (; *cursor; cursor++)
    {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (work[0] && access(work, 0) != 0 && mkdir(work, 0) != 0)
        {
            *cursor = '/';
            return -RT_ERROR;
        }
        *cursor = '/';
    }
    if (access(work, 0) != 0 && mkdir(work, 0) != 0) return -RT_ERROR;
    return RT_EOK;
}

static int vb_prepare_filesystem(void)
{
    if (g_vb_runtime.fs_ready) return RT_EOK;

    if (vb_mkdir_recursive(VIBEBOARD_APP_ROOT) == RT_EOK)
    {
        g_vb_runtime.fs_ready = 1;
        return RT_EOK;
    }

#ifdef FS_REGION_START_ADDR
    if (!g_vb_runtime.fs_mounted)
    {
        register_mtd_device(FS_REGION_START_ADDR, FS_REGION_SIZE, VIBEBOARD_FS_DEVICE);
        if (dfs_mount(VIBEBOARD_FS_DEVICE, "/", "elm", 0, 0) != 0)
        {
            rt_kprintf("[vb_runtime] fs mount failed, formatting %s\n", VIBEBOARD_FS_DEVICE);
            if (dfs_mkfs("elm", VIBEBOARD_FS_DEVICE) == 0)
            {
                if (dfs_mount(VIBEBOARD_FS_DEVICE, "/", "elm", 0, 0) == 0)
                {
                    rt_kprintf("[vb_runtime] fs formatted and mounted\n");
                    g_vb_runtime.fs_mounted = 1;
                }
            }
        }
        else
        {
            rt_kprintf("[vb_runtime] fs mounted on /\n");
            g_vb_runtime.fs_mounted = 1;
        }
    }
#endif

    if (vb_mkdir_recursive(VIBEBOARD_APP_ROOT) == RT_EOK)
    {
        g_vb_runtime.fs_ready = 1;
        return RT_EOK;
    }

    rt_kprintf("[vb_runtime] fs unavailable: %s\n", VIBEBOARD_APP_ROOT);
    return -RT_ERROR;
}

static void vb_build_app_path(char *dst, rt_size_t cap, const char *app_id, const char *file)
{
    rt_snprintf(dst, cap, "%s/%s/%s", VIBEBOARD_APP_ROOT, app_id, file);
    dst[cap - 1] = '\0';
}

static int vb_read_text_file(const char *path, char *dst, rt_size_t cap)
{
    int fd;
    int len;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0) return -RT_ERROR;
    len = read(fd, dst, cap - 1);
    close(fd);
    if (len < 0) return -RT_ERROR;
    dst[len] = '\0';
    return len;
}

static int vb_write_text_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    int len;
    int written;
    if (fd < 0) return -RT_ERROR;
    len = rt_strlen(text);
    written = write(fd, text, len);
    close(fd);
    return written == len ? RT_EOK : -RT_ERROR;
}

static int vb_runtime_sensor_open_device(rt_device_t *slot, const char *name)
{
    rt_err_t result;
    if (!slot || !name) return 0;
    *slot = rt_device_find(name);
    if (!*slot) return 0;

    result = rt_device_open(*slot, RT_DEVICE_FLAG_RDONLY);
    if (result == RT_EOK || result == -RT_EBUSY)
    {
#if defined(RT_USING_SENSOR)
        rt_device_control(*slot, RT_SENSOR_CTRL_SET_POWER, (void *)RT_SENSOR_POWER_NORMAL);
#endif
        return 1;
    }

    rt_kprintf("[vb_runtime][sensor] open %s failed rc=%d\n", name, result);
    *slot = RT_NULL;
    return 0;
}

static int vb_runtime_sensor_probe_i2c(const char *bus_name, rt_uint16_t dev_addr,
                                       rt_uint16_t reg, const char *label)
{
#if defined(RT_USING_I2C)
    struct rt_i2c_bus_device *bus;
    rt_uint8_t value = 0;
    rt_size_t result;
    bus = rt_i2c_bus_device_find(bus_name);
    if (!bus)
    {
        rt_kprintf("[vb_runtime][sensor] probe %s failed: bus %s missing\n", label, bus_name);
        return 0;
    }
    result = rt_i2c_mem_read(bus, dev_addr, reg, 8, &value, 1);
    if (result <= 0)
    {
        rt_kprintf("[vb_runtime][sensor] probe %s failed: addr=0x%02x reg=0x%02x rc=%d\n",
                   label, dev_addr, reg, (int)result);
        return 0;
    }
    rt_kprintf("[vb_runtime][sensor] probe %s ok: addr=0x%02x reg=0x%02x value=0x%02x\n",
               label, dev_addr, reg, value);
    return 1;
#else
    (void)bus_name;
    (void)dev_addr;
    (void)reg;
    (void)label;
    return 0;
#endif
}

static int vb_runtime_sensors_init(void)
{
#if defined(RT_USING_SENSOR)
    struct rt_sensor_config cfg;
    int ltr303_present = 0;
    int mmc56x3_present = 0;
    int lsm6dsl_present = 0;

    if (g_vb_sensor.initialized) return g_vb_sensor.init_result;
    rt_memset(&g_vb_sensor, 0, sizeof(g_vb_sensor));
    g_vb_sensor.initialized = 1;
    g_vb_sensor.init_result = -RT_ENOSYS;

#if defined(BSP_USING_I2C3)
    HAL_PIN_Set(PAD_PA40, I2C3_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA39, I2C3_SDA, PIN_PULLUP, 1);
    g_vb_sensor.configured = 1;
#endif

    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.intf.dev_name = "i2c3";
    cfg.irq_pin.pin = RT_PIN_NONE;

#if defined(ASL_USING_LTR303)
    ltr303_present = vb_runtime_sensor_probe_i2c("i2c3", LTR303_I2CADDR_DEFAULT,
                                                 LTR303_PART_ID, "ltr303");
#endif
#if defined(MAG_USING_MMC56X3)
    mmc56x3_present = vb_runtime_sensor_probe_i2c("i2c3", MMC56X3_DEFAULT_ADDRESS,
                                                  MMC56X3_PRODUCT_ID, "mmc56x3");
#endif
#if defined(ACC_USING_LSM6DSL)
    lsm6dsl_present = vb_runtime_sensor_probe_i2c("i2c3", LSM6DSL_ADDR_DEFAULT,
                                                  0x0f, "lsm6dsl");
#endif

#if defined(ASL_USING_LTR303)
    if (ltr303_present && !rt_device_find("li_ltr3") && !rt_device_find("li_ltr303"))
    {
        rt_hw_ltr303_init("ltr303", &cfg);
    }
#endif
#if defined(MAG_USING_MMC56X3)
    if (mmc56x3_present && !rt_device_find("mag_mmc") && !rt_device_find("mag_mmc56x3"))
    {
        rt_hw_mmc56x3_init("mmc56x3", &cfg);
    }
#endif
#if defined(ACC_USING_LSM6DSL)
    if (lsm6dsl_present && !rt_device_find("acce_lsm") && !rt_device_find("gyro_lsm") && !rt_device_find("step_lsm"))
    {
        cfg.intf.user_data = (void *)LSM6DSL_ADDR_DEFAULT;
        rt_hw_lsm6dsl_init("lsm6d", &cfg);
    }
#endif

    g_vb_sensor.have_light = vb_runtime_sensor_open_device(&g_vb_sensor.light, "li_ltr3") ||
                             vb_runtime_sensor_open_device(&g_vb_sensor.light, "li_ltr303");
    g_vb_sensor.have_mag = vb_runtime_sensor_open_device(&g_vb_sensor.mag, "mag_mmc") ||
                           vb_runtime_sensor_open_device(&g_vb_sensor.mag, "mag_mmc56x3");
    g_vb_sensor.have_acce = vb_runtime_sensor_open_device(&g_vb_sensor.acce, "acce_lsm") ||
                            vb_runtime_sensor_open_device(&g_vb_sensor.acce, "acce_lsm6d");
    g_vb_sensor.have_gyro = vb_runtime_sensor_open_device(&g_vb_sensor.gyro, "gyro_lsm") ||
                            vb_runtime_sensor_open_device(&g_vb_sensor.gyro, "gyro_lsm6d");
    g_vb_sensor.have_step = vb_runtime_sensor_open_device(&g_vb_sensor.step, "step_lsm") ||
                            vb_runtime_sensor_open_device(&g_vb_sensor.step, "step_lsm6d");

    if (g_vb_sensor.acce) rt_device_control(g_vb_sensor.acce, RT_SENSOR_CTRL_SET_ODR, (void *)1660);
    if (g_vb_sensor.gyro) rt_device_control(g_vb_sensor.gyro, RT_SENSOR_CTRL_SET_ODR, (void *)1660);

    g_vb_sensor.ready = g_vb_sensor.have_light || g_vb_sensor.have_mag ||
                        g_vb_sensor.have_acce || g_vb_sensor.have_gyro ||
                        g_vb_sensor.have_step;
    g_vb_sensor.init_result = g_vb_sensor.ready ? RT_EOK : -RT_ERROR;
    rt_kprintf("[vb_runtime][sensor] init configured=%d ready=%d light=%d mag=%d acce=%d gyro=%d step=%d\n",
               g_vb_sensor.configured, g_vb_sensor.ready,
               g_vb_sensor.have_light, g_vb_sensor.have_mag,
               g_vb_sensor.have_acce, g_vb_sensor.have_gyro, g_vb_sensor.have_step);
    return g_vb_sensor.init_result;
#else
    g_vb_sensor.initialized = 1;
    g_vb_sensor.init_result = -RT_ENOSYS;
    g_vb_sensor.ready = 0;
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_sensor_read_one(rt_device_t dev, struct rt_sensor_data *out)
{
#if defined(RT_USING_SENSOR)
    if (!dev || !out) return 0;
    rt_memset(out, 0, sizeof(*out));
    return rt_device_read(dev, 0, out, 1) == 1;
#else
    (void)dev;
    (void)out;
    return 0;
#endif
}

static int vb_runtime_sensor_read_mmc56x3(struct rt_sensor_data *out)
{
#if defined(RT_USING_I2C) && defined(MAG_USING_MMC56X3)
    struct rt_i2c_bus_device *bus;
    rt_uint8_t buffer[9];
    rt_size_t result;
    rt_int32_t x;
    rt_int32_t y;
    rt_int32_t z;

    if (!out) return 0;
    bus = rt_i2c_bus_device_find("i2c3");
    if (!bus) return 0;
    rt_memset(buffer, 0, sizeof(buffer));
    result = rt_i2c_mem_read(bus, MMC56X3_DEFAULT_ADDRESS, MMC56X3_OUT_X_L, 8,
                             buffer, sizeof(buffer));
    if (result <= 0) return 0;

    x = ((rt_int32_t)buffer[0] << 12) | ((rt_int32_t)buffer[1] << 4) | ((rt_int32_t)buffer[6] >> 4);
    y = ((rt_int32_t)buffer[2] << 12) | ((rt_int32_t)buffer[3] << 4) | ((rt_int32_t)buffer[7] >> 4);
    z = ((rt_int32_t)buffer[4] << 12) | ((rt_int32_t)buffer[5] << 4) | ((rt_int32_t)buffer[8] >> 4);
    x -= (rt_int32_t)1 << 19;
    y -= (rt_int32_t)1 << 19;
    z -= (rt_int32_t)1 << 19;

    rt_memset(out, 0, sizeof(*out));
    out->type = RT_SENSOR_CLASS_MAG;
    out->data.mag.x = (rt_int32_t)(x / 16);
    out->data.mag.y = (rt_int32_t)(y / 16);
    out->data.mag.z = (rt_int32_t)(z / 16);
    out->timestamp = rt_sensor_get_ts();
    return 1;
#else
    (void)out;
    return 0;
#endif
}

static int vb_runtime_sensors_refresh(void)
{
    if (vb_runtime_sensors_init() != RT_EOK) return g_vb_sensor.init_result;
    g_vb_sensor.have_light = vb_runtime_sensor_read_one(g_vb_sensor.light, &g_vb_sensor.light_data);
    g_vb_sensor.have_mag = 0;
    if (g_vb_sensor.mag)
    {
        g_vb_sensor.have_mag = vb_runtime_sensor_read_mmc56x3(&g_vb_sensor.mag_data);
        if (!g_vb_sensor.have_mag)
        {
            g_vb_sensor.have_mag = vb_runtime_sensor_read_one(g_vb_sensor.mag, &g_vb_sensor.mag_data);
        }
    }
    g_vb_sensor.have_acce = vb_runtime_sensor_read_one(g_vb_sensor.acce, &g_vb_sensor.acce_data);
    g_vb_sensor.have_gyro = vb_runtime_sensor_read_one(g_vb_sensor.gyro, &g_vb_sensor.gyro_data);
    g_vb_sensor.have_step = vb_runtime_sensor_read_one(g_vb_sensor.step, &g_vb_sensor.step_data);
    g_vb_sensor.ready = g_vb_sensor.have_light || g_vb_sensor.have_mag ||
                        g_vb_sensor.have_acce || g_vb_sensor.have_gyro ||
                        g_vb_sensor.have_step;
    g_vb_sensor.read_count++;
    return g_vb_sensor.ready ? RT_EOK : -RT_ERROR;
}

static int vb_runtime_sensors_read_json(char *dst, rt_size_t cap)
{
    int result;
    int used;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    result = vb_runtime_sensors_refresh();
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"count\":%lu,"
                       "\"light\":{\"ok\":%d,\"lux\":%ld},"
                       "\"mag\":{\"ok\":%d,\"x\":%ld,\"y\":%ld,\"z\":%ld},"
                       "\"acce\":{\"ok\":%d,\"x\":%ld,\"y\":%ld,\"z\":%ld},"
                       "\"gyro\":{\"ok\":%d,\"x\":%ld,\"y\":%ld,\"z\":%ld},"
                       "\"step\":{\"ok\":%d,\"count\":%lu}}",
                       VIBEBOARD_RUNTIME_SENSOR_API_VERSION,
                       g_vb_sensor.init_result != -RT_ENOSYS ? 1 : 0,
                       g_vb_sensor.ready,
                       (unsigned long)g_vb_sensor.read_count,
                       g_vb_sensor.have_light,
                       (long)g_vb_sensor.light_data.data.light,
                       g_vb_sensor.have_mag,
                       (long)g_vb_sensor.mag_data.data.mag.x,
                       (long)g_vb_sensor.mag_data.data.mag.y,
                       (long)g_vb_sensor.mag_data.data.mag.z,
                       g_vb_sensor.have_acce,
                       (long)g_vb_sensor.acce_data.data.acce.x,
                       (long)g_vb_sensor.acce_data.data.acce.y,
                       (long)g_vb_sensor.acce_data.data.acce.z,
                       g_vb_sensor.have_gyro,
                       (long)g_vb_sensor.gyro_data.data.gyro.x,
                       (long)g_vb_sensor.gyro_data.data.gyro.y,
                       (long)g_vb_sensor.gyro_data.data.gyro.z,
                       g_vb_sensor.have_step,
                       (unsigned long)g_vb_sensor.step_data.data.step);
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_SENSOR_API_VERSION,
                    g_vb_sensor.init_result != -RT_ENOSYS ? 1 : 0,
                    g_vb_sensor.ready);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return result;
}

static void vb_runtime_print_json_line(const char *text)
{
    char chunk[97];
    rt_size_t len;
    rt_size_t offset = 0;

    if (!text)
    {
        rt_kprintf("\n");
        return;
    }

    len = rt_strlen(text);
    while (offset < len)
    {
        rt_size_t n = len - offset;
        if (n >= sizeof(chunk)) n = sizeof(chunk) - 1;
        rt_memcpy(chunk, text + offset, n);
        chunk[n] = '\0';
        rt_kprintf("%s", chunk);
        offset += n;
    }
    rt_kprintf("\n");
}

#if VB_RUNTIME_HAS_BLE_INSTALL
#ifndef SERIAL_UUID_16
#define SERIAL_UUID_16(x) {((uint8_t)((x) & 0xff)), ((uint8_t)((x) >> 8))}
#endif

enum vb_ble_install_att_list
{
    VB_BLE_INSTALL_SVC = 0,
    VB_BLE_INSTALL_CMD_CHAR,
    VB_BLE_INSTALL_CMD_VALUE,
    VB_BLE_INSTALL_STATUS_CHAR,
    VB_BLE_INSTALL_STATUS_VALUE,
    VB_BLE_INSTALL_STATUS_CCCD,
    VB_BLE_INSTALL_ATT_NB
};

#define VB_UUID128(a, b, c, d) { \
    0x56, 0x42, 0x52, 0x54, 0x49, 0x4e, 0x53, 0x54, \
    (uint8_t)(a), (uint8_t)(b), (uint8_t)(c), (uint8_t)(d), \
    0x52, 0x54, 0x4d, 0x45 \
}

static uint8_t g_vb_ble_install_svc_uuid[ATT_UUID_128_LEN] = VB_UUID128(0x00, 0x00, 0x00, 0x01);

BLE_GATT_SERVICE_DEFINE_128(vb_ble_install_att_db)
{
    BLE_GATT_SERVICE_DECLARE(VB_BLE_INSTALL_SVC, SERIAL_UUID_16_PRI_SERVICE, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_DECLARE(VB_BLE_INSTALL_CMD_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(VB_BLE_INSTALL_CMD_VALUE, VB_UUID128(0x00, 0x00, 0x00, 0x02),
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE |
                                BLE_GATT_PERM_WRITE_COMMAND_ENABLE,
                                BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE,
                                VB_BLE_MAX_COMMAND),
    BLE_GATT_CHAR_DECLARE(VB_BLE_INSTALL_STATUS_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(VB_BLE_INSTALL_STATUS_VALUE, VB_UUID128(0x00, 0x00, 0x00, 0x03),
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_NOTIFY_ENABLE,
                                BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE,
                                VB_BLE_STATUS_MAX),
    BLE_GATT_DESCRIPTOR_DECLARE(VB_BLE_INSTALL_STATUS_CCCD, SERIAL_UUID_16_CLIENT_CHAR_CFG,
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE,
                                BLE_GATT_VALUE_PERM_RI_ENABLE, 2),
};

SIBLES_ADVERTISING_CONTEXT_DECLAR(g_vb_ble_install_adv_context);

static void vb_ble_set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    rt_vsnprintf(g_vb_ble.status, sizeof(g_vb_ble.status), fmt, ap);
    va_end(ap);
    g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
    rt_kprintf("[vb_runtime][ble] %s\n", g_vb_ble.status);
}

static void vb_ble_notify_status(void)
{
    sibles_value_t value;
    if (!g_vb_ble.connected || !g_vb_ble.notify_cccd || !g_vb_ble.srv_handle) return;
    value.hdl = g_vb_ble.srv_handle;
    value.idx = VB_BLE_INSTALL_STATUS_VALUE;
    value.len = (uint16_t)rt_strlen(g_vb_ble.status);
    value.value = (uint8_t *)g_vb_ble.status;
    sibles_write_value(g_vb_ble.conn_idx, &value);
}

static uint8_t *vb_ble_gatts_get_cbk(uint8_t conn_idx, uint8_t idx, uint16_t *len)
{
    (void)conn_idx;
    switch (idx)
    {
    case VB_BLE_INSTALL_CMD_VALUE:
        *len = (uint16_t)rt_strlen(VIBEBOARD_RUNTIME_BLE_API_VERSION);
        return (uint8_t *)VIBEBOARD_RUNTIME_BLE_API_VERSION;
    case VB_BLE_INSTALL_STATUS_VALUE:
        *len = (uint16_t)rt_strlen(g_vb_ble.status);
        return (uint8_t *)g_vb_ble.status;
    case VB_BLE_INSTALL_STATUS_CCCD:
        *len = sizeof(g_vb_ble.notify_cccd);
        return (uint8_t *)&g_vb_ble.notify_cccd;
    default:
        *len = 0;
        return RT_NULL;
    }
}

static int vb_ble_tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (*p)
        {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

static int vb_ble_execute_line(char *line)
{
    char *argv[8];
    int argc = vb_ble_tokenize(line, argv, sizeof(argv) / sizeof(argv[0]));

    if (argc == 0) return RT_EOK;
    if (rt_strcmp(argv[0], "status") == 0 || rt_strcmp(argv[0], "vb_runtime_status") == 0)
    {
        char active[VB_MAX_APP_ID];
        vb_read_active_app(active, sizeof(active));
        vb_ble_set_status("ok status api=%s active=%s", VIBEBOARD_RUNTIME_BLE_API_VERSION,
                          active[0] ? active : "(unknown)");
        return RT_EOK;
    }
    if (rt_strcmp(argv[0], "sensors") == 0 || rt_strcmp(argv[0], "vb_runtime_sensors") == 0)
    {
        int result = vb_runtime_sensors_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] sensors rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_install_begin") == 0 && argc >= 2)
    {
        int result = vb_runtime_install_begin_app(argv[1]);
        vb_ble_set_status("%s install_begin %s rc=%d", result == RT_EOK ? "ok" : "err", argv[1], result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_install_file") == 0 && argc >= 5)
    {
        int result = vb_runtime_install_file_chunk(argv[1], argv[2], argv[3], argv[4]);
        vb_ble_set_status("%s install_file %s/%s rc=%d", result == RT_EOK ? "ok" : "err", argv[1], argv[2], result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_install_end") == 0 && argc >= 2)
    {
        int result = vb_runtime_install_end_app(argv[1]);
        vb_ble_set_status("%s install_end %s rc=%d", result == RT_EOK ? "ok" : "err", argv[1], result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_select") == 0 && argc >= 2)
    {
        int result = vb_runtime_select_app(argv[1]);
        vb_ble_set_status("%s select %s rc=%d", result == RT_EOK ? "ok" : "err", argv[1], result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_reload") == 0)
    {
        vb_runtime_request_reload();
        vb_ble_set_status("ok reload pending");
        return RT_EOK;
    }

    vb_ble_set_status("err unknown_command %s", argv[0]);
    return -RT_EINVAL;
}

static void vb_ble_receive_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        char ch = (char)data[i];
        if (ch == '\r' || ch == '\n')
        {
            if (g_vb_ble.rx_len > 0)
            {
                g_vb_ble.rx_line[g_vb_ble.rx_len] = '\0';
                vb_ble_execute_line(g_vb_ble.rx_line);
                vb_ble_notify_status();
                g_vb_ble.rx_len = 0;
            }
            continue;
        }
        if (g_vb_ble.rx_len >= VB_BLE_MAX_COMMAND)
        {
            g_vb_ble.rx_len = 0;
            vb_ble_set_status("err command_too_long");
            vb_ble_notify_status();
            continue;
        }
        g_vb_ble.rx_line[g_vb_ble.rx_len++] = ch;
    }
}

static uint8_t vb_ble_gatts_set_cbk(uint8_t conn_idx, sibles_set_cbk_t *para)
{
    (void)conn_idx;
    if (!para) return 1;
    switch (para->idx)
    {
    case VB_BLE_INSTALL_CMD_VALUE:
        vb_ble_receive_bytes(para->value, para->len);
        break;
    case VB_BLE_INSTALL_STATUS_CCCD:
        if (para->value && para->len >= 2)
        {
            g_vb_ble.notify_cccd = (uint16_t)para->value[0] | ((uint16_t)para->value[1] << 8);
            vb_ble_set_status("ok notify=%d", g_vb_ble.notify_cccd ? 1 : 0);
            vb_ble_notify_status();
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t vb_ble_advertising_event(uint8_t event, void *context, void *data)
{
    (void)context;
    switch (event)
    {
    case SIBLES_ADV_EVT_ADV_STARTED:
    {
        sibles_adv_evt_startted_t *evt = (sibles_adv_evt_startted_t *)data;
        g_vb_ble.advertising = evt && evt->status == 0;
        vb_ble_set_status("adv started name=%s status=%d mode=%d",
                          VIBEBOARD_BLE_NAME, evt ? evt->status : -1, evt ? evt->adv_mode : -1);
        break;
    }
    case SIBLES_ADV_EVT_ADV_STOPPED:
    {
        sibles_adv_evt_stopped_t *evt = (sibles_adv_evt_stopped_t *)data;
        g_vb_ble.advertising = 0;
        vb_ble_set_status("adv stopped reason=%d mode=%d",
                          evt ? evt->reason : -1, evt ? evt->adv_mode : -1);
        break;
    }
    default:
        break;
    }
    return 0;
}

static void vb_ble_service_init(void)
{
    if (g_vb_ble.service_ready) return;

    BLE_GATT_SERVICE_INIT_128(svc, vb_ble_install_att_db, VB_BLE_INSTALL_ATT_NB,
                              BLE_GATT_SERVICE_PERM_NOAUTH | BLE_GATT_SERVICE_PERM_UUID_128 |
                              BLE_GATT_SERVICE_PERM_MULTI_LINK,
                              g_vb_ble_install_svc_uuid);
    g_vb_ble.srv_handle = sibles_register_svc_128(&svc);
    if (g_vb_ble.srv_handle)
    {
        sibles_register_cbk(g_vb_ble.srv_handle, vb_ble_gatts_get_cbk, vb_ble_gatts_set_cbk);
        g_vb_ble.service_ready = 1;
        vb_ble_set_status("service ready uuid=56425254-494e-5354-0000-000152544d45");
    }
    else
    {
        vb_ble_set_status("service register failed");
    }
}

static void vb_ble_advertising_start(void)
{
    sibles_advertising_para_t para = {0};
    uint8_t ret;
    uint8_t manu_data[] = {'V', 'B', 'R', 'T', 1};
    ble_gap_dev_name_t *dev_name;

    dev_name = rt_malloc(sizeof(ble_gap_dev_name_t) + rt_strlen(VIBEBOARD_BLE_NAME));
    if (dev_name)
    {
        dev_name->len = rt_strlen(VIBEBOARD_BLE_NAME);
        rt_memcpy(dev_name->name, VIBEBOARD_BLE_NAME, dev_name->len);
        ble_gap_set_dev_name(dev_name);
        rt_free(dev_name);
    }

    para.own_addr_type = GAPM_STATIC_ADDR;
    para.config.adv_mode = SIBLES_ADV_CONNECT_MODE;
    para.config.mode_config.conn_config.duration = 0;
    para.config.mode_config.conn_config.interval = 0x30;
    para.config.max_tx_pwr = 0x7F;
    para.config.is_auto_restart = 1;

    para.adv_data.completed_uuid = rt_malloc(sizeof(sibles_adv_type_srv_uuid_t) + sizeof(sibles_adv_uuid_t));
    para.rsp_data.completed_name = rt_malloc(sizeof(sibles_adv_type_name_t) + rt_strlen(VIBEBOARD_BLE_NAME));
    para.adv_data.manufacturer_data = rt_malloc(sizeof(sibles_adv_type_manufacturer_data_t) + sizeof(manu_data));
    if (!para.adv_data.completed_uuid || !para.rsp_data.completed_name || !para.adv_data.manufacturer_data)
    {
        vb_ble_set_status("adv alloc failed");
        goto cleanup;
    }

    para.adv_data.completed_uuid->count = 1;
    para.adv_data.completed_uuid->uuid_list[0].uuid_len = ATT_UUID_128_LEN;
    rt_memcpy(para.adv_data.completed_uuid->uuid_list[0].uuid.uuid_128,
              g_vb_ble_install_svc_uuid, ATT_UUID_128_LEN);

    para.rsp_data.completed_name->name_len = rt_strlen(VIBEBOARD_BLE_NAME);
    rt_memcpy(para.rsp_data.completed_name->name, VIBEBOARD_BLE_NAME,
              para.rsp_data.completed_name->name_len);

    para.adv_data.manufacturer_data->company_id = SIG_SIFLI_COMPANY_ID;
    para.adv_data.manufacturer_data->data_len = sizeof(manu_data);
    rt_memcpy(para.adv_data.manufacturer_data->additional_data, manu_data, sizeof(manu_data));

    para.evt_handler = vb_ble_advertising_event;
    ret = sibles_advertising_init(g_vb_ble_install_adv_context, &para);
    if (ret == SIBLES_ADV_NO_ERR)
    {
        ret = sibles_advertising_start(g_vb_ble_install_adv_context);
        vb_ble_set_status("adv start requested name=%s rc=%d", VIBEBOARD_BLE_NAME, ret);
    }
    else
    {
        vb_ble_set_status("adv init failed rc=%d", ret);
    }

cleanup:
    if (para.adv_data.completed_uuid) rt_free(para.adv_data.completed_uuid);
    if (para.rsp_data.completed_name) rt_free(para.rsp_data.completed_name);
    if (para.adv_data.manufacturer_data) rt_free(para.adv_data.manufacturer_data);
}

static void vb_ble_worker_entry(void *parameter)
{
    uint32_t value;
    (void)parameter;
    while (1)
    {
        if (!g_vb_ble.mailbox ||
            rt_mb_recv(g_vb_ble.mailbox, (rt_uint32_t *)&value, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        if (value == BLE_POWER_ON_IND)
        {
            g_vb_ble.power_on = 1;
            g_vb_ble.mtu = 23;
            vb_ble_service_init();
            vb_ble_advertising_start();
        }
    }
}

static int vb_ble_install_init(void)
{
    rt_thread_t thread;
    if (g_vb_ble.initialized) return RT_EOK;
    rt_memset(&g_vb_ble, 0, sizeof(g_vb_ble));
    g_vb_ble.conn_idx = INVALID_CONN_IDX;
    vb_ble_set_status("init");
    g_vb_ble.mailbox = rt_mb_create("vb_ble", 8, RT_IPC_FLAG_FIFO);
    if (!g_vb_ble.mailbox)
    {
        rt_kprintf("[vb_runtime][ble] init failed: mailbox\n");
        return -RT_ERROR;
    }
    thread = rt_thread_create("vb_ble", vb_ble_worker_entry, RT_NULL, 4096,
                              RT_THREAD_PRIORITY_MIDDLE + 2, RT_THREAD_TICK_DEFAULT);
    if (!thread)
    {
        rt_mb_delete(g_vb_ble.mailbox);
        g_vb_ble.mailbox = RT_NULL;
        rt_kprintf("[vb_runtime][ble] init failed: thread\n");
        return -RT_ERROR;
    }
    rt_thread_startup(thread);
    vb_bt_core_enable_once();
    g_vb_ble.initialized = 1;
    rt_kprintf("[vb_runtime][ble] init name=%s api=%s\n",
               VIBEBOARD_BLE_NAME, VIBEBOARD_RUNTIME_BLE_API_VERSION);
    return RT_EOK;
}

static int vb_ble_startup_init(void)
{
    return vb_ble_install_init();
}
INIT_APP_EXPORT(vb_ble_startup_init);

static int vb_ble_event_handler(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    (void)len;
    (void)context;
    switch (event_id)
    {
    case BLE_POWER_ON_IND:
        if (g_vb_ble.mailbox)
        {
            rt_mb_send(g_vb_ble.mailbox, BLE_POWER_ON_IND);
        }
        break;
    case BLE_GAP_CONNECTED_IND:
    {
        ble_gap_connect_ind_t *ind = (ble_gap_connect_ind_t *)data;
        if (ind)
        {
            g_vb_ble.conn_idx = ind->conn_idx;
            g_vb_ble.connected = 1;
            g_vb_ble.advertising = 0;
            vb_ble_set_status("connected idx=%d interval=%d", ind->conn_idx, ind->con_interval);
        }
        break;
    }
    case BLE_GAP_DISCONNECTED_IND:
    {
        ble_gap_disconnected_ind_t *ind = (ble_gap_disconnected_ind_t *)data;
        g_vb_ble.connected = 0;
        g_vb_ble.notify_cccd = 0;
        g_vb_ble.conn_idx = INVALID_CONN_IDX;
        g_vb_ble.rx_len = 0;
        vb_ble_set_status("disconnected reason=%d", ind ? ind->reason : -1);
        break;
    }
    case SIBLES_MTU_EXCHANGE_IND:
    {
        sibles_mtu_exchange_ind_t *ind = (sibles_mtu_exchange_ind_t *)data;
        if (ind)
        {
            g_vb_ble.mtu = ind->mtu;
            vb_ble_set_status("mtu=%d", ind->mtu);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}
BLE_EVENT_REGISTER(vb_ble_event_handler, NULL);
#else
static int vb_ble_install_init(void)
{
    rt_kprintf("[vb_runtime][ble] unavailable: not built\n");
    return -RT_ENOSYS;
}
#endif

#if VB_RUNTIME_HAS_BT_PAN
static void vb_pan_print_mac(const char *prefix, const bt_notify_device_mac_t *mac)
{
    if (!mac) return;
    rt_kprintf("%s%02x:%02x:%02x:%02x:%02x:%02x\n", prefix,
               mac->addr[5], mac->addr[4], mac->addr[3],
               mac->addr[2], mac->addr[1], mac->addr[0]);
}

static int vb_pan_parse_hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int vb_pan_parse_mac(const char *text, bt_notify_device_mac_t *mac)
{
    uint8_t bytes[6];
    int byte_index = 0;
    int high = -1;

    if (!text || !mac) return -RT_EINVAL;
    rt_memset(bytes, 0, sizeof(bytes));

    while (*text)
    {
        int value;
        if (*text == ':' || *text == '-' || *text == ' ')
        {
            text++;
            continue;
        }
        value = vb_pan_parse_hex_nibble(*text);
        if (value < 0) return -RT_EINVAL;
        if (high < 0)
        {
            high = value;
        }
        else
        {
            if (byte_index >= 6) return -RT_EINVAL;
            bytes[byte_index++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
        text++;
    }

    if (high >= 0 || byte_index != 6) return -RT_EINVAL;
    for (int i = 0; i < 6; i++)
    {
        mac->addr[i] = bytes[5 - i];
    }
    return RT_EOK;
}

static void vb_pan_connect_timer_cb(void *parameter)
{
    (void)parameter;
    if (g_vb_pan.mailbox && g_vb_pan.bt_connected)
    {
        rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_CONNECT_PAN);
    }
}

static void vb_pan_open_timer_cb(void *parameter)
{
    (void)parameter;
    if (g_vb_pan.mailbox && !g_vb_pan.bt_opened)
    {
        rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_OPEN_BT);
    }
}

static void vb_pan_schedule_connect(void)
{
    if (!g_vb_pan.connect_timer)
    {
        g_vb_pan.connect_timer = rt_timer_create("vb_pan", vb_pan_connect_timer_cb, RT_NULL,
                                                 rt_tick_from_millisecond(VB_PAN_TIMER_MS),
                                                 RT_TIMER_FLAG_SOFT_TIMER);
    }
    else
    {
        rt_timer_stop(g_vb_pan.connect_timer);
    }
    if (g_vb_pan.connect_timer)
    {
        rt_timer_start(g_vb_pan.connect_timer);
    }
}

static void vb_pan_schedule_open(void)
{
    if (g_vb_pan.bt_opened || g_vb_pan.open_retries >= VB_PAN_OPEN_MAX_RETRY)
    {
        return;
    }
    if (!g_vb_pan.open_timer)
    {
        g_vb_pan.open_timer = rt_timer_create("vb_btop", vb_pan_open_timer_cb, RT_NULL,
                                              rt_tick_from_millisecond(VB_PAN_OPEN_RETRY_MS),
                                              RT_TIMER_FLAG_SOFT_TIMER);
    }
    else
    {
        rt_timer_stop(g_vb_pan.open_timer);
    }
    if (g_vb_pan.open_timer)
    {
        rt_timer_start(g_vb_pan.open_timer);
    }
}

static int vb_pan_bt_event_handle(uint16_t type, uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    if (type == BT_NOTIFY_COMMON)
    {
        int pan_conn = 0;
        switch (event_id)
        {
        case BT_NOTIFY_COMMON_BT_STACK_READY:
            g_vb_pan.stack_ready = 1;
            rt_kprintf("[vb_runtime][pan] stack ready\n");
            if (g_vb_pan.mailbox)
            {
                rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_STACK_READY);
            }
            break;
        case BT_NOTIFY_COMMON_LOCAL_NAME_RSP:
            if (data)
            {
                char name[32];
                int copy_len = data_len < sizeof(name) - 1 ? data_len : sizeof(name) - 1;
                rt_memcpy(name, data, copy_len);
                name[copy_len] = '\0';
                g_vb_pan.name_confirmed = 1;
                rt_kprintf("[vb_runtime][pan] local name rsp: %s\n", name);
            }
            break;
        case BT_NOTIFY_COMMON_LOCAL_ADDR_RSP:
            if (data && data_len >= sizeof(g_vb_pan.local_addr.addr))
            {
                rt_memcpy(g_vb_pan.local_addr.addr, data, sizeof(g_vb_pan.local_addr.addr));
                vb_pan_print_mac("[vb_runtime][pan] local addr: ", &g_vb_pan.local_addr);
            }
            break;
        case BT_NOTIFY_COMMON_SCAN_ENB_CFM_IND:
            if (data && data_len >= 1)
            {
                g_vb_pan.scan_mode = data[0];
                g_vb_pan.scan_confirmed = 1;
                rt_kprintf("[vb_runtime][pan] scan mode confirmed: %d\n", g_vb_pan.scan_mode);
            }
            break;
        case BT_NOTIFY_COMMON_DISCOVER_IND:
            if (data && data_len >= sizeof(bt_notify_remote_device_info_t))
            {
                bt_notify_remote_device_info_t *info = (bt_notify_remote_device_info_t *)data;
                g_vb_pan.inquiry_count++;
                rt_kprintf("[vb_runtime][pan] inquiry found #%d ", g_vb_pan.inquiry_count);
                rt_kprintf("%02x:%02x:%02x:%02x:%02x:%02x class=0x%06lx rssi=%d name=%s\n",
                           info->mac.addr[5], info->mac.addr[4], info->mac.addr[3],
                           info->mac.addr[2], info->mac.addr[1], info->mac.addr[0],
                           (unsigned long)info->dev_cls, info->rssi,
                           info->bt_name[0] ? info->bt_name : "(unknown)");
            }
            break;
        case BT_NOTIFY_COMMON_INQUIRY_CMP:
            g_vb_pan.inquiry_running = 0;
            rt_kprintf("[vb_runtime][pan] inquiry complete: %d device(s)\n", g_vb_pan.inquiry_count);
            break;
        case BT_NOTIFY_COMMON_ACL_CONNECTED:
            if (data)
            {
                bt_notify_device_acl_conn_info_t *info = (bt_notify_device_acl_conn_info_t *)data;
                g_vb_pan.bd_addr = info->mac;
                g_vb_pan.bt_connected = 1;
                vb_pan_print_mac("[vb_runtime][pan] acl connected: ", &g_vb_pan.bd_addr);
            }
            break;
        case BT_NOTIFY_COMMON_ACL_DISCONNECTED:
            if (data)
            {
                bt_notify_device_base_info_t *info = (bt_notify_device_base_info_t *)data;
                rt_kprintf("[vb_runtime][pan] acl disconnected res=%d\n", info->res);
            }
            g_vb_pan.bt_connected = 0;
            g_vb_pan.pan_connected = 0;
            g_vb_pan.connecting = 0;
            g_vb_pan.pairing_pending = 0;
            rt_memset(&g_vb_pan.bd_addr, 0xFF, sizeof(g_vb_pan.bd_addr));
            if (g_vb_pan.connect_timer)
            {
                rt_timer_stop(g_vb_pan.connect_timer);
            }
            break;
        case BT_NOTIFY_COMMON_IO_CAPABILITY_IND:
            if (data && data_len >= sizeof(g_vb_pan.bd_addr.addr))
            {
                rt_memcpy(g_vb_pan.bd_addr.addr, data, sizeof(g_vb_pan.bd_addr.addr));
                g_vb_pan.pairing_pending = 1;
                vb_pan_print_mac("[vb_runtime][pan] io capability request: ", &g_vb_pan.bd_addr);
                bt_interface_io_req_res((unsigned char *)&g_vb_pan.bd_addr,
                                        IO_CAPABILITY_NO_INPUT_NO_OUTPUT, 0, 1);
                rt_kprintf("[vb_runtime][pan] io capability accepted\n");
            }
            break;
        case BT_NOTIFY_COMMON_USER_CONFIRM_IND:
            if (data && data_len >= sizeof(bt_notify_pair_confirm_t))
            {
                bt_notify_pair_confirm_t *info = (bt_notify_pair_confirm_t *)data;
                g_vb_pan.bd_addr = info->mac;
                g_vb_pan.pair_code = info->num_val;
                g_vb_pan.pairing_pending = 1;
                vb_pan_print_mac("[vb_runtime][pan] user confirm request: ", &g_vb_pan.bd_addr);
                rt_kprintf("[vb_runtime][pan] pair code=%lu auto_confirm=1\n",
                           (unsigned long)g_vb_pan.pair_code);
                bt_interface_user_confirm_res((unsigned char *)&g_vb_pan.bd_addr, 1);
            }
            break;
        case BT_NOTIFY_COMMON_ENCRYPTION:
            if (data)
            {
                bt_notify_device_mac_t *mac = (bt_notify_device_mac_t *)data;
                g_vb_pan.bd_addr = *mac;
                pan_conn = 1;
                rt_kprintf("[vb_runtime][pan] encryption complete\n");
            }
            break;
        case BT_NOTIFY_COMMON_PAIR_IND:
            if (data)
            {
                bt_notify_device_base_info_t *info = (bt_notify_device_base_info_t *)data;
                rt_kprintf("[vb_runtime][pan] pair complete res=%d\n", info->res);
                g_vb_pan.pairing_pending = 0;
                if (info->res == BTS2_SUCC)
                {
                    g_vb_pan.bd_addr = info->mac;
                    pan_conn = 1;
                }
                else
                {
                    g_vb_pan.last_error = info->res;
                }
            }
            break;
        case BT_NOTIFY_COMMON_KEY_MISSING:
            if (data)
            {
                bt_notify_device_base_info_t *info = (bt_notify_device_base_info_t *)data;
                rt_kprintf("[vb_runtime][pan] key missing res=%d\n", info->res);
#ifdef BSP_BT_CONNECTION_MANAGER
                bt_cm_delete_bonded_devs_and_linkkey(info->mac.addr);
#endif
            }
            rt_memset(&g_vb_pan.bd_addr, 0xFF, sizeof(g_vb_pan.bd_addr));
            g_vb_pan.bt_connected = 0;
            g_vb_pan.pan_connected = 0;
            g_vb_pan.connecting = 0;
            g_vb_pan.pairing_pending = 0;
            break;
        default:
            break;
        }

        if (pan_conn)
        {
            g_vb_pan.bt_connected = 1;
            vb_pan_print_mac("[vb_runtime][pan] paired device: ", &g_vb_pan.bd_addr);
            vb_pan_schedule_connect();
        }
    }
    else if (type == BT_NOTIFY_PAN)
    {
        switch (event_id)
        {
        case BT_NOTIFY_PAN_PROFILE_CONNECTED:
            g_vb_pan.pan_connected = 1;
            g_vb_pan.connecting = 0;
            g_vb_pan.last_error = 0;
            if (g_vb_pan.connect_timer)
            {
                rt_timer_stop(g_vb_pan.connect_timer);
            }
            rt_kprintf("[vb_runtime][pan] connected\n");
            break;
        case BT_NOTIFY_PAN_PROFILE_DISCONNECTED:
            g_vb_pan.pan_connected = 0;
            g_vb_pan.connecting = 0;
            rt_kprintf("[vb_runtime][pan] disconnected\n");
            break;
        default:
            break;
        }
    }
    return 0;
}

uint32_t bt_get_class_of_device(void)
{
    return (uint32_t)BT_SRVCLS_NETWORK | BT_DEVCLS_LAP | BT_LAP_FULLY;
}

static void vb_pan_worker_entry(void *parameter)
{
    uint32_t value;
    (void)parameter;

    while (1)
    {
        if (!g_vb_pan.mailbox ||
            rt_mb_recv(g_vb_pan.mailbox, (rt_uint32_t *)&value, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        if (value == VB_PAN_EVT_STACK_READY)
        {
            if (!g_vb_pan.final_name_requested)
            {
                bt_interface_set_local_name((int)rt_strlen(g_vb_pan.local_name),
                                            (void *)g_vb_pan.local_name);
                g_vb_pan.final_name_requested = 1;
                rt_kprintf("[vb_runtime][pan] final name requested: %s\n", g_vb_pan.local_name);
            }
            bt_interface_rd_local_bd_addr();
            if (!g_vb_pan.bt_opened)
            {
                bt_interface_open_bt();
                g_vb_pan.bt_opened = 1;
                rt_kprintf("[vb_runtime][pan] classic bt open requested\n");
            }
            rt_kprintf("[vb_runtime][pan] name=%s\n", g_vb_pan.local_name);
        }
        else if (value == VB_PAN_EVT_OPEN_BT)
        {
            if (g_vb_pan.bt_opened)
            {
                continue;
            }
            if (!getApp())
            {
                g_vb_pan.open_retries++;
                rt_kprintf("[vb_runtime][pan] waiting for BTS2 app before open (%d/%d)\n",
                           g_vb_pan.open_retries, VB_PAN_OPEN_MAX_RETRY);
                vb_pan_schedule_open();
                continue;
            }
            if (!g_vb_pan.name_requested)
            {
                bt_interface_set_local_name((int)rt_strlen(g_vb_pan.local_name),
                                            (void *)g_vb_pan.local_name);
                g_vb_pan.name_requested = 1;
                g_vb_pan.open_retries++;
                rt_kprintf("[vb_runtime][pan] local name requested: %s\n", g_vb_pan.local_name);
                vb_pan_schedule_open();
                continue;
            }
            if (!g_vb_pan.stack_ready)
            {
                g_vb_pan.open_retries++;
                rt_kprintf("[vb_runtime][pan] waiting for stack ready after name (%d/%d)\n",
                           g_vb_pan.open_retries, VB_PAN_OPEN_MAX_RETRY);
                vb_pan_schedule_open();
                continue;
            }
            bt_interface_open_bt();
            g_vb_pan.bt_opened = 1;
            rt_kprintf("[vb_runtime][pan] classic bt open requested\n");
        }
        else if (value == VB_PAN_EVT_PROBE)
        {
            if (!getApp())
            {
                rt_kprintf("[vb_runtime][pan] probe skipped: BTS2 app not ready\n");
                continue;
            }
            g_vb_pan.scan_mode = bt_interface_get_current_scan_mode();
            g_vb_pan.target_scan_mode = bt_interface_get_target_scan_mode();
            g_vb_pan.scan_mode_fsm = bt_interface_get_scan_mode_fsm();
            rt_kprintf("[vb_runtime][pan] probe ready=%d opened=%d name_req=%d name_ok=%d scan=%d target=%d fsm=%d\n",
                       g_vb_pan.stack_ready, g_vb_pan.bt_opened,
                       g_vb_pan.name_requested, g_vb_pan.name_confirmed,
                       g_vb_pan.scan_mode, g_vb_pan.target_scan_mode,
                       g_vb_pan.scan_mode_fsm);
        }
        else if (value == VB_PAN_EVT_CONNECT_PAN)
        {
            if (!g_vb_pan.bt_connected)
            {
                rt_kprintf("[vb_runtime][pan] connect skipped: no paired ACL\n");
                continue;
            }
            if (g_vb_pan.pan_connected)
            {
                rt_kprintf("[vb_runtime][pan] already connected\n");
                continue;
            }
            g_vb_pan.connecting = 1;
            vb_pan_print_mac("[vb_runtime][pan] connecting PAN to ", &g_vb_pan.bd_addr);
            bt_interface_conn_ext((unsigned char *)&g_vb_pan.bd_addr, BT_PROFILE_PAN);
        }
        else if (value == VB_PAN_EVT_INQUIRY)
        {
            bt_start_inquiry_ex_t inquiry;
            if (!getApp())
            {
                rt_kprintf("[vb_runtime][pan] inquiry skipped: BTS2 app not ready\n");
                continue;
            }
            if (!g_vb_pan.stack_ready)
            {
                rt_kprintf("[vb_runtime][pan] inquiry deferred: stack not ready\n");
                rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_OPEN_BT);
                continue;
            }
            if (g_vb_pan.inquiry_running)
            {
                rt_kprintf("[vb_runtime][pan] inquiry already running\n");
                continue;
            }
            g_vb_pan.inquiry_count = 0;
            g_vb_pan.inquiry_running = 1;
            inquiry.dev_cls_mask = 0;
            inquiry.max_timeout = 20;
            inquiry.max_rsp = 16;
            if (bt_interface_start_inquiry_ex(&inquiry) == BT_EOK)
            {
                rt_kprintf("[vb_runtime][pan] inquiry started: any device, timeout=%us\n",
                           inquiry.max_timeout);
            }
            else
            {
                g_vb_pan.inquiry_running = 0;
                rt_kprintf("[vb_runtime][pan] inquiry failed to start\n");
            }
        }
    }
}

static int vb_pan_init(void)
{
    bt_interface_status_t status;
    rt_thread_t thread;
    if (g_vb_pan.initialized)
    {
        return RT_EOK;
    }

    rt_memset(&g_vb_pan, 0, sizeof(g_vb_pan));
    rt_memset(&g_vb_pan.bd_addr, 0xFF, sizeof(g_vb_pan.bd_addr));
    rt_memset(&g_vb_pan.local_addr, 0xFF, sizeof(g_vb_pan.local_addr));
    vb_safe_copy(g_vb_pan.local_name, sizeof(g_vb_pan.local_name), VIBEBOARD_BT_PAN_NAME);

    g_vb_pan.mailbox = rt_mb_create("vb_pan", 8, RT_IPC_FLAG_FIFO);
    if (!g_vb_pan.mailbox)
    {
        rt_kprintf("[vb_runtime][pan] init failed: mailbox\n");
        return -RT_ERROR;
    }

    thread = rt_thread_create("vb_pan", vb_pan_worker_entry, RT_NULL, 4096,
                              RT_THREAD_PRIORITY_MIDDLE + 2, RT_THREAD_TICK_DEFAULT);
    if (!thread)
    {
        rt_mb_delete(g_vb_pan.mailbox);
        g_vb_pan.mailbox = RT_NULL;
        rt_kprintf("[vb_runtime][pan] init failed: thread\n");
        return -RT_ERROR;
    }

#ifdef BSP_BT_CONNECTION_MANAGER
    bt_cm_set_profile_target(BT_CM_HID, BT_LINK_PHONE, 1);
#endif
    status = bt_interface_register_bt_event_notify_callback(vb_pan_bt_event_handle);
    if (status != BT_INTERFACE_STATUS_OK && status != BT_INTERFACE_STATUS_ALREADY_EXIST)
    {
        rt_kprintf("[vb_runtime][pan] callback register failed: %d\n", status);
        rt_mb_delete(g_vb_pan.mailbox);
        g_vb_pan.mailbox = RT_NULL;
        return -RT_ERROR;
    }
    rt_thread_startup(thread);
    vb_bt_core_enable_once();
    vb_pan_schedule_open();
    g_vb_pan.initialized = 1;
    rt_kprintf("[vb_runtime][pan] init name=%s callback=%d open=scheduled\n", g_vb_pan.local_name, status);
    return RT_EOK;
}

static int vb_pan_open_now(void)
{
    int result = vb_pan_init();
    if (result != RT_EOK) return result;
    if (!g_vb_pan.mailbox) return -RT_ERROR;
    return rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_OPEN_BT);
}

static int vb_pan_scan_now(void)
{
    int result = vb_pan_init();
    if (result != RT_EOK) return result;
    if (!getApp())
    {
        rt_kprintf("[vb_runtime][pan] scan unavailable: BTS2 app not ready\n");
        return -RT_ERROR;
    }
    if (!g_vb_pan.stack_ready)
    {
        rt_kprintf("[vb_runtime][pan] scan deferred: stack not ready\n");
        return rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_OPEN_BT);
    }
    g_vb_pan.scan_mode = bt_interface_get_current_scan_mode();
    g_vb_pan.scan_mode_fsm = bt_interface_get_scan_mode_fsm();
    if (g_vb_pan.scan_mode_fsm)
    {
        rt_kprintf("[vb_runtime][pan] scan busy: fsm=%d\n", g_vb_pan.scan_mode_fsm);
        return -RT_EBUSY;
    }
    bt_interface_set_scan_mode(1, 1);
    g_vb_pan.target_scan_mode = 3;
    rt_kprintf("[vb_runtime][pan] scan requested: inquiry=1 page=1\n");
    return RT_EOK;
}

static int vb_pan_probe_now(void)
{
    int result = vb_pan_init();
    if (result != RT_EOK) return result;
    if (!g_vb_pan.mailbox) return -RT_ERROR;
    return rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_PROBE);
}

static int vb_pan_connect_now(void)
{
    int result = vb_pan_init();
    if (result != RT_EOK) return result;
    if (g_vb_pan.pan_connected)
    {
        rt_kprintf("[vb_runtime][pan] already connected\n");
        return RT_EOK;
    }
    if (!g_vb_pan.bt_connected)
    {
        rt_kprintf("[vb_runtime][pan] waiting for phone/Mac Bluetooth pairing with %s\n",
                   g_vb_pan.local_name);
        return -RT_ERROR;
    }
    if (g_vb_pan.mailbox)
    {
        rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_CONNECT_PAN);
    }
    return RT_EOK;
}

static int vb_pan_inquiry_now(void)
{
    int result = vb_pan_init();
    if (result != RT_EOK) return result;
    if (!g_vb_pan.mailbox) return -RT_ERROR;
    return rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_INQUIRY);
}

static int vb_pan_connect_addr_now(const char *mac_text)
{
    int result;
    bt_notify_device_mac_t mac;

    result = vb_pan_init();
    if (result != RT_EOK) return result;
    result = vb_pan_parse_mac(mac_text, &mac);
    if (result != RT_EOK)
    {
        rt_kprintf("[vb_runtime][pan] invalid mac: %s\n", mac_text ? mac_text : "(null)");
        return result;
    }
    if (!getApp())
    {
        rt_kprintf("[vb_runtime][pan] connect_addr unavailable: BTS2 app not ready\n");
        return -RT_ERROR;
    }
    if (!g_vb_pan.stack_ready)
    {
        rt_kprintf("[vb_runtime][pan] connect_addr deferred: stack not ready\n");
        return rt_mb_send(g_vb_pan.mailbox, VB_PAN_EVT_OPEN_BT);
    }
    g_vb_pan.bd_addr = mac;
    g_vb_pan.connecting = 1;
    vb_pan_print_mac("[vb_runtime][pan] connecting address: ", &g_vb_pan.bd_addr);
    bt_interface_conn_ext((unsigned char *)&g_vb_pan.bd_addr, BT_PROFILE_PAN);
    return RT_EOK;
}

static int vb_pan_forget(void)
{
    int result = vb_pan_init();
    if (result != RT_EOK) return result;
#ifdef BSP_BT_CONNECTION_MANAGER
    bt_cm_delete_bonded_devs();
    rt_memset(&g_vb_pan.bd_addr, 0xFF, sizeof(g_vb_pan.bd_addr));
    g_vb_pan.bt_connected = 0;
    g_vb_pan.pan_connected = 0;
    g_vb_pan.connecting = 0;
    rt_kprintf("[vb_runtime][pan] bonded devices cleared\n");
    return RT_EOK;
#else
    rt_kprintf("[vb_runtime][pan] forget unavailable: connection manager not built\n");
    return -RT_ENOSYS;
#endif
}
#else
static int vb_pan_init(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_connect_now(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_open_now(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_scan_now(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_probe_now(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_inquiry_now(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_connect_addr_now(const char *addr_text)
{
    (void)addr_text;
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}

static int vb_pan_forget(void)
{
    rt_kprintf("[vb_runtime][pan] unavailable: not built\n");
    return -RT_ENOSYS;
}
#endif

static int vb_path_is_dir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) return 0;
    closedir(dir);
    return 1;
}

static int vb_remove_tree(const char *path)
{
    DIR *dir;
    struct dirent *entry;
    int result = RT_EOK;

    if (!path || !path[0]) return -RT_EINVAL;
    dir = opendir(path);
    if (!dir)
    {
        return unlink(path) == 0 ? RT_EOK : -RT_ERROR;
    }

    while ((entry = readdir(dir)) != RT_NULL)
    {
        char child[VB_MAX_PATH];
        if (rt_strcmp(entry->d_name, ".") == 0 || rt_strcmp(entry->d_name, "..") == 0) continue;
        rt_snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        child[sizeof(child) - 1] = '\0';
        if (vb_path_is_dir(child))
        {
            if (vb_remove_tree(child) != RT_EOK) result = -RT_ERROR;
        }
        else if (unlink(child) != 0)
        {
            result = -RT_ERROR;
        }
    }
    closedir(dir);
    if (rmdir(path) != 0) result = -RT_ERROR;
    return result;
}

static void vb_build_install_dir(char *dst, rt_size_t cap, const char *prefix, const char *app_id)
{
    rt_snprintf(dst, cap, "%s/%s%s", VIBEBOARD_APP_ROOT, prefix, app_id);
    dst[cap - 1] = '\0';
}

static void vb_read_active_app(char *dst, rt_size_t cap)
{
    char text[VB_MAX_APP_ID + 4];
    int i;
    vb_safe_copy(dst, cap, VIBEBOARD_DEFAULT_APP_ID);
    if (vb_prepare_filesystem() != RT_EOK) return;
    if (vb_read_text_file(VIBEBOARD_ACTIVE_APP_FILE, text, sizeof(text)) < 0) return;
    for (i = 0; text[i]; i++)
    {
        if (text[i] == '\r' || text[i] == '\n' || text[i] == ' ' || text[i] == '\t')
        {
            text[i] = '\0';
            break;
        }
    }
    if (vb_is_safe_app_id(text)) vb_safe_copy(dst, cap, text);
}

static int vb_write_active_app(const char *app_id)
{
    char text[VB_MAX_APP_ID + 2];
    if (!vb_is_safe_app_id(app_id)) return -RT_EINVAL;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    rt_snprintf(text, sizeof(text), "%s\n", app_id);
    return vb_write_text_file(VIBEBOARD_ACTIVE_APP_FILE, text);
}

static int vb_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int vb_write_hex_chunk(int fd, const char *hex)
{
    uint8_t buffer[128];
    int count = 0;
    int i;
    int len;
    if (!hex || rt_strcmp(hex, "-") == 0) return RT_EOK;
    len = rt_strlen(hex);
    if ((len % 2) != 0 || len > VB_MAX_HEX_CHARS) return -RT_EINVAL;
    for (i = 0; i < len; i += 2)
    {
        int hi = vb_hex_value(hex[i]);
        int lo = vb_hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) return -RT_EINVAL;
        buffer[count++] = (uint8_t)((hi << 4) | lo);
        if (count == (int)sizeof(buffer))
        {
            if (write(fd, buffer, count) != count) return -RT_ERROR;
            count = 0;
        }
    }
    if (count > 0 && write(fd, buffer, count) != count) return -RT_ERROR;
    return RT_EOK;
}

static const char *vb_json_find_string(const char *begin, const char *end, const char *key)
{
    char needle[48];
    const char *cursor = begin;
    rt_snprintf(needle, sizeof(needle), "\"%s\"", key);
    while (cursor && (!end || cursor < end))
    {
        const char *hit = strstr(cursor, needle);
        if (!hit || (end && hit >= end)) return RT_NULL;
        hit += rt_strlen(needle);
        while ((!end || hit < end) && (*hit == ' ' || *hit == '\t' || *hit == '\r' || *hit == '\n')) hit++;
        if (*hit != ':')
        {
            cursor = hit;
            continue;
        }
        hit++;
        while ((!end || hit < end) && (*hit == ' ' || *hit == '\t' || *hit == '\r' || *hit == '\n')) hit++;
        if (*hit == '"') return hit + 1;
        cursor = hit;
    }
    return RT_NULL;
}

static void vb_json_copy_string(const char *begin, const char *end, const char *key,
                                char *dst, rt_size_t cap, const char *fallback)
{
    const char *src = vb_json_find_string(begin, end, key);
    rt_size_t used = 0;
    if (!src)
    {
        vb_safe_copy(dst, cap, fallback);
        return;
    }
    while ((!end || src < end) && *src && *src != '"' && used + 1 < cap)
    {
        if (*src == '\\' && src[1])
        {
            src++;
        }
        dst[used++] = *src++;
    }
    dst[used] = '\0';
}

static int vb_parse_manifest_components(const char *json)
{
    const char *components = strstr(json, "\"components\"");
    const char *array;
    const char *array_end;
    const char *cursor;
    int count = 0;
    if (!components) return 0;
    array = strchr(components, '[');
    if (!array) return 0;
    array_end = strchr(array, ']');
    if (!array_end) return 0;
    cursor = array;
    while (count < VB_MAX_COMPONENTS)
    {
        const char *obj = strchr(cursor, '{');
        const char *obj_end;
        vb_component_t *component;
        if (!obj || obj >= array_end) break;
        obj_end = strchr(obj, '}');
        if (!obj_end || obj_end > array_end) break;
        component = &g_vb_runtime.components[count];
        rt_memset(component, 0, sizeof(*component));
        vb_json_copy_string(obj, obj_end, "type", component->type, sizeof(component->type), "status");
        vb_json_copy_string(obj, obj_end, "capability", component->capability, sizeof(component->capability), "status");
        vb_json_copy_string(obj, obj_end, "label", component->label, sizeof(component->label), component->capability);
        vb_json_copy_string(obj, obj_end, "value", component->value, sizeof(component->value), "--");
        count++;
        cursor = obj_end + 1;
    }
    return count;
}

static const char *vb_skip_spaces(const char *text)
{
    while (text && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) text++;
    return text;
}

static int vb_is_ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int vb_parse_quoted(const char **cursor, char *dst, rt_size_t cap)
{
    const char *p = vb_skip_spaces(*cursor);
    rt_size_t used = 0;
    if (!p || (*p != '"' && *p != '\'')) return 0;
    char quote = *p++;
    while (*p && *p != quote)
    {
        char c = *p++;
        if (c == '\\' && *p)
        {
            char e = *p++;
            if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 't') c = '\t';
            else c = e;
        }
        if (used + 1 < cap) dst[used++] = c;
    }
    if (*p == quote) p++;
    dst[used] = '\0';
    *cursor = p;
    return 1;
}

static int vb_parse_number(const char **cursor, int *out)
{
    char *end;
    long value;
    const char *p = vb_skip_spaces(*cursor);
    if (!p || (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '+'))) return 0;
    value = strtol(p, &end, 0);
    if (end == p) return 0;
    *out = (int)value;
    *cursor = end;
    return 1;
}

static int vb_parse_name_token(const char **cursor, char *dst, rt_size_t cap)
{
    const char *p = vb_skip_spaces(*cursor);
    rt_size_t used = 0;
    if (!p || !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) return 0;
    while (vb_is_ident_char(*p))
    {
        if (used + 1 < cap) dst[used++] = *p;
        p++;
    }
    dst[used] = '\0';
    *cursor = p;
    return used > 0;
}

static int vb_split_args(const char *inside, char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG], int *count)
{
    const char *p = inside;
    int argc = 0;
    *count = 0;
    while (p && *p && argc < VB_MAX_SCRIPT_ARGS)
    {
        char value[VB_MAX_SCRIPT_ARG];
        int number;
        p = vb_skip_spaces(p);
        if (*p == ')') break;
        if (vb_parse_quoted(&p, value, sizeof(value)))
        {
            vb_safe_copy(args[argc++], VB_MAX_SCRIPT_ARG, value);
        }
        else if (vb_parse_number(&p, &number))
        {
            rt_snprintf(args[argc++], VB_MAX_SCRIPT_ARG, "%d", number);
        }
        else
        {
            char ident[VB_MAX_SCRIPT_NAME];
            if (!vb_parse_name_token(&p, ident, sizeof(ident))) break;
            vb_safe_copy(args[argc++], VB_MAX_SCRIPT_ARG, ident);
        }
        p = vb_skip_spaces(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        break;
    }
    *count = argc;
    return argc;
}

static int vb_extract_call_args(const char *line, const char *fn,
                                char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG], int *count)
{
    const char *hit = strstr(line, fn);
    const char *open;
    const char *close;
    char inside[VB_MAX_SCRIPT_LINE];
    rt_size_t len;
    if (!hit) return 0;
    hit += rt_strlen(fn);
    hit = vb_skip_spaces(hit);
    if (*hit != '(') return 0;
    open = hit + 1;
    close = strrchr(open, ')');
    if (!close || close <= open) return 0;
    len = close - open;
    if (len >= sizeof(inside)) len = sizeof(inside) - 1;
    memcpy(inside, open, len);
    inside[len] = '\0';
    vb_split_args(inside, args, count);
    return 1;
}

static int vb_script_name_valid(const char *name)
{
    const char *p = name;
    if (!name || !name[0]) return 0;
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) return 0;
    for (; *p; p++)
    {
        if (!vb_is_ident_char(*p)) return 0;
    }
    return 1;
}

static vb_script_object_t *vb_script_find_object(const char *name)
{
    int i;
    if (!name || rt_strcmp(name, "root") == 0) return &g_vb_runtime.script_objects[0];
    for (i = 0; i < g_vb_runtime.script_object_count; i++)
    {
        if (rt_strcmp(g_vb_runtime.script_objects[i].name, name) == 0) return &g_vb_runtime.script_objects[i];
    }
    return RT_NULL;
}

static lv_obj_t *vb_script_resolve_parent(const char *name)
{
    vb_script_object_t *found = vb_script_find_object(name);
    return (found && found->obj) ? found->obj : g_vb_runtime.root;
}

static vb_script_object_t *vb_script_store_object(const char *name, lv_obj_t *obj, vb_script_object_type_t type)
{
    vb_script_object_t *slot;
    if (!vb_script_name_valid(name) || !obj) return RT_NULL;
    slot = vb_script_find_object(name);
    if (!slot)
    {
        if (g_vb_runtime.script_object_count >= VB_MAX_SCRIPT_OBJECTS) return RT_NULL;
        slot = &g_vb_runtime.script_objects[g_vb_runtime.script_object_count++];
    }
    vb_safe_copy(slot->name, sizeof(slot->name), name);
    slot->obj = obj;
    slot->type = type;
    return slot;
}

static lv_align_t vb_script_align_from_arg(const char *arg)
{
    if (!arg) return LV_ALIGN_CENTER;
    if (rt_strcmp(arg, "LV_ALIGN_TOP_LEFT") == 0) return LV_ALIGN_TOP_LEFT;
    if (rt_strcmp(arg, "LV_ALIGN_TOP_MID") == 0) return LV_ALIGN_TOP_MID;
    if (rt_strcmp(arg, "LV_ALIGN_BOTTOM_LEFT") == 0) return LV_ALIGN_BOTTOM_LEFT;
    if (rt_strcmp(arg, "LV_ALIGN_BOTTOM_MID") == 0) return LV_ALIGN_BOTTOM_MID;
    if (rt_strcmp(arg, "LV_ALIGN_BOTTOM_RIGHT") == 0) return LV_ALIGN_BOTTOM_RIGHT;
    if (rt_strcmp(arg, "LV_ALIGN_TOP_RIGHT") == 0) return LV_ALIGN_TOP_RIGHT;
    if (rt_strcmp(arg, "LV_ALIGN_LEFT_MID") == 0) return LV_ALIGN_LEFT_MID;
    if (rt_strcmp(arg, "LV_ALIGN_RIGHT_MID") == 0) return LV_ALIGN_RIGHT_MID;
    return LV_ALIGN_CENTER;
}

static int vb_snake_index(int x, int y)
{
    if (x < 0 || y < 0 || x >= g_vb_runtime.snake.cols || y >= g_vb_runtime.snake.rows) return -1;
    return y * g_vb_runtime.snake.cols + x;
}

static int vb_snake_contains(int x, int y)
{
    int i;
    for (i = 0; i < g_vb_runtime.snake.length; i++)
    {
        if (g_vb_runtime.snake.snake_x[i] == x && g_vb_runtime.snake.snake_y[i] == y) return 1;
    }
    return 0;
}

static void vb_snake_place_food(void)
{
    int total = g_vb_runtime.snake.cols * g_vb_runtime.snake.rows;
    int start = (g_vb_runtime.snake.moves * 17 + g_vb_runtime.snake.score * 31 + 11) % total;
    int i;
    for (i = 0; i < total; i++)
    {
        int pos = (start + i) % total;
        int x = pos % g_vb_runtime.snake.cols;
        int y = pos / g_vb_runtime.snake.cols;
        if (!vb_snake_contains(x, y))
        {
            g_vb_runtime.snake.food_x = x;
            g_vb_runtime.snake.food_y = y;
            return;
        }
    }
    g_vb_runtime.snake.food_x = 0;
    g_vb_runtime.snake.food_y = 0;
}

static void vb_snake_reset(void)
{
    int cx = g_vb_runtime.snake.cols / 2;
    int cy = g_vb_runtime.snake.rows / 2;
    g_vb_runtime.snake.length = 4;
    g_vb_runtime.snake.dir_x = 1;
    g_vb_runtime.snake.dir_y = 0;
    g_vb_runtime.snake.score = 0;
    g_vb_runtime.snake.moves = 0;
    g_vb_runtime.snake.last_score = -1;
    g_vb_runtime.snake.last_best_score = -1;
    g_vb_runtime.snake.last_status_move = -1;
    g_vb_runtime.snake.snake_x[0] = cx;
    g_vb_runtime.snake.snake_y[0] = cy;
    g_vb_runtime.snake.snake_x[1] = cx - 1;
    g_vb_runtime.snake.snake_y[1] = cy;
    g_vb_runtime.snake.snake_x[2] = cx - 2;
    g_vb_runtime.snake.snake_y[2] = cy;
    g_vb_runtime.snake.snake_x[3] = cx - 3;
    g_vb_runtime.snake.snake_y[3] = cy;
    vb_snake_place_food();
}

static int vb_snake_candidate_safe(int x, int y, int grow)
{
    int i;
    if (x < 0 || y < 0 || x >= g_vb_runtime.snake.cols || y >= g_vb_runtime.snake.rows) return 0;
    for (i = 0; i < g_vb_runtime.snake.length; i++)
    {
        if (!grow && i == g_vb_runtime.snake.length - 1) continue;
        if (g_vb_runtime.snake.snake_x[i] == x && g_vb_runtime.snake.snake_y[i] == y) return 0;
    }
    return 1;
}

static void vb_snake_pick_direction(int *out_dx, int *out_dy)
{
    static const int dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    int best = -1;
    int best_score = 10000;
    int hx = g_vb_runtime.snake.snake_x[0];
    int hy = g_vb_runtime.snake.snake_y[0];
    int i;
    for (i = 0; i < 4; i++)
    {
        int dx = dirs[i][0];
        int dy = dirs[i][1];
        int nx = hx + dx;
        int ny = hy + dy;
        int grow = nx == g_vb_runtime.snake.food_x && ny == g_vb_runtime.snake.food_y;
        int score;
        if (dx == -g_vb_runtime.snake.dir_x && dy == -g_vb_runtime.snake.dir_y) continue;
        if (!vb_snake_candidate_safe(nx, ny, grow)) continue;
        score = abs(nx - g_vb_runtime.snake.food_x) + abs(ny - g_vb_runtime.snake.food_y);
        score += ((g_vb_runtime.snake.moves + i) % 3);
        if (score < best_score)
        {
            best_score = score;
            best = i;
        }
    }
    if (best < 0)
    {
        for (i = 0; i < 4; i++)
        {
            int nx = hx + dirs[i][0];
            int ny = hy + dirs[i][1];
            if (vb_snake_candidate_safe(nx, ny, 0))
            {
                best = i;
                break;
            }
        }
    }
    if (best < 0)
    {
        *out_dx = g_vb_runtime.snake.dir_x;
        *out_dy = g_vb_runtime.snake.dir_y;
        return;
    }
    *out_dx = dirs[best][0];
    *out_dy = dirs[best][1];
}

static void vb_snake_render(void)
{
    int i;
    char text[64];
    int board_w = g_vb_runtime.snake.cols * g_vb_runtime.snake.cell;
    int board_h = g_vb_runtime.snake.rows * g_vb_runtime.snake.cell;
    int origin_x = (LV_HOR_RES_MAX - board_w) / 2;
    int origin_y = 112;
    int tile_size = g_vb_runtime.snake.cell - 2;

    for (i = 0; i < VB_SNAKE_MAX_DRAWN; i++)
    {
        lv_obj_t *segment = g_vb_runtime.snake.segments[i];
        if (!segment) continue;
        if (i < g_vb_runtime.snake.length)
        {
            lv_obj_set_pos(segment,
                           origin_x + g_vb_runtime.snake.snake_x[i] * g_vb_runtime.snake.cell,
                           origin_y + g_vb_runtime.snake.snake_y[i] * g_vb_runtime.snake.cell);
            lv_obj_set_style_bg_color(segment,
                                      lv_color_hex(i == 0 ? 0xa7f3d0 : 0x22c55e),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_clear_flag(segment, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_vb_runtime.snake.food)
    {
        lv_obj_set_size(g_vb_runtime.snake.food, tile_size, tile_size);
        lv_obj_set_pos(g_vb_runtime.snake.food,
                       origin_x + g_vb_runtime.snake.food_x * g_vb_runtime.snake.cell,
                       origin_y + g_vb_runtime.snake.food_y * g_vb_runtime.snake.cell);
    }
    if (g_vb_runtime.snake.score_label &&
        (g_vb_runtime.snake.score != g_vb_runtime.snake.last_score ||
         g_vb_runtime.snake.best_score != g_vb_runtime.snake.last_best_score))
    {
        rt_snprintf(text, sizeof(text), "Score %d   Best %d", g_vb_runtime.snake.score, g_vb_runtime.snake.best_score);
        lv_label_set_text(g_vb_runtime.snake.score_label, text);
        g_vb_runtime.snake.last_score = g_vb_runtime.snake.score;
        g_vb_runtime.snake.last_best_score = g_vb_runtime.snake.best_score;
    }
    if (g_vb_runtime.snake.status_label &&
        (g_vb_runtime.snake.last_status_move < 0 ||
         (g_vb_runtime.snake.moves % 10) == 0 ||
         g_vb_runtime.snake.moves < g_vb_runtime.snake.last_status_move))
    {
        rt_snprintf(text, sizeof(text), "auto run  move %d", g_vb_runtime.snake.moves);
        lv_label_set_text(g_vb_runtime.snake.status_label, text);
        g_vb_runtime.snake.last_status_move = g_vb_runtime.snake.moves;
    }
}

static void vb_snake_step(void)
{
    int i;
    int dx;
    int dy;
    int nx;
    int ny;
    int grow;
    if (!g_vb_runtime.snake.active) return;
    vb_snake_pick_direction(&dx, &dy);
    nx = g_vb_runtime.snake.snake_x[0] + dx;
    ny = g_vb_runtime.snake.snake_y[0] + dy;
    grow = nx == g_vb_runtime.snake.food_x && ny == g_vb_runtime.snake.food_y;
    if (!vb_snake_candidate_safe(nx, ny, grow))
    {
        if (g_vb_runtime.snake.score > g_vb_runtime.snake.best_score)
        {
            g_vb_runtime.snake.best_score = g_vb_runtime.snake.score;
        }
        rt_kprintf("[vb_runtime][snake] reset score=%d best=%d\n",
                   g_vb_runtime.snake.score, g_vb_runtime.snake.best_score);
        vb_snake_reset();
        vb_snake_render();
        return;
    }
    if (grow && g_vb_runtime.snake.length < VB_SNAKE_MAX_DRAWN)
    {
        g_vb_runtime.snake.length++;
        g_vb_runtime.snake.score++;
        if (g_vb_runtime.snake.score > g_vb_runtime.snake.best_score)
        {
            g_vb_runtime.snake.best_score = g_vb_runtime.snake.score;
        }
    }
    for (i = g_vb_runtime.snake.length - 1; i > 0; i--)
    {
        g_vb_runtime.snake.snake_x[i] = g_vb_runtime.snake.snake_x[i - 1];
        g_vb_runtime.snake.snake_y[i] = g_vb_runtime.snake.snake_y[i - 1];
    }
    g_vb_runtime.snake.snake_x[0] = nx;
    g_vb_runtime.snake.snake_y[0] = ny;
    g_vb_runtime.snake.dir_x = dx;
    g_vb_runtime.snake.dir_y = dy;
    g_vb_runtime.snake.moves++;
    if (grow)
    {
        vb_snake_place_food();
        if (!g_vb_runtime.quiet_logs)
        {
            rt_kprintf("[vb_runtime][snake] food score=%d\n", g_vb_runtime.snake.score);
        }
    }
    if (!g_vb_runtime.quiet_logs &&
        (g_vb_runtime.snake.moves <= 8 || (g_vb_runtime.snake.moves % VB_SNAKE_LOG_EVERY) == 0))
    {
        rt_kprintf("[vb_runtime][snake] move=%d score=%d head=%d,%d food=%d,%d\n",
                   g_vb_runtime.snake.moves, g_vb_runtime.snake.score, nx, ny,
                   g_vb_runtime.snake.food_x, g_vb_runtime.snake.food_y);
    }
    vb_snake_render();
}

static int vb_snake_start(const char *title, int cols, int rows, int period_ms)
{
    int size;
    int board_w;
    int board_h;
    int origin_x;
    int origin_y;
    int i;
    lv_obj_t *label;
    lv_obj_t *panel;

    if (!g_vb_runtime.root) return -RT_ERROR;
    if (cols < 6) cols = 6;
    if (rows < 6) rows = 6;
    if (cols > VB_SNAKE_MAX_COLS) cols = VB_SNAKE_MAX_COLS;
    if (rows > VB_SNAKE_MAX_ROWS) rows = VB_SNAKE_MAX_ROWS;
    if (period_ms < 120) period_ms = 120;
    if (period_ms > 1200) period_ms = 1200;
    size = 16;
    board_w = cols * size;
    board_h = rows * size;
    origin_x = (LV_HOR_RES_MAX - board_w) / 2;
    origin_y = 112;

    rt_memset(&g_vb_runtime.snake, 0, sizeof(g_vb_runtime.snake));
    g_vb_runtime.snake.active = 1;
    g_vb_runtime.snake.cols = cols;
    g_vb_runtime.snake.rows = rows;
    g_vb_runtime.snake.cell = size;
    g_vb_runtime.snake.period_ticks = (uint32_t)((period_ms * RT_TICK_PER_SECOND) / 1000);
    if (g_vb_runtime.snake.period_ticks == 0) g_vb_runtime.snake.period_ticks = RT_TICK_PER_SECOND / 4;
    g_vb_runtime.snake.last_run = rt_tick_get();

    panel = lv_obj_create(g_vb_runtime.root);
    lv_obj_set_size(panel, board_w + 18, board_h + 18);
    lv_obj_set_pos(panel, origin_x - 9, origin_y - 9);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0b1220), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = vb_create_label(g_vb_runtime.root, title && title[0] ? title : "Auto Snake", FONT_BIGL, lv_color_hex(0xa7f3d0));
    lv_obj_set_width(label, 330);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 32);

    g_vb_runtime.snake.score_label = vb_create_label(g_vb_runtime.root, "Score 0   Best 0", FONT_SMALL, lv_color_hex(0xfbbf24));
    lv_obj_set_width(g_vb_runtime.snake.score_label, 330);
    lv_obj_set_style_text_align(g_vb_runtime.snake.score_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.snake.score_label, LV_ALIGN_TOP_MID, 0, 72);

    g_vb_runtime.snake.status_label = vb_create_label(g_vb_runtime.root, "auto run", FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_set_width(g_vb_runtime.snake.status_label, 330);
    lv_obj_set_style_text_align(g_vb_runtime.snake.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.snake.status_label, LV_ALIGN_BOTTOM_MID, 0, -24);

    for (i = 0; i < VB_SNAKE_MAX_DRAWN; i++)
    {
        lv_obj_t *segment = lv_obj_create(g_vb_runtime.root);
        lv_obj_set_size(segment, size - 2, size - 2);
        lv_obj_set_pos(segment, origin_x, origin_y);
        lv_obj_set_style_radius(segment, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(segment, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(segment, lv_color_hex(0x22c55e), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
        g_vb_runtime.snake.segments[i] = segment;
    }
    g_vb_runtime.snake.food = lv_obj_create(g_vb_runtime.root);
    lv_obj_set_size(g_vb_runtime.snake.food, size - 2, size - 2);
    lv_obj_set_style_radius(g_vb_runtime.snake.food, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_vb_runtime.snake.food, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_vb_runtime.snake.food, lv_color_hex(0xf43f5e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_vb_runtime.snake.food, LV_OBJ_FLAG_SCROLLABLE);

    vb_snake_reset();
    vb_snake_render();
    rt_kprintf("[vb_runtime][snake] autoplay started cols=%d rows=%d period=%d\n", cols, rows, period_ms);
    return RT_EOK;
}

static void vb_script_resolve_asset_path(const char *src, char *dst, rt_size_t cap)
{
    if (!src || !src[0])
    {
        dst[0] = '\0';
        return;
    }
    if (strstr(src, "..") || strstr(src, "//"))
    {
        dst[0] = '\0';
        return;
    }
    if (src[0] == '/')
    {
        vb_safe_copy(dst, cap, src);
        return;
    }
    rt_snprintf(dst, cap, "%s/%s/%s", VIBEBOARD_APP_ROOT, g_vb_runtime.active_app, src);
    dst[cap - 1] = '\0';
}

static int vb_script_execute_print(const char *line)
{
    char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
    int argc = 0;
    int i;
    if (!vb_extract_call_args(line, "print", args, &argc)) return 0;
    rt_kprintf("[vb_runtime][lua]");
    for (i = 0; i < argc; i++) rt_kprintf("%s%s", i == 0 ? " " : "\t", args[i]);
    rt_kprintf("\n");
    return 1;
}

static int vb_script_execute_create(const char *line)
{
    char var[VB_MAX_SCRIPT_NAME];
    char parent[VB_MAX_SCRIPT_NAME];
    const char *p = vb_skip_spaces(line);
    const char *eq;
    lv_obj_t *obj = RT_NULL;
    vb_script_object_type_t type = VB_SCRIPT_OBJ_CONTAINER;

    if (strncmp(p, "local ", 6) == 0) p = vb_skip_spaces(p + 6);
    if (!vb_parse_name_token(&p, var, sizeof(var))) return 0;
    p = vb_skip_spaces(p);
    if (*p != '=') return 0;
    eq = vb_skip_spaces(p + 1);

    if (strstr(eq, "lv_scr_act("))
    {
        vb_script_store_object(var, g_vb_runtime.root, VB_SCRIPT_OBJ_ROOT);
        return 1;
    }

    if (strstr(eq, "lv_obj_create("))
    {
        char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
        int argc = 0;
        vb_extract_call_args(eq, "lv_obj_create", args, &argc);
        vb_safe_copy(parent, sizeof(parent), argc >= 1 ? args[0] : "root");
        obj = lv_obj_create(vb_script_resolve_parent(parent));
        type = VB_SCRIPT_OBJ_CONTAINER;
    }
    else if (strstr(eq, "lv_label_create("))
    {
        char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
        int argc = 0;
        vb_extract_call_args(eq, "lv_label_create", args, &argc);
        vb_safe_copy(parent, sizeof(parent), argc >= 1 ? args[0] : "root");
        obj = lv_label_create(vb_script_resolve_parent(parent));
        lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
        type = VB_SCRIPT_OBJ_LABEL;
        g_vb_runtime.script_last_label = obj;
    }
    else if (strstr(eq, "lv_btn_create("))
    {
        char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
        int argc = 0;
        vb_extract_call_args(eq, "lv_btn_create", args, &argc);
        vb_safe_copy(parent, sizeof(parent), argc >= 1 ? args[0] : "root");
        obj = lv_btn_create(vb_script_resolve_parent(parent));
        type = VB_SCRIPT_OBJ_BUTTON;
    }
    else if (strstr(eq, "lv_img_create("))
    {
        char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
        int argc = 0;
        vb_extract_call_args(eq, "lv_img_create", args, &argc);
        vb_safe_copy(parent, sizeof(parent), argc >= 1 ? args[0] : "root");
        obj = lv_img_create(vb_script_resolve_parent(parent));
        type = VB_SCRIPT_OBJ_IMAGE;
    }

    if (!obj) return 0;
    if (type == VB_SCRIPT_OBJ_CONTAINER)
    {
        lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    vb_script_store_object(var, obj, type);
    return 1;
}

static int vb_script_execute_lvgl_call(const char *line)
{
    char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
    int argc = 0;
    vb_script_object_t *target;
    lv_obj_t *obj;

    if (vb_extract_call_args(line, "lv_obj_clean", args, &argc) && argc >= 1)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj)
        {
            lv_obj_clean(obj);
            if (obj == g_vb_runtime.root)
            {
                int i;
                g_vb_runtime.status_label = RT_NULL;
                g_vb_runtime.clock_label = RT_NULL;
                for (i = 0; i < g_vb_runtime.component_count; i++)
                {
                    g_vb_runtime.components[i].value_label = RT_NULL;
                }
            }
        }
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_size", args, &argc) && argc >= 3)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_size(obj, atoi(args[1]), atoi(args[2]));
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_width", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_width(obj, atoi(args[1]));
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_height", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_height(obj, atoi(args[1]));
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_pos", args, &argc) && argc >= 3)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_pos(obj, atoi(args[1]), atoi(args[2]));
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_align", args, &argc) && argc >= 4)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_align(obj, vb_script_align_from_arg(args[1]), atoi(args[2]), atoi(args[3]));
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_style_bg_color", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_style_bg_color(obj, lv_color_hex(strtoul(args[1], RT_NULL, 0)), LV_PART_MAIN | LV_STATE_DEFAULT);
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_style_text_color", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_style_text_color(obj, lv_color_hex(strtoul(args[1], RT_NULL, 0)), LV_PART_MAIN | LV_STATE_DEFAULT);
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_style_radius", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_style_radius(obj, atoi(args[1]), LV_PART_MAIN | LV_STATE_DEFAULT);
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_style_border_width", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_style_border_width(obj, atoi(args[1]), LV_PART_MAIN | LV_STATE_DEFAULT);
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_set_style_border_color", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj) lv_obj_set_style_border_color(obj, lv_color_hex(strtoul(args[1], RT_NULL, 0)), LV_PART_MAIN | LV_STATE_DEFAULT);
        return 1;
    }
    if (vb_extract_call_args(line, "lv_obj_clear_flag", args, &argc) && argc >= 2)
    {
        obj = vb_script_resolve_parent(args[0]);
        if (obj && rt_strcmp(args[1], "LV_OBJ_FLAG_SCROLLABLE") == 0) lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        return 1;
    }
    if (vb_extract_call_args(line, "lv_label_set_text", args, &argc) && argc >= 2)
    {
        target = vb_script_find_object(args[0]);
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, args[1]);
            g_vb_runtime.script_last_label = target->obj;
        }
        return 1;
    }
    if (vb_extract_call_args(line, "lv_label_set_long_mode", args, &argc) && argc >= 2)
    {
        target = vb_script_find_object(args[0]);
        if (target && target->obj)
        {
            if (rt_strcmp(args[1], "LV_LABEL_LONG_CLIP") == 0) lv_label_set_long_mode(target->obj, LV_LABEL_LONG_CLIP);
            else if (rt_strcmp(args[1], "LV_LABEL_LONG_SCROLL_CIRCULAR") == 0) lv_label_set_long_mode(target->obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
            else lv_label_set_long_mode(target->obj, LV_LABEL_LONG_WRAP);
        }
        return 1;
    }
    if (vb_extract_call_args(line, "lv_img_set_src", args, &argc) && argc >= 2)
    {
        char resolved[VB_MAX_PATH];
        target = vb_script_find_object(args[0]);
        vb_script_resolve_asset_path(args[1], resolved, sizeof(resolved));
        if (target && target->obj && resolved[0]) lv_img_set_src(target->obj, resolved);
        return 1;
    }
    return 0;
}

static int vb_script_execute_helper_call(const char *line)
{
    char args[VB_MAX_SCRIPT_ARGS][VB_MAX_SCRIPT_ARG];
    int argc = 0;
    if (vb_extract_call_args(line, "vibe_label", args, &argc) && argc >= 6)
    {
        lv_obj_t *label = lv_label_create(g_vb_runtime.root);
        lv_label_set_text(label, args[0]);
        lv_obj_set_width(label, atoi(args[3]));
        lv_obj_align(label, vb_script_align_from_arg(args[4]), atoi(args[1]), atoi(args[2]));
        lv_obj_set_style_text_color(label, lv_color_hex(strtoul(args[5], RT_NULL, 0)), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_ext_set_local_font(label, FONT_NORMAL, lv_color_hex(strtoul(args[5], RT_NULL, 0)));
        g_vb_runtime.script_last_label = label;
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_button", args, &argc) && argc >= 5)
    {
        lv_obj_t *button = lv_btn_create(g_vb_runtime.root);
        lv_obj_t *label;
        lv_obj_set_size(button, atoi(args[1]), atoi(args[2]));
        lv_obj_align(button, LV_ALIGN_TOP_LEFT, atoi(args[3]), atoi(args[4]));
        lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        label = vb_create_label(button, args[0], FONT_SMALL, LV_COLOR_WHITE);
        lv_obj_center(label);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_image", args, &argc) && argc >= 3)
    {
        char resolved[VB_MAX_PATH];
        lv_obj_t *image = lv_img_create(g_vb_runtime.root);
        vb_script_resolve_asset_path(args[0], resolved, sizeof(resolved));
        if (resolved[0]) lv_img_set_src(image, resolved);
        lv_obj_align(image, LV_ALIGN_TOP_LEFT, atoi(args[1]), atoi(args[2]));
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_read_file", args, &argc) && argc >= 1)
    {
        char path[VB_MAX_PATH];
        char preview[80];
        int len;
        vb_script_resolve_asset_path(args[0], path, sizeof(path));
        len = vb_read_text_file(path, preview, sizeof(preview));
        rt_kprintf("[vb_runtime][lua] file.read %s %s len=%d\n", args[0], len >= 0 ? "ok" : "failed", len);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_timer_label", args, &argc) && argc >= 3)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        if (target && target->obj)
        {
            g_vb_runtime.script_tick_label = target->obj;
            g_vb_runtime.script_tick_count = 0;
            g_vb_runtime.script_tick_last_run = rt_tick_get();
            g_vb_runtime.script_tick_period_ticks = (uint32_t)((atoi(args[1]) * RT_TICK_PER_SECOND) / 1000);
            if (g_vb_runtime.script_tick_period_ticks == 0) g_vb_runtime.script_tick_period_ticks = RT_TICK_PER_SECOND;
            vb_safe_copy(g_vb_runtime.script_tick_prefix, sizeof(g_vb_runtime.script_tick_prefix), args[2]);
        }
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_sensor_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[96];
        int result = vb_runtime_sensors_refresh();
        if (rt_strcmp(args[1], "light") == 0)
        {
            rt_snprintf(text, sizeof(text), g_vb_sensor.have_light ? "Light %ld lux" : "Light --",
                        (long)g_vb_sensor.light_data.data.light);
        }
        else if (rt_strcmp(args[1], "mag") == 0)
        {
            rt_snprintf(text, sizeof(text), g_vb_sensor.have_mag ? "Mag %ld,%ld,%ld" : "Mag --",
                        (long)g_vb_sensor.mag_data.data.mag.x,
                        (long)g_vb_sensor.mag_data.data.mag.y,
                        (long)g_vb_sensor.mag_data.data.mag.z);
        }
        else if (rt_strcmp(args[1], "acce") == 0 || rt_strcmp(args[1], "accel") == 0)
        {
            rt_snprintf(text, sizeof(text), g_vb_sensor.have_acce ? "Acce %ld,%ld,%ld mg" : "Acce --",
                        (long)g_vb_sensor.acce_data.data.acce.x,
                        (long)g_vb_sensor.acce_data.data.acce.y,
                        (long)g_vb_sensor.acce_data.data.acce.z);
        }
        else if (rt_strcmp(args[1], "gyro") == 0)
        {
            rt_snprintf(text, sizeof(text), g_vb_sensor.have_gyro ? "Gyro %ld,%ld,%ld mdps" : "Gyro --",
                        (long)g_vb_sensor.gyro_data.data.gyro.x,
                        (long)g_vb_sensor.gyro_data.data.gyro.y,
                        (long)g_vb_sensor.gyro_data.data.gyro.z);
        }
        else if (rt_strcmp(args[1], "step") == 0)
        {
            rt_snprintf(text, sizeof(text), g_vb_sensor.have_step ? "Step %lu" : "Step --",
                        (unsigned long)g_vb_sensor.step_data.data.step);
        }
        else
        {
            rt_snprintf(text, sizeof(text), "Sensor %s rc=%d", args[1], result);
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
        }
        rt_kprintf("[vb_runtime][lua] sensor.%s %s rc=%d\n", args[1], text, result);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_snake_autoplay", args, &argc) && argc >= 4)
    {
        vb_snake_start(args[0], atoi(args[1]), atoi(args[2]), atoi(args[3]));
        return 1;
    }
    return 0;
}

static int vb_script_execute_line(const char *line)
{
    const char *p = vb_skip_spaces(line);
    if (!p[0] || p[0] == '-' || strncmp(p, "--", 2) == 0) return 0;
    if (vb_script_execute_create(p)) return 1;
    if (vb_script_execute_print(p)) return 1;
    if (vb_script_execute_lvgl_call(p)) return 1;
    if (vb_script_execute_helper_call(p)) return 1;
    return 0;
}

static int vb_builtin_script_start(const char *script_path, const char *manifest_path)
{
    char *script;
    char *cursor;
    char *line_start;
    int len;
    int executed = 0;
    int unsupported = 0;

    (void)manifest_path;
    if (!script_path || !g_vb_runtime.root) return -RT_ERROR;
    script = (char *)rt_malloc(VB_MAX_SCRIPT);
    if (!script) return -RT_ENOMEM;
    len = vb_read_text_file(script_path, script, VB_MAX_SCRIPT);
    if (len <= 0)
    {
        rt_free(script);
        return -RT_ERROR;
    }

    rt_memset(g_vb_runtime.script_objects, 0, sizeof(g_vb_runtime.script_objects));
    g_vb_runtime.script_object_count = 1;
    vb_safe_copy(g_vb_runtime.script_objects[0].name, sizeof(g_vb_runtime.script_objects[0].name), "root");
    g_vb_runtime.script_objects[0].obj = g_vb_runtime.root;
    g_vb_runtime.script_objects[0].type = VB_SCRIPT_OBJ_ROOT;
    g_vb_runtime.script_last_label = RT_NULL;
    g_vb_runtime.script_tick_label = RT_NULL;
    g_vb_runtime.script_runtime_active = 0;
    g_vb_runtime.script_tick_prefix[0] = '\0';
    rt_memset(&g_vb_runtime.snake, 0, sizeof(g_vb_runtime.snake));

    cursor = script;
    line_start = script;
    while (*cursor)
    {
        if (*cursor == '\r' || *cursor == '\n')
        {
            char saved = *cursor;
            *cursor = '\0';
            if (vb_script_execute_line(line_start)) executed++;
            else if (vb_skip_spaces(line_start)[0]) unsupported++;
            *cursor = saved;
            if (saved == '\r' && cursor[1] == '\n') cursor++;
            line_start = cursor + 1;
        }
        cursor++;
    }
    if (vb_skip_spaces(line_start)[0])
    {
        if (vb_script_execute_line(line_start)) executed++;
        else unsupported++;
    }
    g_vb_runtime.script_runtime_active = 1;
    rt_kprintf("[vb_runtime] script-subset started: %s lines=%d unsupported=%d\n",
               script_path, executed, unsupported);
    rt_free(script);
    return RT_EOK;
}

static void vb_builtin_script_stop(void)
{
    g_vb_runtime.script_runtime_active = 0;
    g_vb_runtime.script_last_label = RT_NULL;
    g_vb_runtime.script_tick_label = RT_NULL;
    g_vb_runtime.script_object_count = 0;
    g_vb_runtime.script_tick_prefix[0] = '\0';
    rt_memset(&g_vb_runtime.snake, 0, sizeof(g_vb_runtime.snake));
}

static void vb_set_obj_bg(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *vb_create_label(lv_obj_t *parent, const char *text, uint16_t font_size,
                                 lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_ext_set_local_font(label, font_size, color);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static void vb_set_status(const char *text)
{
    if (g_vb_runtime.status_label)
    {
        lv_label_set_text(g_vb_runtime.status_label, text);
    }
}

static void vb_action_event_cb(lv_event_t *event)
{
    vb_component_t *component;
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) return;
    component = (vb_component_t *)lv_event_get_user_data(event);
    if (!component) return;
    if (rt_strcmp(component->capability, "reload") == 0 ||
        rt_strcmp(component->capability, "vibeboard.launcher.reload") == 0)
    {
        vb_set_status("reload requested");
        rt_kprintf("[vb_runtime] action reload\n");
        return;
    }
    vb_set_status("Runtime update required for native action");
    rt_kprintf("[vb_runtime] action %s requires runtime binding\n", component->capability);
}

static void vb_back_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        gui_app_run("Main");
    }
}

static void vb_render_component(vb_component_t *component, int index)
{
    lv_coord_t y = 142 + index * 42;
    if (rt_strcmp(component->type, "action") == 0)
    {
        lv_obj_t *button = lv_btn_create(g_vb_runtime.root);
        lv_obj_t *label;
        lv_obj_set_size(button, 260, 34);
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x2dd4bf), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(button, vb_action_event_cb, LV_EVENT_CLICKED, component);
        label = vb_create_label(button, component->label, FONT_SMALL, lv_color_hex(0x0f172a));
        lv_obj_center(label);
        return;
    }

    lv_obj_t *left = vb_create_label(g_vb_runtime.root, component->label, FONT_SMALL, lv_color_hex(0x94a3b8));
    lv_obj_set_width(left, 150);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 32, y);

    component->value_label = vb_create_label(g_vb_runtime.root, component->value, FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_set_width(component->value_label, 170);
    lv_obj_align(component->value_label, LV_ALIGN_TOP_LEFT, 188, y);
}

static void vb_render_runtime_ui(int manifest_loaded, int main_lua_present)
{
    int i;
    char line[VB_MAX_TEXT + 32];

    g_vb_runtime.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_vb_runtime.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(g_vb_runtime.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_vb_runtime.root, LV_OBJ_FLAG_SCROLLABLE);
    vb_set_obj_bg(g_vb_runtime.root, 0x0f172a);

    lv_obj_t *back = lv_btn_create(g_vb_runtime.root);
    lv_obj_set_size(back, 72, 36);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 12, 14);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back, vb_back_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_t *back_label = vb_create_label(back, "Back", FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_center(back_label);

    lv_obj_t *title = vb_create_label(g_vb_runtime.root, "VibeBoard Runtime", FONT_BIGL, lv_color_hex(0x5eead4));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    rt_snprintf(line, sizeof(line), "%s", g_vb_runtime.app_name);
    lv_obj_t *name = vb_create_label(g_vb_runtime.root, line, FONT_NORMAL, LV_COLOR_WHITE);
    lv_obj_set_width(name, 330);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t *desc = vb_create_label(g_vb_runtime.root, g_vb_runtime.description, FONT_SMALL, lv_color_hex(0xcbd5e1));
    lv_obj_set_width(desc, 330);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 92);

    rt_snprintf(line, sizeof(line), "active=%s manifest=%s lua=%s", g_vb_runtime.active_app,
                manifest_loaded ? "ok" : "fallback", main_lua_present ? "present" : "missing");
    lv_obj_t *evidence = vb_create_label(g_vb_runtime.root, line, FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_set_width(evidence, 350);
    lv_obj_set_style_text_align(evidence, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(evidence, LV_ALIGN_TOP_MID, 0, 116);

    if (g_vb_runtime.component_count > 0)
    {
        for (i = 0; i < g_vb_runtime.component_count; i++)
        {
            vb_render_component(&g_vb_runtime.components[i], i);
        }
    }
    else
    {
        lv_obj_t *panel = lv_obj_create(g_vb_runtime.root);
        lv_obj_set_size(panel, 300, 128);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 154);
        vb_set_obj_bg(panel, 0x172554);
        lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *panel_text = vb_create_label(panel, "No dynamic manifest UI yet\nInstall a Runtime App package", FONT_NORMAL, LV_COLOR_WHITE);
        lv_obj_set_width(panel_text, 250);
        lv_obj_set_style_text_align(panel_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(panel_text);
    }

    g_vb_runtime.clock_label = vb_create_label(g_vb_runtime.root, "tick=0", FONT_SMALL, lv_color_hex(0xfbbf24));
    lv_obj_align(g_vb_runtime.clock_label, LV_ALIGN_BOTTOM_MID, 0, -48);

    g_vb_runtime.status_label = vb_create_label(g_vb_runtime.root, "serial bridge ready", FONT_SMALL, lv_color_hex(0xa7f3d0));
    lv_obj_set_width(g_vb_runtime.status_label, 350);
    lv_obj_set_style_text_align(g_vb_runtime.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.status_label, LV_ALIGN_BOTTOM_MID, 0, -16);
}

static int vb_load_active_package(void)
{
    char manifest_path[VB_MAX_PATH];
    char lua_path[VB_MAX_PATH];
    char *json;
    int manifest_loaded = 0;
    int main_lua_present = 0;

    vibeboard_lua_stop_app();
    rt_memset(g_vb_runtime.components, 0, sizeof(g_vb_runtime.components));
    g_vb_runtime.component_count = 0;
    vb_read_active_app(g_vb_runtime.active_app, sizeof(g_vb_runtime.active_app));
    vb_safe_copy(g_vb_runtime.app_name, sizeof(g_vb_runtime.app_name), g_vb_runtime.active_app);
    vb_safe_copy(g_vb_runtime.description, sizeof(g_vb_runtime.description), "Install app packages over USB serial. No firmware flash needed.");

    vb_build_app_path(manifest_path, sizeof(manifest_path), g_vb_runtime.active_app, "manifest.json");
    vb_build_app_path(lua_path, sizeof(lua_path), g_vb_runtime.active_app, "main.lua");
    main_lua_present = vb_file_exists(lua_path);

    json = (char *)rt_malloc(VB_MAX_MANIFEST);
    if (json)
    {
        if (vb_read_text_file(manifest_path, json, VB_MAX_MANIFEST) > 0)
        {
            manifest_loaded = 1;
            vb_json_copy_string(json, RT_NULL, "name", g_vb_runtime.app_name, sizeof(g_vb_runtime.app_name), g_vb_runtime.active_app);
            vb_json_copy_string(json, RT_NULL, "description", g_vb_runtime.description, sizeof(g_vb_runtime.description), "Runtime app package");
            g_vb_runtime.component_count = vb_parse_manifest_components(json);
        }
        rt_free(json);
    }

    if (!manifest_loaded)
    {
        rt_kprintf("[vb_runtime] missing manifest: %s, using built-in panel\n", manifest_path);
    }
    rt_kprintf("[vb_runtime] active app: %s\n", g_vb_runtime.active_app);
    vb_render_runtime_ui(manifest_loaded, main_lua_present);

    if (vibeboard_lua_runtime_available() && main_lua_present)
    {
        if (vibeboard_lua_start_script(lua_path, manifest_path) == RT_EOK)
        {
            rt_kprintf("[vb_runtime] lua app started: %s engine=%s\n", lua_path, vibeboard_lua_runtime_name());
        }
        else
        {
            rt_kprintf("[vb_runtime] lua adapter failed, using manifest fallback\n");
        }
    }
    else if (main_lua_present)
    {
        rt_kprintf("[vb_runtime] main.lua present, lua adapter unavailable: %s\n", lua_path);
    }
    return RT_EOK;
}

static int vb_runtime_reload_current(void)
{
    if (!g_vb_runtime.running)
    {
        return gui_app_run(APP_ID);
    }
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    g_vb_runtime.status_label = RT_NULL;
    g_vb_runtime.clock_label = RT_NULL;
    return vb_load_active_package();
}

static void vb_runtime_request_reload(void)
{
    if (g_vb_runtime.running)
    {
        g_vb_runtime.pending_reload = 1;
    }
}

static int vb_runtime_select_app(const char *app_id)
{
    int result;
    if (!vb_is_safe_app_id(app_id))
    {
        rt_kprintf("usage: vb_runtime_select <app_id>\n");
        return -RT_EINVAL;
    }
    result = vb_write_active_app(app_id);
    if (result != RT_EOK)
    {
        rt_kprintf("[vb_runtime] select failed: %s (%d)\n", app_id, result);
        return result;
    }
    rt_kprintf("[vb_runtime] selected app: %s\n", app_id);
    vb_runtime_request_reload();
    return RT_EOK;
}

static int vb_runtime_install_begin_app(const char *app_id)
{
    char staging_dir[VB_MAX_PATH];
    char marker[VB_MAX_PATH];
    if (!vb_is_safe_app_id(app_id))
    {
        rt_kprintf("usage: vb_runtime_install_begin <app_id>\n");
        return -RT_EINVAL;
    }
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    vb_build_install_dir(staging_dir, sizeof(staging_dir), VIBEBOARD_STAGING_PREFIX, app_id);
    if (access(staging_dir, 0) == 0 && vb_remove_tree(staging_dir) != RT_EOK)
    {
        rt_kprintf("[vb_runtime] install begin failed: clean staging %s\n", staging_dir);
        return -RT_ERROR;
    }
    if (vb_mkdir_recursive(staging_dir) != RT_EOK)
    {
        rt_kprintf("[vb_runtime] install begin failed: mkdir %s\n", staging_dir);
        return -RT_ERROR;
    }
    rt_snprintf(marker, sizeof(marker), "%s/.installing", staging_dir);
    vb_write_text_file(marker, "1\n");
    rt_kprintf("[vb_runtime] install begin: %s\n", app_id);
    return RT_EOK;
}

static int vb_runtime_install_file_chunk(const char *app_id, const char *path,
                                         const char *offset_text, const char *hex)
{
    char file_path[VB_MAX_PATH];
    char dir_path[VB_MAX_PATH];
    char *slash;
    long offset;
    int fd;
    int result;
    int flags;

    if (!vb_is_safe_app_id(app_id) || !vb_is_runtime_package_path(path) ||
        !offset_text || !hex || rt_strlen(hex) > VB_MAX_HEX_CHARS)
    {
        rt_kprintf("[vb_runtime] install file failed: invalid args\n");
        return -RT_EINVAL;
    }

    offset = strtol(offset_text, RT_NULL, 10);
    if (offset < 0)
    {
        rt_kprintf("[vb_runtime] install file failed: bad offset\n");
        return -RT_EINVAL;
    }

    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    rt_snprintf(file_path, sizeof(file_path), "%s/%s%s/%s", VIBEBOARD_APP_ROOT,
                VIBEBOARD_STAGING_PREFIX, app_id, path);
    file_path[sizeof(file_path) - 1] = '\0';
    vb_safe_copy(dir_path, sizeof(dir_path), file_path);
    slash = strrchr(dir_path, '/');
    if (slash)
    {
        *slash = '\0';
        if (vb_mkdir_recursive(dir_path) != RT_EOK)
        {
            rt_kprintf("[vb_runtime] install file failed: mkdir %s\n", dir_path);
            return -RT_ERROR;
        }
    }

    flags = O_WRONLY | O_CREAT;
    if (offset == 0) flags |= O_TRUNC;
    fd = open(file_path, flags, 0);
    if (fd < 0)
    {
        rt_kprintf("[vb_runtime] install file open failed: %s\n", file_path);
        return -RT_ERROR;
    }
    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        close(fd);
        rt_kprintf("[vb_runtime] install file seek failed: %s\n", file_path);
        return -RT_ERROR;
    }
    result = vb_write_hex_chunk(fd, hex);
    close(fd);
    if (result != RT_EOK)
    {
        rt_kprintf("[vb_runtime] install file write failed: %s (%d)\n", path, result);
        return result;
    }
    if (!g_vb_runtime.quiet_logs)
    {
        rt_kprintf("[vb_runtime] install chunk: %s %ld %u\n", path, offset,
                   (unsigned int)(rt_strcmp(hex, "-") == 0 ? 0 : rt_strlen(hex) / 2));
    }
    return RT_EOK;
}

static int vb_runtime_install_end_app(const char *app_id)
{
    char manifest_path[VB_MAX_PATH];
    char appinfo_path[VB_MAX_PATH];
    char lua_path[VB_MAX_PATH];
    char staging_dir[VB_MAX_PATH];
    char app_dir[VB_MAX_PATH];
    char backup_dir[VB_MAX_PATH];
    char marker[VB_MAX_PATH];
    int have_current = 0;
    int have_backup = 0;
    if (!vb_is_safe_app_id(app_id))
    {
        rt_kprintf("usage: vb_runtime_install_end <app_id>\n");
        return -RT_EINVAL;
    }
    vb_build_install_dir(staging_dir, sizeof(staging_dir), VIBEBOARD_STAGING_PREFIX, app_id);
    rt_snprintf(app_dir, sizeof(app_dir), "%s/%s", VIBEBOARD_APP_ROOT, app_id);
    app_dir[sizeof(app_dir) - 1] = '\0';
    vb_build_install_dir(backup_dir, sizeof(backup_dir), VIBEBOARD_BACKUP_PREFIX, app_id);
    rt_snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", staging_dir);
    rt_snprintf(appinfo_path, sizeof(appinfo_path), "%s/app.info", staging_dir);
    rt_snprintf(lua_path, sizeof(lua_path), "%s/main.lua", staging_dir);
    manifest_path[sizeof(manifest_path) - 1] = '\0';
    appinfo_path[sizeof(appinfo_path) - 1] = '\0';
    lua_path[sizeof(lua_path) - 1] = '\0';
    if (!vb_file_exists(lua_path) || (!vb_file_exists(manifest_path) && !vb_file_exists(appinfo_path)))
    {
        rt_kprintf("[vb_runtime] install end failed: package incomplete %s\n", app_id);
        return -RT_ERROR;
    }
    if (access(backup_dir, 0) == 0 && vb_remove_tree(backup_dir) != RT_EOK)
    {
        rt_kprintf("[vb_runtime] install end failed: clean backup %s\n", backup_dir);
        return -RT_ERROR;
    }
    have_current = access(app_dir, 0) == 0;
    if (have_current)
    {
        if (rename(app_dir, backup_dir) != 0)
        {
            rt_kprintf("[vb_runtime] install end failed: backup current %s\n", app_id);
            return -RT_ERROR;
        }
        have_backup = 1;
    }
    if (rename(staging_dir, app_dir) != 0)
    {
        rt_kprintf("[vb_runtime] install end failed: commit staging %s\n", app_id);
        if (have_backup)
        {
            rename(backup_dir, app_dir);
        }
        return -RT_ERROR;
    }
    if (vb_write_active_app(app_id) != RT_EOK)
    {
        rt_kprintf("[vb_runtime] install end failed: active marker\n");
        if (access(app_dir, 0) == 0)
        {
            vb_remove_tree(app_dir);
        }
        if (have_backup)
        {
            rename(backup_dir, app_dir);
        }
        return -RT_ERROR;
    }
    rt_snprintf(marker, sizeof(marker), "%s/.installing", app_dir);
    unlink(marker);
    if (have_backup)
    {
        vb_remove_tree(backup_dir);
    }
    rt_kprintf("[vb_runtime] install complete: %s\n", app_id);
    if (g_vb_runtime.running)
    {
        vb_runtime_request_reload();
    }
    return RT_EOK;
}

static int vb_is_safe_http_base_url(const char *url)
{
    int i;
    int len;
    if (!url) return 0;
    len = rt_strlen(url);
    if (len <= 7 || len >= VB_MAX_URL) return 0;
    if (rt_strncmp(url, "http://", 7) != 0 && rt_strncmp(url, "https://", 8) != 0) return 0;
    if (strchr(url, '?') || strchr(url, '#')) return 0;
    for (i = 0; i < len; i++)
    {
        char c = url[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'')
        {
            return 0;
        }
    }
    return 1;
}

static int vb_http_build_url(char *dst, rt_size_t cap, const char *base_url, const char *path)
{
    int base_len;
    int path_len;
    int add_slash;
    if (!dst || cap == 0 || !base_url || !path) return -RT_EINVAL;
    base_len = rt_strlen(base_url);
    path_len = rt_strlen(path);
    add_slash = base_url[base_len - 1] == '/' ? 0 : 1;
    if (base_len + add_slash + path_len + 1 > (int)cap) return -RT_EINVAL;
    rt_snprintf(dst, cap, "%s%s%s", base_url, add_slash ? "/" : "", path);
    dst[cap - 1] = '\0';
    return RT_EOK;
}

static int vb_build_staging_file_path(char *dst, rt_size_t cap, const char *app_id, const char *path)
{
    char staging_dir[VB_MAX_PATH];
    if (!vb_is_safe_app_id(app_id) || !vb_is_runtime_package_path(path)) return -RT_EINVAL;
    vb_build_install_dir(staging_dir, sizeof(staging_dir), VIBEBOARD_STAGING_PREFIX, app_id);
    if (rt_strlen(staging_dir) + 1 + rt_strlen(path) + 1 > cap) return -RT_EINVAL;
    rt_snprintf(dst, cap, "%s/%s", staging_dir, path);
    dst[cap - 1] = '\0';
    return RT_EOK;
}

#if VB_RUNTIME_HAS_HTTP_APP_OTA
static int vb_http_download_to_file(const char *url, const char *file_path, int required, int max_size)
{
    struct webclient_session *session = RT_NULL;
    char *buffer = RT_NULL;
    char dir_path[VB_MAX_PATH];
    char *slash;
    int fd = -1;
    int status;
    int content_length;
    int total = 0;
    int result = RT_EOK;

    session = webclient_session_create(VB_HTTP_HEADER_BUFSZ);
    if (!session)
    {
        rt_kprintf("[vb_runtime][ota] no memory for HTTP session\n");
        return -RT_ENOMEM;
    }
    webclient_set_timeout(session, 15000);

    status = webclient_get(session, url);
    if (status != 200)
    {
        rt_kprintf("[vb_runtime][ota] GET %s status=%d%s\n",
                   url, status, required ? "" : " optional");
        webclient_close(session);
        return required ? -RT_ERROR : RT_EOK;
    }

    content_length = webclient_content_length_get(session);
    if (content_length > max_size)
    {
        rt_kprintf("[vb_runtime][ota] file too large: %s %d>%d\n", url, content_length, max_size);
        webclient_close(session);
        return -RT_ERROR;
    }

    vb_safe_copy(dir_path, sizeof(dir_path), file_path);
    slash = strrchr(dir_path, '/');
    if (slash)
    {
        *slash = '\0';
        if (vb_mkdir_recursive(dir_path) != RT_EOK)
        {
            rt_kprintf("[vb_runtime][ota] mkdir failed: %s\n", dir_path);
            webclient_close(session);
            return -RT_ERROR;
        }
    }

    fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        rt_kprintf("[vb_runtime][ota] open failed: %s\n", file_path);
        webclient_close(session);
        return -RT_ERROR;
    }

    buffer = (char *)rt_malloc(VB_HTTP_CHUNK_SIZE);
    if (!buffer)
    {
        result = -RT_ENOMEM;
        goto out;
    }

    while (content_length < 0 || total < content_length)
    {
        int want = VB_HTTP_CHUNK_SIZE;
        int bytes_read;
        if (content_length >= 0 && content_length - total < want)
        {
            want = content_length - total;
        }
        if (want <= 0) break;

        bytes_read = webclient_read(session, buffer, want);
        if (bytes_read <= 0) break;
        if (total + bytes_read > max_size)
        {
            rt_kprintf("[vb_runtime][ota] stream too large: %s\n", url);
            result = -RT_ERROR;
            break;
        }
        if (write(fd, buffer, bytes_read) != bytes_read)
        {
            rt_kprintf("[vb_runtime][ota] write failed: %s\n", file_path);
            result = -RT_ERROR;
            break;
        }
        total += bytes_read;
    }

    if (result == RT_EOK && content_length >= 0 && total != content_length)
    {
        rt_kprintf("[vb_runtime][ota] short read: %s %d/%d\n", url, total, content_length);
        result = -RT_ERROR;
    }

out:
    if (buffer) rt_free(buffer);
    if (fd >= 0) close(fd);
    webclient_close(session);
    if (result != RT_EOK)
    {
        unlink(file_path);
    }
    else
    {
        rt_kprintf("[vb_runtime][ota] downloaded %s bytes=%d\n", file_path, total);
    }
    return result;
}

static int vb_http_download_package_file(const char *app_id, const char *base_url, const char *path,
                                         int required, int max_size)
{
    char url[VB_MAX_URL];
    char file_path[VB_MAX_PATH];
    int result;
    if (!vb_is_runtime_package_path(path)) return -RT_EINVAL;
    result = vb_http_build_url(url, sizeof(url), base_url, path);
    if (result != RT_EOK) return result;
    result = vb_build_staging_file_path(file_path, sizeof(file_path), app_id, path);
    if (result != RT_EOK) return result;
    return vb_http_download_to_file(url, file_path, required, max_size);
}

static int vb_http_download_listed_files(const char *app_id, const char *base_url, const char *files_txt_path)
{
    char *list;
    char *line;
    char *next;
    int len;
    int result = RT_EOK;

    if (!vb_file_exists(files_txt_path)) return RT_EOK;
    list = (char *)rt_malloc(VB_HTTP_FILES_TXT_MAX);
    if (!list) return -RT_ENOMEM;
    len = vb_read_text_file(files_txt_path, list, VB_HTTP_FILES_TXT_MAX);
    if (len < 0)
    {
        rt_free(list);
        return -RT_ERROR;
    }

    line = list;
    while (line && *line)
    {
        char *start;
        char *end;
        next = strchr(line, '\n');
        if (next)
        {
            *next = '\0';
            next++;
        }

        start = line;
        while (*start == ' ' || *start == '\t' || *start == '\r') start++;
        end = start + rt_strlen(start);
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        {
            *--end = '\0';
        }

        if (*start && *start != '#')
        {
            if (!vb_is_resource_package_path(start) || vb_is_core_package_path(start))
            {
                rt_kprintf("[vb_runtime][ota] invalid files.txt path: %s\n", start);
                result = -RT_EINVAL;
                break;
            }
            result = vb_http_download_package_file(app_id, base_url, start, 1,
                                                   VB_HTTP_MAX_RESOURCE_SIZE);
            if (result != RT_EOK) break;
        }

        line = next;
    }

    rt_free(list);
    return result;
}
#endif

static int vb_runtime_install_url_app(const char *app_id, const char *base_url)
{
#if VB_RUNTIME_HAS_HTTP_APP_OTA
    char staging_dir[VB_MAX_PATH];
    char files_txt_path[VB_MAX_PATH];
    int result;

    if (!vb_is_safe_app_id(app_id) || !vb_is_safe_http_base_url(base_url))
    {
        rt_kprintf("usage: vb_runtime_install_url <app_id> <http_base_url>\n");
        return -RT_EINVAL;
    }
    if (!g_vb_pan.pan_connected)
    {
        rt_kprintf("[vb_runtime][ota] pan not connected; run vb_runtime_pan_start and pair/tether first\n");
        return -RT_ERROR;
    }
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;

    result = vb_runtime_install_begin_app(app_id);
    if (result != RT_EOK) return result;

    result = vb_http_download_package_file(app_id, base_url, "manifest.json", 1,
                                           VB_HTTP_MAX_FILE_SIZE);
    if (result == RT_EOK)
    {
        result = vb_http_download_package_file(app_id, base_url, "main.lua", 1,
                                               VB_HTTP_MAX_FILE_SIZE);
    }
    if (result == RT_EOK)
    {
        result = vb_http_download_package_file(app_id, base_url, "app.info", 0,
                                               VB_HTTP_MAX_FILE_SIZE);
    }
    if (result == RT_EOK)
    {
        result = vb_http_download_package_file(app_id, base_url, "files.txt", 0,
                                               VB_HTTP_FILES_TXT_MAX);
    }
    if (result == RT_EOK &&
        vb_build_staging_file_path(files_txt_path, sizeof(files_txt_path), app_id, "files.txt") == RT_EOK)
    {
        result = vb_http_download_listed_files(app_id, base_url, files_txt_path);
    }
    if (result == RT_EOK)
    {
        result = vb_runtime_install_end_app(app_id);
    }

    if (result != RT_EOK)
    {
        vb_build_install_dir(staging_dir, sizeof(staging_dir), VIBEBOARD_STAGING_PREFIX, app_id);
        vb_remove_tree(staging_dir);
        rt_kprintf("[vb_runtime][ota] install url failed: %s (%d)\n", app_id, result);
        return result;
    }

    rt_kprintf("[vb_runtime][ota] install url complete: %s from %s\n", app_id, base_url);
    return RT_EOK;
#else
    (void)app_id;
    (void)base_url;
    rt_kprintf("[vb_runtime][ota] unavailable: bt-pan or webclient not built\n");
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_status_command(void)
{
    char active[VB_MAX_APP_ID];
    int fs = vb_prepare_filesystem();
    vb_read_active_app(active, sizeof(active));
    rt_kprintf("[vb_runtime] api=%s\n", VIBEBOARD_RUNTIME_API_VERSION);
    rt_kprintf("[vb_runtime] root=%s\n", VIBEBOARD_APP_ROOT);
    rt_kprintf("[vb_runtime] active=%s\n", active);
    rt_kprintf("[vb_runtime] fs=%s\n", fs == RT_EOK ? "ready" : "unavailable");
    rt_kprintf("[vb_runtime] lua=%s\n", vibeboard_lua_runtime_available() ? vibeboard_lua_runtime_name() : "manifest-fallback");
    rt_kprintf("[vb_runtime] transport=serial-msh\n");
    rt_kprintf("[vb_runtime] running=%d\n", g_vb_runtime.running);
    rt_kprintf("[vb_runtime] quiet_logs=%d\n", g_vb_runtime.quiet_logs);
    rt_kprintf("[vb_runtime] snake_active=%d\n", g_vb_runtime.snake.active);
    rt_kprintf("[vb_runtime] sensors_api=%s available=%d ready=%d\n",
               VIBEBOARD_RUNTIME_SENSOR_API_VERSION,
               g_vb_sensor.init_result != -RT_ENOSYS ? 1 : 0,
               g_vb_sensor.ready);
    return RT_EOK;
}

static int vb_runtime_net_status_command(void)
{
    rt_kprintf("[vb_runtime][net] api=%s\n", VIBEBOARD_RUNTIME_NET_API_VERSION);
    rt_kprintf("[vb_runtime][net] hardware=sf32lb52-mod-1\n");
    rt_kprintf("[vb_runtime][net] native_wifi=0\n");
    rt_kprintf("[vb_runtime][net] recommended_transport=ble-gatt-app-install\n");
#if VB_RUNTIME_HAS_BT_PAN
    rt_kprintf("[vb_runtime][net] available=1\n");
    rt_kprintf("[vb_runtime][net] transport=bt-pan\n");
    rt_kprintf("[vb_runtime][net] built=1\n");
    rt_kprintf("[vb_runtime][net] initialized=%d\n", g_vb_pan.initialized);
    rt_kprintf("[vb_runtime][net] bt_opened=%d\n", g_vb_pan.bt_opened);
    rt_kprintf("[vb_runtime][net] stack_ready=%d\n", g_vb_pan.stack_ready);
    rt_kprintf("[vb_runtime][net] name_requested=%d\n", g_vb_pan.name_requested);
    rt_kprintf("[vb_runtime][net] name_confirmed=%d\n", g_vb_pan.name_confirmed);
    rt_kprintf("[vb_runtime][net] final_name_requested=%d\n", g_vb_pan.final_name_requested);
    rt_kprintf("[vb_runtime][net] scan_mode=%d\n", g_vb_pan.scan_mode);
    rt_kprintf("[vb_runtime][net] target_scan_mode=%d\n", g_vb_pan.target_scan_mode);
    rt_kprintf("[vb_runtime][net] scan_mode_fsm=%d\n", g_vb_pan.scan_mode_fsm);
    rt_kprintf("[vb_runtime][net] scan_confirmed=%d\n", g_vb_pan.scan_confirmed);
    rt_kprintf("[vb_runtime][net] bt_connected=%d\n", g_vb_pan.bt_connected);
    rt_kprintf("[vb_runtime][net] pan_connected=%d\n", g_vb_pan.pan_connected);
    rt_kprintf("[vb_runtime][net] connecting=%d\n", g_vb_pan.connecting);
    rt_kprintf("[vb_runtime][net] pairing_pending=%d\n", g_vb_pan.pairing_pending);
    rt_kprintf("[vb_runtime][net] open_retries=%d\n", g_vb_pan.open_retries);
    rt_kprintf("[vb_runtime][net] last_error=%d\n", g_vb_pan.last_error);
    rt_kprintf("[vb_runtime][net] local_name=%s\n",
               g_vb_pan.local_name[0] ? g_vb_pan.local_name : VIBEBOARD_BT_PAN_NAME);
    rt_kprintf("[vb_runtime][net] lwip=1\n");
    rt_kprintf("[vb_runtime][net] app_ota_http=%d\n", VB_RUNTIME_HAS_HTTP_APP_OTA);
#else
    rt_kprintf("[vb_runtime][net] available=0\n");
    rt_kprintf("[vb_runtime][net] transport=bt-pan\n");
    rt_kprintf("[vb_runtime][net] reason=not_built\n");
#if defined(RT_USING_BLUETOOTH)
    rt_kprintf("[vb_runtime][net] bluetooth_config=1\n");
#else
    rt_kprintf("[vb_runtime][net] bluetooth_config=0\n");
#endif
#if defined(CFG_PAN)
    rt_kprintf("[vb_runtime][net] pan_config=1\n");
#else
    rt_kprintf("[vb_runtime][net] pan_config=0\n");
#endif
#if defined(RT_USING_LWIP)
    rt_kprintf("[vb_runtime][net] lwip=1\n");
#else
    rt_kprintf("[vb_runtime][net] lwip=0\n");
#endif
#endif
    return RT_EOK;
}

static int vb_runtime_sensors_status_command(void)
{
    char json[VB_SENSOR_JSON_MAX];
    int result = vb_runtime_sensors_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_sensor_probe_command(void)
{
#if defined(RT_USING_I2C)
#if defined(BSP_USING_I2C3)
    HAL_PIN_Set(PAD_PA40, I2C3_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA39, I2C3_SDA, PIN_PULLUP, 1);
#endif
    rt_kprintf("[vb_runtime][sensor] probe bus=i2c3\n");
#if defined(ASL_USING_LTR303)
    vb_runtime_sensor_probe_i2c("i2c3", LTR303_I2CADDR_DEFAULT, LTR303_PART_ID, "ltr303.part");
    vb_runtime_sensor_probe_i2c("i2c3", LTR303_I2CADDR_DEFAULT, LTR303_MANU_ID, "ltr303.manu");
#else
    rt_kprintf("[vb_runtime][sensor] ltr303 driver=0\n");
#endif
#if defined(MAG_USING_MMC56X3)
    vb_runtime_sensor_probe_i2c("i2c3", MMC56X3_DEFAULT_ADDRESS, MMC56X3_PRODUCT_ID, "mmc56x3.id");
#else
    rt_kprintf("[vb_runtime][sensor] mmc56x3 driver=0\n");
#endif
#if defined(ACC_USING_LSM6DSL)
    vb_runtime_sensor_probe_i2c("i2c3", LSM6DSL_ADDR_DEFAULT, 0x0F, "lsm6dsl.whoami");
    vb_runtime_sensor_probe_i2c("i2c3", 0x6B, 0x0F, "lsm6dsl.alt");
#else
    rt_kprintf("[vb_runtime][sensor] lsm6dsl driver=0\n");
#endif
    return RT_EOK;
#else
    rt_kprintf("[vb_runtime][sensor] probe unavailable: i2c not built\n");
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_component_sensor_text(const char *capability, char *dst, rt_size_t cap)
{
    if (!capability || !dst || cap == 0) return 0;
    if (rt_strcmp(capability, "sensor.light") == 0 ||
        rt_strcmp(capability, "vibeboard.sensor.light") == 0)
    {
        rt_snprintf(dst, cap, g_vb_sensor.have_light ? "%ld lux" : "--",
                    (long)g_vb_sensor.light_data.data.light);
        return 1;
    }
    if (rt_strcmp(capability, "sensor.mag") == 0 ||
        rt_strcmp(capability, "vibeboard.sensor.mag") == 0)
    {
        rt_snprintf(dst, cap, g_vb_sensor.have_mag ? "%ld,%ld,%ld" : "--",
                    (long)g_vb_sensor.mag_data.data.mag.x,
                    (long)g_vb_sensor.mag_data.data.mag.y,
                    (long)g_vb_sensor.mag_data.data.mag.z);
        return 1;
    }
    if (rt_strcmp(capability, "sensor.acce") == 0 ||
        rt_strcmp(capability, "sensor.accel") == 0 ||
        rt_strcmp(capability, "vibeboard.sensor.acce") == 0)
    {
        rt_snprintf(dst, cap, g_vb_sensor.have_acce ? "%ld,%ld,%ld mg" : "--",
                    (long)g_vb_sensor.acce_data.data.acce.x,
                    (long)g_vb_sensor.acce_data.data.acce.y,
                    (long)g_vb_sensor.acce_data.data.acce.z);
        return 1;
    }
    if (rt_strcmp(capability, "sensor.gyro") == 0 ||
        rt_strcmp(capability, "vibeboard.sensor.gyro") == 0)
    {
        rt_snprintf(dst, cap, g_vb_sensor.have_gyro ? "%ld,%ld,%ld mdps" : "--",
                    (long)g_vb_sensor.gyro_data.data.gyro.x,
                    (long)g_vb_sensor.gyro_data.data.gyro.y,
                    (long)g_vb_sensor.gyro_data.data.gyro.z);
        return 1;
    }
    if (rt_strcmp(capability, "sensor.step") == 0 ||
        rt_strcmp(capability, "vibeboard.sensor.step") == 0)
    {
        rt_snprintf(dst, cap, g_vb_sensor.have_step ? "%lu" : "--",
                    (unsigned long)g_vb_sensor.step_data.data.step);
        return 1;
    }
    return 0;
}

static void vb_timer_cb(lv_timer_t *timer)
{
    int i;
    char text[48];
    uint32_t now = rt_tick_get();
    int sensor_refreshed = 0;
    (void)timer;
    if (g_vb_runtime.pending_reload)
    {
        g_vb_runtime.pending_reload = 0;
        vb_runtime_reload_current();
        return;
    }
    g_vb_runtime.tick_count++;
    if (g_vb_runtime.clock_label &&
        now - g_vb_runtime.last_clock_update >= rt_tick_from_millisecond(VB_STATUS_TICK_REFRESH_MS))
    {
        rt_snprintf(text, sizeof(text), "tick=%lu", (unsigned long)g_vb_runtime.tick_count);
        lv_label_set_text(g_vb_runtime.clock_label, text);
        g_vb_runtime.last_clock_update = now;
    }
    for (i = 0; i < g_vb_runtime.component_count; i++)
    {
        vb_component_t *component = &g_vb_runtime.components[i];
        if (!component->value_label) continue;
        if (rt_strcmp(component->capability, "clock") == 0)
        {
            unsigned long seconds = rt_tick_get() / RT_TICK_PER_SECOND;
            rt_snprintf(text, sizeof(text), "%02lu:%02lu:%02lu",
                        (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);
            if (rt_strcmp(component->value, text) != 0)
            {
                vb_safe_copy(component->value, sizeof(component->value), text);
                lv_label_set_text(component->value_label, text);
            }
        }
        else if (rt_strncmp(component->capability, "sensor.", 7) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.sensor.", 17) == 0)
        {
            if (!sensor_refreshed)
            {
                vb_runtime_sensors_refresh();
                sensor_refreshed = 1;
            }
            if (vb_runtime_component_sensor_text(component->capability, text, sizeof(text)) &&
                rt_strcmp(component->value, text) != 0)
            {
                vb_safe_copy(component->value, sizeof(component->value), text);
                lv_label_set_text(component->value_label, text);
            }
        }
    }
    if (g_vb_runtime.script_runtime_active && g_vb_runtime.script_tick_label)
    {
        if (now - g_vb_runtime.script_tick_last_run >= g_vb_runtime.script_tick_period_ticks)
        {
            g_vb_runtime.script_tick_last_run = now;
            g_vb_runtime.script_tick_count++;
            rt_snprintf(text, sizeof(text), "%s %lu",
                        g_vb_runtime.script_tick_prefix[0] ? g_vb_runtime.script_tick_prefix : "Tick",
                        (unsigned long)g_vb_runtime.script_tick_count);
            lv_label_set_text(g_vb_runtime.script_tick_label, text);
            if (g_vb_runtime.script_tick_count <= 3)
            {
                rt_kprintf("[vb_runtime][lua] timer %s\n", text);
            }
        }
    }
    if (g_vb_runtime.snake.active && now - g_vb_runtime.snake.last_run >= g_vb_runtime.snake.period_ticks)
    {
        g_vb_runtime.snake.last_run = now;
        vb_snake_step();
    }
}

static void on_start(void)
{
    rt_memset(&g_vb_runtime, 0, sizeof(g_vb_runtime));
    g_vb_runtime.running = 1;
    g_vb_runtime.quiet_logs = 1;
    vb_prepare_filesystem();
    vb_load_active_package();
    g_vb_runtime.timer = lv_timer_create(vb_timer_cb, VB_TIMER_PERIOD_MS, RT_NULL);
    rt_kprintf("[vb_runtime] start api=%s root=%s\n", VIBEBOARD_RUNTIME_API_VERSION, VIBEBOARD_APP_ROOT);
}

static void on_stop(void)
{
    g_vb_runtime.running = 0;
    if (g_vb_runtime.timer)
    {
        lv_timer_del(g_vb_runtime.timer);
        g_vb_runtime.timer = RT_NULL;
    }
    vibeboard_lua_stop_app();
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    rt_kprintf("[vb_runtime] stop\n");
}

static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        on_start();
        break;
    case GUI_APP_MSG_ONSTOP:
        on_stop();
        break;
    default:
        break;
    }
}

static int app_main(intent_t i)
{
    (void)i;
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    rt_kprintf("[vb_runtime] registered\n");
    return 0;
}

static void vb_runtime_autorun_entry(void *parameter)
{
    int attempt;
    (void)parameter;
    rt_thread_mdelay(VB_AUTORUN_DELAY_MS);
    for (attempt = 0; attempt < 8; attempt++)
    {
        if (gui_app_run(APP_ID) == RT_EOK)
        {
            rt_kprintf("[vb_runtime] autorun requested\n");
            return;
        }
        rt_thread_mdelay(500);
    }
    rt_kprintf("[vb_runtime] autorun failed\n");
}

static int vb_runtime_autorun_init(void)
{
    rt_thread_t thread = rt_thread_create("vb_auto", vb_runtime_autorun_entry, RT_NULL,
                                          2048, RT_THREAD_PRIORITY_MIDDLE + 1,
                                          RT_THREAD_TICK_DEFAULT);
    if (thread) rt_thread_startup(thread);
    return RT_EOK;
}
INIT_APP_EXPORT(vb_runtime_autorun_init);

#ifdef FINSH_USING_MSH
static int vb_runtime_reload(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vb_runtime_request_reload();
    return RT_EOK;
}
MSH_CMD_EXPORT(vb_runtime_reload, reload VibeBoard runtime app);

static int vb_runtime_select(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_select <app_id>\n");
        return -RT_EINVAL;
    }
    return vb_runtime_select_app(argv[1]);
}
MSH_CMD_EXPORT(vb_runtime_select, select VibeBoard runtime app);

static int vb_runtime_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_status_command();
}
MSH_CMD_EXPORT(vb_runtime_status, show VibeBoard runtime status);

static int vb_runtime_net_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_net_status_command();
}
MSH_CMD_EXPORT(vb_runtime_net_status, show VibeBoard runtime network status);

static int vb_runtime_sensors(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_sensors_status_command();
}
MSH_CMD_EXPORT(vb_runtime_sensors, read VibeBoard built-in sensors as JSON);

static int vb_runtime_sensor_probe(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_sensor_probe_command();
}
MSH_CMD_EXPORT(vb_runtime_sensor_probe, probe VibeBoard built-in sensor I2C addresses);

static int vb_runtime_ble_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if VB_RUNTIME_HAS_BLE_INSTALL
    rt_kprintf("[vb_runtime][ble] api=%s name=%s init=%d power=%d service=%d adv=%d conn=%d notify=%d mtu=%d status=%s\n",
               VIBEBOARD_RUNTIME_BLE_API_VERSION, VIBEBOARD_BLE_NAME,
               g_vb_ble.initialized, g_vb_ble.power_on, g_vb_ble.service_ready,
               g_vb_ble.advertising, g_vb_ble.connected, g_vb_ble.notify_cccd ? 1 : 0,
               g_vb_ble.mtu, g_vb_ble.status);
    return RT_EOK;
#else
    rt_kprintf("[vb_runtime][ble] unavailable\n");
    return -RT_ENOSYS;
#endif
}
MSH_CMD_EXPORT(vb_runtime_ble_status, show VibeBoard BLE install status);

static int vb_runtime_pan_start(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_init();
}
MSH_CMD_EXPORT(vb_runtime_pan_start, start VibeBoard Bluetooth PAN service);

static int vb_runtime_pan_open(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_open_now();
}
MSH_CMD_EXPORT(vb_runtime_pan_open, reopen VibeBoard classic Bluetooth scan);

static int vb_runtime_pan_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_scan_now();
}
MSH_CMD_EXPORT(vb_runtime_pan_scan, enable VibeBoard Bluetooth inquiry and page scan);

static int vb_runtime_pan_probe(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_probe_now();
}
MSH_CMD_EXPORT(vb_runtime_pan_probe, probe VibeBoard Bluetooth PAN controller state);

static int vb_runtime_pan_connect(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_connect_now();
}
MSH_CMD_EXPORT(vb_runtime_pan_connect, connect VibeBoard PAN to paired phone or Mac);

static int vb_runtime_pan_inquiry(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_inquiry_now();
}
MSH_CMD_EXPORT(vb_runtime_pan_inquiry, scan nearby Bluetooth devices for PAN tethering);

static int vb_runtime_pan_connect_addr(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_pan_connect_addr <aa:bb:cc:dd:ee:ff>\n");
        return -RT_EINVAL;
    }
    return vb_pan_connect_addr_now(argv[1]);
}
MSH_CMD_EXPORT(vb_runtime_pan_connect_addr, connect VibeBoard PAN to Bluetooth MAC address);

static int vb_runtime_pan_forget(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_pan_forget();
}
MSH_CMD_EXPORT(vb_runtime_pan_forget, clear VibeBoard Bluetooth paired devices);

static int vb_runtime_install_begin(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_install_begin <app_id>\n");
        return -RT_EINVAL;
    }
    return vb_runtime_install_begin_app(argv[1]);
}
MSH_CMD_EXPORT(vb_runtime_install_begin, begin VibeBoard runtime app serial install);

static int vb_runtime_install_file(int argc, char **argv)
{
    if (argc < 5)
    {
        rt_kprintf("usage: vb_runtime_install_file <app_id> <path> <offset> <hex>\n");
        return -RT_EINVAL;
    }
    return vb_runtime_install_file_chunk(argv[1], argv[2], argv[3], argv[4]);
}
MSH_CMD_EXPORT(vb_runtime_install_file, write hex chunk to VibeBoard runtime app file);

static int vb_runtime_install_end(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_install_end <app_id>\n");
        return -RT_EINVAL;
    }
    return vb_runtime_install_end_app(argv[1]);
}
MSH_CMD_EXPORT(vb_runtime_install_end, finish VibeBoard runtime app serial install);

static int vb_runtime_install_url(int argc, char **argv)
{
    if (argc < 3)
    {
        rt_kprintf("usage: vb_runtime_install_url <app_id> <http_base_url>\n");
        return -RT_EINVAL;
    }
    return vb_runtime_install_url_app(argv[1], argv[2]);
}
MSH_CMD_EXPORT(vb_runtime_install_url, install VibeBoard runtime app from HTTP base URL);

static int vb_runtime_quiet(int argc, char **argv)
{
    if (argc >= 2)
    {
        g_vb_runtime.quiet_logs = atoi(argv[1]) ? 1 : 0;
    }
    rt_kprintf("[vb_runtime] quiet_logs=%d\n", g_vb_runtime.quiet_logs);
    return RT_EOK;
}
MSH_CMD_EXPORT(vb_runtime_quiet, set VibeBoard runtime app log quiet mode);
#endif

LV_IMG_DECLARE(img_remote);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(vibeboard_runtime), LV_EXT_IMG_GET(img_remote), APP_ID, app_main);
