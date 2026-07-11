#include <rtthread.h>
#include <rtdevice.h>
#include "drivers/rt_drv_pwm.h"
#if defined(BSP_USING_LCD)
#include "drv_lcd.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <dfs_file.h>
#include <dfs_fs.h>
#include <dfs_posix.h>
#if defined(AUDIO) && defined(AUDIO_USING_MANAGER)
#include "audio_server.h"
#endif
#if defined(RT_USING_ADC)
#include "bf0_hal.h"
#endif
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
#if defined(PKG_USING_WEBCLIENT) && defined(VB_RUNTIME_ENABLE_HTTP_APP_OTA)
#include <webclient.h>
#endif
#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH)
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#if defined(VB_RUNTIME_ENABLE_BT_PAN) && defined(CFG_PAN) && defined(RT_USING_LWIP)
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
#include "app_mem.h"
#include "vb_runtime_package.h"
#if defined(RGB_SK6812MINI_HS_ENABLE)
#include "bf0_hal.h"
#include "drv_io.h"
#endif

#define APP_ID "vb_runtime"
#define VIBEBOARD_RUNTIME_API_VERSION "vibeboard-huangshan-runtime/v1"
#define VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION "vibeboard-huangshan-app-manager/v1"
#define VIBEBOARD_RUNTIME_CAPABILITY_API_VERSION "vibeboard-huangshan-capabilities/v1"
#define VIBEBOARD_RUNTIME_BLE_API_VERSION "vibeboard-huangshan-ble-install/v1"
#define VIBEBOARD_RUNTIME_DISPLAY_API_VERSION "vibeboard-huangshan-display/v1"
#define VIBEBOARD_RUNTIME_SENSOR_API_VERSION "vibeboard-huangshan-sensors/v1"
#define VIBEBOARD_RUNTIME_FLOW_API_VERSION "vibeboard-huangshan-info-flow/v1"
#define VIBEBOARD_RUNTIME_VOICE_API_VERSION "vibeboard-huangshan-voice-bridge/v1"
#define VIBEBOARD_RUNTIME_POWER_API_VERSION "vibeboard-huangshan-power/v1"
#define VIBEBOARD_RUNTIME_GPIO_API_VERSION "vibeboard-huangshan-gpio/v1"
#define VIBEBOARD_RUNTIME_RGB_API_VERSION "vibeboard-huangshan-rgb/v1"
#define VIBEBOARD_RUNTIME_TOUCH_API_VERSION "vibeboard-huangshan-touch/v1"
#define VIBEBOARD_APP_ROOT "/sdcard/apps"
#define VIBEBOARD_ACTIVE_APP_FILE VIBEBOARD_APP_ROOT "/.active"
#define VIBEBOARD_FLOW_STATE_FILE VIBEBOARD_APP_ROOT "/.flow"
#define VIBEBOARD_DISPLAY_STATE_FILE VIBEBOARD_APP_ROOT "/.display"
#define VIBEBOARD_RGB_STATE_FILE VIBEBOARD_APP_ROOT "/.rgb"
#define VIBEBOARD_STAGING_PREFIX ".__install_"
#define VIBEBOARD_BACKUP_PREFIX ".__backup_"
#define VIBEBOARD_DEFAULT_APP_ID "welcome"
#define VIBEBOARD_FS_DEVICE "vbfs"
#define VIBEBOARD_BT_PAN_NAME "VibeBoard-PAN"
#define VIBEBOARD_BLE_NAME "VibeBoard"

#define VB_MAX_APP_ID 16
#define VB_MAX_PATH 160
#define VB_MAX_TEXT 96
#define VB_MAX_META 48
#define VB_MAX_VALUE 96
#define VB_MAX_URL 192
#define VB_MAX_MANIFEST 4096
#define VB_MAX_SCRIPT 8192
#define VB_MAX_SCRIPT_LINE 256
#define VB_MAX_COMPONENTS 8
#define VB_LAUNCHER_MAX_ITEMS 5
#define VB_MAX_SCRIPT_OBJECTS 96
#define VB_MAX_SCRIPT_NAME 24
#define VB_MAX_SCRIPT_ARGS 6
#define VB_MAX_SCRIPT_ARG 96
#define VB_MAX_HEX_CHARS 512
#define VB_AUTORUN_DELAY_MS 2200
#define VB_RUNTIME_AUTORUN_UI 0
#define VB_SCREEN_SAFE_LEFT 30
#define VB_SCREEN_SAFE_RIGHT 30
#define VB_SCREEN_SAFE_TOP 36
#define VB_SCREEN_SAFE_BOTTOM 36
#define VB_SCREEN_SAFE_WIDTH (LV_HOR_RES_MAX - VB_SCREEN_SAFE_LEFT - VB_SCREEN_SAFE_RIGHT)
#define VB_SCREEN_SAFE_HEIGHT (LV_VER_RES_MAX - VB_SCREEN_SAFE_TOP - VB_SCREEN_SAFE_BOTTOM)
#define VB_SCREEN_EDGE_BACK_X 72
#define VB_SCREEN_EDGE_BACK_DX 50
#define VB_SCREEN_EDGE_BACK_MAX_DY 120
#define VB_KEY_HOME_DEBOUNCE_MS 350
#define VB_TIMER_PERIOD_MS 200
#define VB_STATUS_TICK_REFRESH_MS 1000
#define VB_PAN_TIMER_MS 3000
#define VB_HTTP_HEADER_BUFSZ 1024
#define VB_HTTP_CHUNK_SIZE 512
#define VB_HTTP_MAX_FILE_SIZE 16384
#define VB_HTTP_MAX_RESOURCE_SIZE 131072
#define VB_HTTP_FILES_TXT_MAX 4096
#define VB_BLE_MAX_COMMAND 896
#define VB_BLE_STATUS_MAX 1024
#define VB_RUNTIME_INSTALL_MAX_CHUNK_BYTES 240
#define VB_BLE_EVT_RESTART_ADV 0x56524201u
#define VB_BLE_ADV_RESTART_DELAY_MS 600
#define VB_BLE_ADV_RESTART_INTERVAL_MS 700
#define VB_BLE_ADV_RESTART_ATTEMPTS 6
#define VB_BLE_ADV_FORCE_RESTART_DELAY_MS 250
#define VB_BLE_ADV_FORCE_RESTART_ATTEMPTS 8
#define VB_SENSOR_JSON_MAX 512
#define VB_POWER_JSON_MAX 384
#define VB_DISPLAY_JSON_MAX 384
#define VB_VOICE_JSON_MAX 384
#define VB_RGB_JSON_MAX 256
#define VB_TOUCH_JSON_MAX 384
#define VB_GPIO_JSON_MAX 448
#define VB_APP_JSON_MAX 4096
#define VB_JSON_READ_MAX 4096
#define VB_FLOW_MAX_CHANNEL 24
#define VB_FLOW_MAX_PAYLOAD 192
#define VB_FLOW_HISTORY 6
#define VB_VOICE_SAMPLE_RATE 16000
#define VB_VOICE_BITS_PER_SAMPLE 16
#define VB_VOICE_CHANNELS 1
#define VB_VOICE_MAX_MS 5000
#define VB_VOICE_DEFAULT_MS 1500
#define VB_VOICE_MAX_BYTES ((VB_VOICE_SAMPLE_RATE * VB_VOICE_BITS_PER_SAMPLE / 8 * VB_VOICE_CHANNELS * VB_VOICE_MAX_MS) / 1000)
#define VB_VOICE_CHUNK_BYTES 160
#define VB_VOICE_CACHE_SIZE 2048
#define VB_VOICE_THREAD_STACK 4096
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
#define VB_2048_SIZE 4
#define VB_2048_CELLS (VB_2048_SIZE * VB_2048_SIZE)
#define VB_2048_BOARD_SIZE 236
#define VB_2048_GAP 8
#define VB_2048_SWIPE_MIN_PRIMARY 16
#define VB_SNAKE_LOG_EVERY 120
#define VB_WEATHER_DROP_COUNT 8
#define VB_WEATHER_CLOUD_COUNT 12
#define VB_WEATHER_RAY_COUNT 8
#define VB_WEATHER_DECOR_COUNT 8
#define VB_WEATHER_FOG_COUNT 3
#define VB_WEATHER_PERIOD_MS 180
#define VB_WEATHER_IMG_MAGIC 0x474d4956u
#define VB_WEATHER_IMG_VERSION 1u

#if defined(VB_RUNTIME_ENABLE_BT_PAN) && defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH) && defined(CFG_PAN) && defined(RT_USING_LWIP)
#define VB_RUNTIME_HAS_BT_PAN 1
#else
#define VB_RUNTIME_HAS_BT_PAN 0
#endif

#if defined(VB_RUNTIME_ENABLE_HTTP_APP_OTA) && VB_RUNTIME_HAS_BT_PAN && defined(PKG_USING_WEBCLIENT)
#define VB_RUNTIME_HAS_HTTP_APP_OTA 1
#else
#define VB_RUNTIME_HAS_HTTP_APP_OTA 0
#endif

#if defined(ADC_VBAT_DEDICATED_CHANNEL)
#define VB_RUNTIME_BATTERY_ADC_CHANNEL ADC_VBAT_DEDICATED_CHANNEL
#elif defined(BSP_BATTERY_DETECT_ADC_CHANNEL)
#define VB_RUNTIME_BATTERY_ADC_CHANNEL BSP_BATTERY_DETECT_ADC_CHANNEL
#endif

#if defined(RT_USING_ADC) && defined(BSP_BATTERY_DETECT_ADC) && defined(VB_RUNTIME_BATTERY_ADC_CHANNEL)
#define VB_RUNTIME_HAS_POWER_BATTERY 1
#else
#define VB_RUNTIME_HAS_POWER_BATTERY 0
#endif

#if defined(RT_USING_I2C) && defined(CHARGE_USING_AW32001) && defined(BSP_BATTERY_USE_I2C_BUS)
#define VB_RUNTIME_HAS_POWER_CHARGER 1
#else
#define VB_RUNTIME_HAS_POWER_CHARGER 0
#endif

#if defined(BSP_USING_LCD)
#define VB_RUNTIME_HAS_DISPLAY 1
#else
#define VB_RUNTIME_HAS_DISPLAY 0
#endif

#define VB_DISPLAY_DEVICE_NAME "lcd"

#if defined(BSP_USING_GPIO) && ((defined(BSP_USING_KEY1) && defined(BSP_KEY1_PIN)) || (defined(BSP_USING_KEY2) && defined(BSP_KEY2_PIN)))
#define VB_RUNTIME_HAS_GPIO 1
#else
#define VB_RUNTIME_HAS_GPIO 0
#endif

#if defined(BSP_KEY1_ACTIVE_HIGH)
#define VB_RUNTIME_GPIO_KEY1_ACTIVE_HIGH 1
#else
#define VB_RUNTIME_GPIO_KEY1_ACTIVE_HIGH 0
#endif

#if defined(BSP_KEY2_ACTIVE_HIGH)
#define VB_RUNTIME_GPIO_KEY2_ACTIVE_HIGH 1
#else
#define VB_RUNTIME_GPIO_KEY2_ACTIVE_HIGH 0
#endif

#if defined(RGB_SK6812MINI_HS_ENABLE) && defined(RGB_USING_SK6812MINI_HS_DEV_NAME)
#define VB_RUNTIME_HAS_RGB 1
#else
#define VB_RUNTIME_HAS_RGB 0
#endif

#define VB_AW32001_I2C_ADDRESS 0x49
#define VB_AW32001_REG_POWERON_CONF 0x01
#define VB_AW32001_REG_SYS_STATUS 0x08
#define VB_AW32001_REG_FAULT 0x09

#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH) && defined(BSP_BLE_SIBLES)
#define VB_RUNTIME_HAS_BLE_INSTALL 1
#else
#define VB_RUNTIME_HAS_BLE_INSTALL 0
#endif

#if defined(AUDIO) && defined(AUDIO_USING_MANAGER)
#define VB_RUNTIME_HAS_VOICE_CAPTURE 1
#else
#define VB_RUNTIME_HAS_VOICE_CAPTURE 0
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
    int active;
    int board[VB_2048_SIZE][VB_2048_SIZE];
    int score;
    int best_score;
    int moves;
    int game_over;
    int drag_consumed;
    uint32_t rng;
    lv_obj_t *board_panel;
    lv_obj_t *tiles[VB_2048_CELLS];
    lv_obj_t *labels[VB_2048_CELLS];
    lv_obj_t *score_label;
    lv_obj_t *status_label;
} vb_2048_state_t;


typedef enum
{
    VB_WEATHER_SUNNY = 0,
    VB_WEATHER_CLOUDY,
    VB_WEATHER_RAIN,
    VB_WEATHER_SNOW,
    VB_WEATHER_STORM,
    VB_WEATHER_FOG
} vb_weather_kind_t;

typedef struct
{
    int active;
    vb_weather_kind_t kind;
    int temp_c;
    int humidity;
    int weather_code;
    int frame;
    uint32_t last_run;
    uint32_t period_ticks;
    char city[VB_MAX_TEXT];
    char condition[32];
    lv_obj_t *pet;
    lv_obj_t *image;
    lv_img_dsc_t *image_dsc;
    int image_raw;
    lv_obj_t *sun;
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;
    lv_obj_t *mouth;
    lv_obj_t *blush_l;
    lv_obj_t *blush_r;
    lv_obj_t *cloud[VB_WEATHER_CLOUD_COUNT];
    lv_obj_t *rays[VB_WEATHER_RAY_COUNT];
    lv_obj_t *decor[VB_WEATHER_DECOR_COUNT];
    lv_obj_t *drops[VB_WEATHER_DROP_COUNT];
    lv_obj_t *bolt;
    lv_obj_t *fog[VB_WEATHER_FOG_COUNT];
    lv_obj_t *title_label;
    lv_obj_t *summary_label;
} vb_weather_state_t;

typedef struct
{
    uint32_t sequence;
    uint32_t bytes;
    uint32_t tick;
    char channel[VB_FLOW_MAX_CHANNEL];
    char payload[VB_FLOW_MAX_PAYLOAD + 1];
} vb_info_flow_item_t;

typedef struct
{
    uint32_t total_count;
    int count;
    int write_index;
    vb_info_flow_item_t items[VB_FLOW_HISTORY];
} vb_info_flow_state_t;

typedef struct
{
    int initialized;
    int recording;
    int ready;
    int last_error;
    uint32_t sequence;
    uint32_t requested_ms;
    uint32_t recorded_bytes;
    uint32_t dropped_bytes;
    uint32_t max_bytes;
    uint8_t *buffer;
#if VB_RUNTIME_HAS_VOICE_CAPTURE
    audio_client_t client;
    rt_sem_t done_sem;
    rt_thread_t worker;
    int stop_requested;
#endif
} vb_voice_state_t;

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

typedef struct
{
    int available;
    int ready;
    int active;
    int last_event;
    int gesture_dir;
    uint32_t count;
    uint32_t last_tick;
    uint32_t press_tick;
    uint32_t last_duration_ms;
    lv_point_t point;
    lv_point_t press_point;
    lv_point_t release_point;
    lv_point_t delta;
} vb_touch_state_t;

typedef struct
{
    int available;
    int ready;
    int configured;
    uint32_t read_count;
    int key1_ok;
    int key1_level;
    int key1_pressed;
    int key2_ok;
    int key2_level;
    int key2_pressed;
} vb_gpio_state_t;

typedef struct
{
    int available;
    int ready;
    int width;
    int height;
    int bpp;
    int format;
    int align;
    int brightness;
    int state;
    const char *state_name;
} vb_display_snapshot_t;

#if VB_RUNTIME_HAS_BLE_INSTALL
typedef struct
{
    int initialized;
    int power_on;
    int service_ready;
    int adv_configured;
    int advertising;
    int connected;
    uint8_t last_adv_start_rc;
    uint8_t last_adv_stop_rc;
    uint8_t last_adv_stop_reason;
    uint32_t adv_start_events;
    uint32_t adv_stop_events;
    uint32_t adv_restart_requests;
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
    char id[VB_MAX_APP_ID];
    char name[VB_MAX_TEXT];
    char description[VB_MAX_TEXT];
    char category[VB_MAX_META];
    char icon[VB_MAX_META];
    char author[VB_MAX_META];
    char screenshot[VB_MAX_TEXT];
    char requirements[VB_MAX_TEXT];
    int manifest;
    int app_info;
    int main_lua;
    int compatible;
    int active;
} vb_app_summary_t;

typedef struct
{
    char id[VB_MAX_APP_ID];
} vb_launcher_item_t;

typedef struct
{
    int delta;
} vb_launcher_page_action_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *status_label;
    lv_obj_t *clock_label;
    lv_obj_t *flow_label;
    lv_obj_t *overlay_apps_button;
    lv_obj_t *overlay_nav_zone;
    lv_obj_t *script_flow_label;
    char script_flow_field[24];
    uint32_t script_flow_seen_total;
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
    vb_2048_state_t game2048;
    vb_weather_state_t weather;
    volatile int pending_reload;
    volatile int pending_stop;
    volatile int pending_manager_refresh;
    int running;
    int app_running;
    int app_failed;
    int app_last_status;
    uint32_t app_launch_count;
    uint32_t app_stop_count;
    char app_last_error[VB_MAX_TEXT];
    char manager_message[VB_MAX_TEXT];
    char launcher_pending_delete[VB_MAX_APP_ID];
    vb_launcher_item_t launcher_items[VB_LAUNCHER_MAX_ITEMS];
    vb_launcher_page_action_t launcher_page_actions[2];
    int launcher_count;
    int launcher_total;
    int launcher_page;
    int fs_ready;
    int fs_mounted;
    int quiet_logs;
    int install_fd;
    long install_offset;
    char install_path[VB_MAX_PATH];
    char install_app[VB_MAX_APP_ID];
    uint32_t tick_count;
    uint32_t last_clock_update;
    uint32_t flow_seen_total;
    rt_uint32_t rgb_color;
    int rgb_last_ready;
    int display_brightness;
    int display_target_brightness;
    int display_last_ready;
    vb_gpio_state_t gpio;
    vb_touch_state_t touch;
    int home_key_last_pressed;
    uint32_t home_key_last_tick;
} vb_runtime_state_t;

static vb_runtime_state_t g_vb_runtime;
static int g_vb_runtime_state_initialized;

static void vb_runtime_state_defaults(void)
{
    g_vb_runtime.install_fd = -1;
    g_vb_runtime.quiet_logs = 1;
    g_vb_runtime.rgb_color = 0x000000u;
    g_vb_runtime.display_brightness = -1;
    g_vb_runtime.display_target_brightness = -1;
}

static void vb_runtime_state_bootstrap(void)
{
    if (g_vb_runtime_state_initialized) return;
    rt_memset(&g_vb_runtime, 0, sizeof(g_vb_runtime));
    vb_runtime_state_defaults();
    g_vb_runtime_state_initialized = 1;
}
static vb_info_flow_state_t g_vb_flow;
static vb_voice_state_t g_vb_voice;
#if VB_RUNTIME_HAS_BT_PAN
static vb_pan_state_t g_vb_pan;
#endif
static vb_sensor_state_t g_vb_sensor;
#if VB_RUNTIME_HAS_BLE_INSTALL
static vb_ble_install_state_t g_vb_ble;
#endif

static int vb_builtin_script_start(const char *script_path, const char *manifest_path);
static void vb_builtin_script_stop(void);
static lv_obj_t *vb_create_label(lv_obj_t *parent, const char *text, uint16_t font_size,
                                 lv_color_t color);
#if VB_RUNTIME_HAS_BT_PAN
static int vb_pan_init(void);
static int vb_pan_open_now(void);
static int vb_pan_scan_now(void);
static int vb_pan_probe_now(void);
static int vb_pan_connect_now(void);
static int vb_pan_forget(void);
#endif
static int vb_runtime_install_begin_app(const char *app_id);
static int vb_runtime_install_file_chunk(const char *app_id, const char *path,
                                         const char *offset_text, const char *hex);
static int vb_runtime_install_end_app(const char *app_id);
static int vb_runtime_install_abort_app(const char *app_id);
static int vb_runtime_staging_clear_all(int *removed);
static void vb_runtime_install_close_file(void);
static int vb_runtime_select_app(const char *app_id);
static void vb_runtime_request_reload(void);
static void vb_runtime_show_navigation_controls(void);
static void vb_runtime_clear_overlay_controls(void);
static void vb_runtime_return_home(void);
static void vb_runtime_touch_event_cb(lv_event_t *event);
static void vb_weather_release_image(void);
#if VB_RUNTIME_HAS_HTTP_APP_OTA
static int vb_runtime_install_url_app(const char *app_id, const char *base_url);
#endif
static int vb_runtime_app_status_json(char *dst, rt_size_t cap);
static int vb_runtime_app_list_json(char *dst, rt_size_t cap);
static int vb_runtime_app_list_page_json(char *dst, rt_size_t cap, int offset, int limit);
static int vb_runtime_app_launch(const char *app_id);
static int vb_runtime_app_stop(void);
static int vb_runtime_app_delete(const char *app_id);
static void vb_runtime_request_manager_refresh(const char *message);
#if 0
static void vb_render_app_manager_ui(const char *reason);
#endif
static void vb_runtime_stop_current_app(void);
static int vb_runtime_app_status_command(void);
static int vb_runtime_apps_status_command(void);
static void vb_read_active_app(char *dst, rt_size_t cap);
static int vb_runtime_sensors_read_json(char *dst, rt_size_t cap);
static int vb_runtime_sensors_status_command(void);
static int vb_runtime_power_read_json(char *dst, rt_size_t cap);
static int vb_runtime_power_format_text(const char *capability, char *dst, rt_size_t cap);
static int vb_runtime_power_status_command(void);
static int vb_runtime_display_read_json(char *dst, rt_size_t cap);
static int vb_runtime_display_set_brightness_text(const char *brightness_text, char *dst, rt_size_t cap);
static int vb_runtime_display_apply_target(void);
static int vb_runtime_display_load_state(void);
static int vb_runtime_display_format_text(const char *selector, char *dst, rt_size_t cap);
static int vb_runtime_display_status_command(void);
static int vb_runtime_gpio_read_json(char *dst, rt_size_t cap);
static int vb_runtime_gpio_status_command(void);
static int vb_runtime_touch_read_json(char *dst, rt_size_t cap);
static int vb_runtime_touch_status_command(void);
static int vb_runtime_rgb_read_json(char *dst, rt_size_t cap);
static int vb_runtime_rgb_set_text(const char *color_text, char *dst, rt_size_t cap);
static int vb_runtime_rgb_load_state(void);
static int vb_runtime_rgb_status_command(void);
static int vb_runtime_capabilities_json(char *dst, rt_size_t cap);
static int vb_runtime_capabilities_status_command(void);
static int vb_runtime_ble_json_read(const char *kind, uint32_t offset, uint32_t max_bytes, char *dst, rt_size_t cap);
static const char *vb_json_find_string(const char *begin, const char *end, const char *key);
static int vb_json_read_int(const char *begin, const char *end, const char *key, int *out);
static void vb_json_copy_string(const char *begin, const char *end, const char *key,
                                char *dst, rt_size_t cap, const char *fallback);
static void vb_json_copy_string_array_csv(const char *begin, const char *end, const char *key,
                                          char *dst, rt_size_t cap, const char *fallback);
static int vb_runtime_flow_send_hex(const char *channel, uint32_t sequence, const char *hex);
static int vb_runtime_flow_latest_index(void);
static int vb_runtime_flow_save_latest(void);
static int vb_runtime_flow_load_state(void);
static void vb_runtime_flow_clear_state(void);
static int vb_runtime_flow_status_command(void);
static int vb_runtime_voice_start(uint32_t duration_ms);
static void vb_runtime_voice_clear(void);
static int vb_runtime_voice_status(char *dst, rt_size_t cap);
static int vb_runtime_voice_read_json(char *dst, rt_size_t cap);
static int vb_runtime_voice_start_text(const char *duration_text, char *dst, rt_size_t cap);
static int vb_runtime_voice_clear_json(char *dst, rt_size_t cap);
static int vb_runtime_voice_format_text(const char *selector, char *dst, rt_size_t cap);
static int vb_runtime_voice_read_hex(uint32_t offset, uint32_t max_bytes, char *dst, rt_size_t cap);
static int vb_runtime_voice_json_status_command(void);
static int vb_runtime_voice_status_command(void);

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
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int len;
    int written;
    int result = RT_EOK;
    if (fd < 0) return -RT_ERROR;
    len = rt_strlen(text);
    written = write(fd, text, len);
    if (written != len)
    {
        result = -RT_ERROR;
    }
    else if (fsync(fd) != 0)
    {
        result = -RT_ERROR;
    }
    close(fd);
    return result;
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
    unsigned long sample_count;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    result = vb_runtime_sensors_refresh();
    sample_count = (unsigned long)(g_vb_sensor.have_light + g_vb_sensor.have_mag +
                                   g_vb_sensor.have_acce + g_vb_sensor.have_gyro +
                                   g_vb_sensor.have_step);
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
                       sample_count,
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

static int vb_runtime_power_read_battery(int *available, int *ready, rt_uint32_t *raw,
                                         rt_uint32_t *mv, const char **device_name,
                                         int *channel)
{
    int rc = -RT_ENOSYS;
    rt_uint32_t raw_value = 0;
    rt_uint32_t mv_value = 0;

    if (available) *available = 0;
    if (ready) *ready = 0;
    if (raw) *raw = 0;
    if (mv) *mv = 0;
    if (device_name) *device_name = "";
    if (channel) *channel = -1;

#if VB_RUNTIME_HAS_POWER_BATTERY
    {
        rt_device_t dev;
        if (available) *available = 1;
        if (device_name) *device_name = BSP_BATTERY_DETECT_ADC;
        if (channel) *channel = VB_RUNTIME_BATTERY_ADC_CHANNEL;

        dev = rt_device_find(BSP_BATTERY_DETECT_ADC);
        if (!dev) return -RT_ERROR;

        rc = rt_adc_enable((rt_adc_device_t)dev, VB_RUNTIME_BATTERY_ADC_CHANNEL);
        if (rc != RT_EOK) return rc;

        raw_value = rt_adc_read((rt_adc_device_t)dev, VB_RUNTIME_BATTERY_ADC_CHANNEL);
        rt_adc_disable((rt_adc_device_t)dev, VB_RUNTIME_BATTERY_ADC_CHANNEL);
        if (raw_value == (rt_uint32_t)RT_ERROR || raw_value == 0) return -RT_ERROR;

#if defined(SF32LB52X)
        mv_value = raw_value / 10;
#else
        mv_value = raw_value;
#endif
        if (ready) *ready = 1;
        if (raw) *raw = raw_value;
        if (mv) *mv = mv_value;
        return RT_EOK;
    }
#endif

    return rc;
}

static const char *vb_runtime_charger_state_label(int state)
{
    switch (state)
    {
    case 0:
        return "no_charging";
    case 1:
        return "pre_charge";
    case 2:
        return "charging";
    case 3:
        return "full";
    default:
        return "unavailable";
    }
}

static int vb_runtime_power_read_charger(int *available, int *ready, rt_uint8_t *sys,
                                         rt_uint8_t *fault, int *state,
                                         int *detected, int *enabled,
                                         const char **status)
{
    int rc = -RT_ENOSYS;

    if (available) *available = 0;
    if (ready) *ready = 0;
    if (sys) *sys = 0;
    if (fault) *fault = 0;
    if (state) *state = -1;
    if (detected) *detected = -1;
    if (enabled) *enabled = -1;
    if (status) *status = "unavailable";

#if VB_RUNTIME_HAS_POWER_CHARGER
    {
        struct rt_i2c_bus_device *bus;
        rt_uint8_t sys_reg = 0;
        rt_uint8_t fault_reg = 0;
        rt_uint8_t poweron_reg = 0;
        int charge_state;
        rt_size_t result;

        if (available) *available = 1;
        bus = rt_i2c_bus_device_find(BSP_BATTERY_USE_I2C_BUS);
        if (!bus) return -RT_ERROR;

        result = rt_i2c_mem_read(bus, VB_AW32001_I2C_ADDRESS, VB_AW32001_REG_SYS_STATUS, 8, &sys_reg, 1);
        if (result <= 0) return -RT_ERROR;
        result = rt_i2c_mem_read(bus, VB_AW32001_I2C_ADDRESS, VB_AW32001_REG_FAULT, 8, &fault_reg, 1);
        if (result <= 0) return -RT_ERROR;

        charge_state = (sys_reg & 0x18) >> 3;
        if (sys) *sys = sys_reg;
        if (fault) *fault = fault_reg;
        if (state) *state = charge_state;
        if (status) *status = vb_runtime_charger_state_label(charge_state);
        if (ready) *ready = 1;
        rc = RT_EOK;

        result = rt_i2c_mem_read(bus, VB_AW32001_I2C_ADDRESS, VB_AW32001_REG_POWERON_CONF, 8,
                                 &poweron_reg, 1);
        if (result > 0 && enabled) *enabled = (poweron_reg & (1 << 3)) ? 0 : 1;

#if defined(BSP_USING_CHARGER_DETECT) && defined(BSP_CHARGER_INT_PIN)
#ifdef BSP_CHARGER_INT_PIN_ACTIVE_HIGH
        if (detected) *detected = rt_pin_read(BSP_CHARGER_INT_PIN) ? 1 : 0;
#else
        if (detected) *detected = rt_pin_read(BSP_CHARGER_INT_PIN) ? 0 : 1;
#endif
#endif
        return rc;
    }
#endif

    return rc;
}

static int vb_runtime_power_read_json(char *dst, rt_size_t cap)
{
    int used;
    int battery_available = 0;
    int battery_ready = 0;
    int charger_available = 0;
    int charger_ready = 0;
    int available;
    int ready;
    const char *device_name = "";
    int channel = -1;
    rt_uint32_t raw = 0;
    rt_uint32_t mv = 0;
    rt_uint8_t charger_sys = 0;
    rt_uint8_t charger_fault = 0;
    int charger_state = -1;
    int charger_detected = -1;
    int charger_enabled = -1;
    const char *charger_status = "unavailable";
    int battery_rc;
    int charger_rc;

    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    battery_rc = vb_runtime_power_read_battery(&battery_available, &battery_ready, &raw, &mv,
                                               &device_name, &channel);
    charger_rc = vb_runtime_power_read_charger(&charger_available, &charger_ready, &charger_sys,
                                               &charger_fault, &charger_state, &charger_detected,
                                               &charger_enabled, &charger_status);
    available = (battery_available || charger_available) ? 1 : 0;
    ready = (battery_ready || charger_ready) ? 1 : 0;

    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,"
                       "\"battery\":{\"ok\":%d,\"mv\":%lu,\"raw\":%lu,\"dev\":\"%s\",\"ch\":%d},"
                       "\"charger\":{\"ok\":%d,\"state\":%d,\"status\":\"%s\",\"det\":%d,\"en\":%d,\"sys\":%u,\"fault\":%u}}",
                       VIBEBOARD_RUNTIME_POWER_API_VERSION,
                       available,
                       ready,
                       battery_ready,
                       (unsigned long)mv,
                       (unsigned long)raw,
                       device_name,
                       channel,
                       charger_ready,
                       charger_state,
                       charger_status,
                       charger_detected,
                       charger_enabled,
                       (unsigned int)charger_sys,
                       (unsigned int)charger_fault);
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_POWER_API_VERSION,
                    available,
                    ready);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    if (ready) return RT_EOK;
    if (battery_rc != -RT_ENOSYS) return battery_rc;
    return charger_rc;
}

static const char *vb_runtime_power_short_capability(const char *capability)
{
    if (!capability) return "";
    if (rt_strncmp(capability, "power.", 6) == 0) return capability + 6;
    if (rt_strncmp(capability, "vibeboard.power.", 16) == 0) return capability + 16;
    return capability;
}

static int vb_runtime_power_format_text(const char *capability, char *dst, rt_size_t cap)
{
    const char *short_cap = vb_runtime_power_short_capability(capability);

    if (!short_cap || !dst || cap == 0) return 0;
    if (rt_strcmp(short_cap, "battery") == 0)
    {
        rt_uint32_t mv = 0;
        if (vb_runtime_power_read_battery(RT_NULL, RT_NULL, RT_NULL, &mv, RT_NULL, RT_NULL) == RT_EOK)
        {
            rt_snprintf(dst, cap, "%lumV", (unsigned long)mv);
        }
        else
        {
            rt_snprintf(dst, cap, "Battery --");
        }
        return 1;
    }
    if (rt_strcmp(short_cap, "charger") == 0 || rt_strcmp(short_cap, "charger.status") == 0)
    {
        const char *status = "unavailable";
        if (vb_runtime_power_read_charger(RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL,
                                          RT_NULL, RT_NULL, &status) == RT_EOK)
        {
            rt_snprintf(dst, cap, "%s", status);
        }
        else
        {
            rt_snprintf(dst, cap, "Charger --");
        }
        return 1;
    }
    if (rt_strcmp(short_cap, "charger.state") == 0)
    {
        int state = -1;
        if (vb_runtime_power_read_charger(RT_NULL, RT_NULL, RT_NULL, RT_NULL, &state,
                                          RT_NULL, RT_NULL, RT_NULL) == RT_EOK)
        {
            rt_snprintf(dst, cap, "%d", state);
        }
        else
        {
            rt_snprintf(dst, cap, "--");
        }
        return 1;
    }
    if (rt_strcmp(short_cap, "charger.det") == 0)
    {
        int detected = -1;
        if (vb_runtime_power_read_charger(RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL,
                                          &detected, RT_NULL, RT_NULL) == RT_EOK)
        {
            rt_snprintf(dst, cap, "%d", detected);
        }
        else
        {
            rt_snprintf(dst, cap, "--");
        }
        return 1;
    }
    if (rt_strcmp(short_cap, "charger.en") == 0)
    {
        int enabled = -1;
        if (vb_runtime_power_read_charger(RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL,
                                          RT_NULL, &enabled, RT_NULL) == RT_EOK)
        {
            rt_snprintf(dst, cap, "%d", enabled);
        }
        else
        {
            rt_snprintf(dst, cap, "--");
        }
        return 1;
    }
    if (rt_strcmp(short_cap, "charger.fault") == 0)
    {
        rt_uint8_t fault = 0;
        if (vb_runtime_power_read_charger(RT_NULL, RT_NULL, RT_NULL, &fault, RT_NULL,
                                          RT_NULL, RT_NULL, RT_NULL) == RT_EOK)
        {
            rt_snprintf(dst, cap, "%u", (unsigned int)fault);
        }
        else
        {
            rt_snprintf(dst, cap, "--");
        }
        return 1;
    }
    return 0;
}


static const char *vb_runtime_display_state_name(int state)
{
#if VB_RUNTIME_HAS_DISPLAY
    switch (state)
    {
    case LCD_STATUS_NONE: return "none";
    case LCD_STATUS_OPENING: return "opening";
    case LCD_STATUS_NOT_FIND_LCD: return "not_found";
    case LCD_STATUS_INITIALIZED: return "initialized";
    case LCD_STATUS_DISPLAY_ON: return "on";
    case LCD_STATUS_DISPLAY_OFF_PENDING: return "off_pending";
    case LCD_STATUS_DISPLAY_OFF: return "off";
    case LCD_STATUS_DISPLAY_TIMEOUT: return "timeout";
    case LCD_STATUS_IDLE_MODE: return "idle";
    default: return "unknown";
    }
#else
    (void)state;
    return "unavailable";
#endif
}

static int vb_runtime_display_read(vb_display_snapshot_t *snapshot)
{
    if (!snapshot) return -RT_EINVAL;
    rt_memset(snapshot, 0, sizeof(*snapshot));
    snapshot->available = VB_RUNTIME_HAS_DISPLAY ? 1 : 0;
    snapshot->brightness = g_vb_runtime.display_brightness >= 0 ? g_vb_runtime.display_brightness : -1;
    snapshot->state = -1;
    snapshot->state_name = "unavailable";

#if VB_RUNTIME_HAS_DISPLAY
    {
        rt_device_t dev = rt_device_find(VB_DISPLAY_DEVICE_NAME);
        struct rt_device_graphic_info info;
        rt_uint8_t brightness = 0;
        int result;
        if (!dev) return -RT_ERROR;

        result = rt_device_control(dev, RTGRAPHIC_CTRL_GET_INFO, &info);
        if (result != RT_EOK) return result;
        snapshot->width = info.width;
        snapshot->height = info.height;
        snapshot->bpp = info.bits_per_pixel;
        snapshot->format = info.pixel_format;
        snapshot->align = info.draw_align;
        snapshot->ready = (snapshot->width > 0 && snapshot->height > 0) ? 1 : 0;

        if (rt_device_control(dev, RTGRAPHIC_CTRL_GET_BRIGHTNESS, &brightness) == RT_EOK)
        {
            if (g_vb_runtime.display_target_brightness < 0)
            {
                (void)vb_runtime_display_load_state();
            }
            if (g_vb_runtime.display_target_brightness >= 0)
            {
                (void)vb_runtime_display_apply_target();
                brightness = (rt_uint8_t)g_vb_runtime.display_target_brightness;
            }
            snapshot->brightness = brightness;
            g_vb_runtime.display_brightness = brightness;
        }
        {
            LCD_DrvStatusTypeDef state = LCD_STATUS_NONE;
            if (rt_device_control(dev, RTGRAPHIC_CTRL_GET_STATE, &state) == RT_EOK)
            {
                snapshot->state = (int)state;
                snapshot->state_name = vb_runtime_display_state_name((int)state);
            }
            else
            {
                snapshot->state_name = "unknown";
            }
        }
        g_vb_runtime.display_last_ready = snapshot->ready;
        return snapshot->ready ? RT_EOK : -RT_ERROR;
    }
#else
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_display_write_json(char *dst, rt_size_t cap, int rc, const vb_display_snapshot_t *snapshot)
{
    int used;
    vb_display_snapshot_t empty;
    const vb_display_snapshot_t *s = snapshot;
    if (!dst || cap == 0) return -RT_EINVAL;
    if (!s)
    {
        rt_memset(&empty, 0, sizeof(empty));
        empty.available = VB_RUNTIME_HAS_DISPLAY ? 1 : 0;
        empty.brightness = g_vb_runtime.display_brightness >= 0 ? g_vb_runtime.display_brightness : -1;
        empty.state = -1;
        empty.state_name = "unavailable";
        s = &empty;
    }
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"ok\":%d,\"dev\":\"%s\","
                       "\"width\":%d,\"height\":%d,\"bpp\":%d,\"format\":%d,\"align\":%d,"
                       "\"brightness\":%d,\"state\":%d,\"state_name\":\"%s\"}",
                       VIBEBOARD_RUNTIME_DISPLAY_API_VERSION,
                       s->available,
                       s->ready,
                       rc == RT_EOK ? 1 : 0,
                       VB_DISPLAY_DEVICE_NAME,
                       s->width,
                       s->height,
                       s->bpp,
                       s->format,
                       s->align,
                       s->brightness,
                       s->state,
                       s->state_name ? s->state_name : "unknown");
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_DISPLAY_API_VERSION,
                    s->available,
                    s->ready);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return rc;
}

static int vb_runtime_display_read_json(char *dst, rt_size_t cap)
{
    vb_display_snapshot_t snapshot;
    int rc = vb_runtime_display_read(&snapshot);
    return vb_runtime_display_write_json(dst, cap, rc, &snapshot);
}

static int vb_runtime_display_apply_target(void)
{
#if VB_RUNTIME_HAS_DISPLAY
    rt_device_t dev;
    rt_uint8_t level;
    if (g_vb_runtime.display_target_brightness < 0) return RT_EOK;
    dev = rt_device_find(VB_DISPLAY_DEVICE_NAME);
    if (!dev) return -RT_ERROR;
    level = (rt_uint8_t)g_vb_runtime.display_target_brightness;
    if (rt_device_control(dev, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &level) != RT_EOK) return -RT_ERROR;
    g_vb_runtime.display_brightness = g_vb_runtime.display_target_brightness;
    g_vb_runtime.display_last_ready = 1;
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_display_save_state(void)
{
    char json[96];
    if (g_vb_runtime.display_target_brightness < 0) return RT_EOK;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    rt_snprintf(json, sizeof(json),
                "{\"api\":\"%s\",\"brightness\":%d}\n",
                VIBEBOARD_RUNTIME_DISPLAY_API_VERSION,
                g_vb_runtime.display_target_brightness);
    json[sizeof(json) - 1] = '\0';
    return vb_write_text_file(VIBEBOARD_DISPLAY_STATE_FILE, json);
}

static int vb_runtime_display_load_state(void)
{
    char json[128];
    char api[64];
    int brightness = -1;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    if (vb_read_text_file(VIBEBOARD_DISPLAY_STATE_FILE, json, sizeof(json)) <= 0) return -RT_ERROR;
    vb_json_copy_string(json, RT_NULL, "api", api, sizeof(api), "");
    if (rt_strcmp(api, VIBEBOARD_RUNTIME_DISPLAY_API_VERSION) != 0) return -RT_EINVAL;
    if (!vb_json_read_int(json, RT_NULL, "brightness", &brightness)) return -RT_EINVAL;
    if (brightness < 0 || brightness > 100) return -RT_EINVAL;
    g_vb_runtime.display_target_brightness = brightness;
    g_vb_runtime.display_brightness = brightness;
    return vb_runtime_display_apply_target();
}

static int vb_runtime_display_set_brightness(int brightness)
{
#if VB_RUNTIME_HAS_DISPLAY
    rt_device_t dev;
    rt_uint8_t level;
    int result;
    if (brightness < 0 || brightness > 100) return -RT_EINVAL;
    dev = rt_device_find(VB_DISPLAY_DEVICE_NAME);
    if (!dev) return -RT_ERROR;
    level = (rt_uint8_t)brightness;
    result = rt_device_control(dev, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &level);
    if (result == RT_EOK)
    {
        g_vb_runtime.display_brightness = brightness;
        g_vb_runtime.display_target_brightness = brightness;
        g_vb_runtime.display_last_ready = 1;
        (void)vb_runtime_display_save_state();
    }
    return result;
#else
    (void)brightness;
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_display_set_brightness_text(const char *brightness_text, char *dst, rt_size_t cap)
{
    char *end = RT_NULL;
    unsigned long value;
    int rc;
    if (!brightness_text || !brightness_text[0]) return vb_runtime_display_write_json(dst, cap, -RT_EINVAL, RT_NULL);
    value = strtoul(brightness_text, &end, 10);
    if (!end || *end || value > 100) return vb_runtime_display_write_json(dst, cap, -RT_EINVAL, RT_NULL);
    rc = vb_runtime_display_set_brightness((int)value);
    if (rc != RT_EOK) return vb_runtime_display_write_json(dst, cap, rc, RT_NULL);
    return vb_runtime_display_read_json(dst, cap);
}

static const char *vb_runtime_display_short_selector(const char *selector)
{
    if (!selector) return "";
    if (rt_strncmp(selector, "display.", 8) == 0) return selector + 8;
    if (rt_strncmp(selector, "screen.", 7) == 0) return selector + 7;
    if (rt_strncmp(selector, "vibeboard.display.", 18) == 0) return selector + 18;
    return selector;
}

static int vb_runtime_display_format_text(const char *selector, char *dst, rt_size_t cap)
{
    vb_display_snapshot_t snapshot;
    const char *short_selector = vb_runtime_display_short_selector(selector);
    int rc;
    if (!short_selector || !dst || cap == 0) return 0;
    rc = vb_runtime_display_read(&snapshot);
    if (rt_strcmp(short_selector, "brightness") == 0)
    {
        if (rc == RT_EOK && snapshot.brightness >= 0) rt_snprintf(dst, cap, "%d%%", snapshot.brightness);
        else rt_snprintf(dst, cap, "Brightness --");
        return 1;
    }
    if (rt_strcmp(short_selector, "size") == 0 || rt_strcmp(short_selector, "resolution") == 0)
    {
        if (rc == RT_EOK) rt_snprintf(dst, cap, "%dx%d", snapshot.width, snapshot.height);
        else rt_snprintf(dst, cap, "Display --");
        return 1;
    }
    if (rt_strcmp(short_selector, "state") == 0)
    {
        rt_snprintf(dst, cap, "%s", snapshot.state_name ? snapshot.state_name : "unknown");
        return 1;
    }
    if (rt_strcmp(short_selector, "bpp") == 0)
    {
        if (rc == RT_EOK) rt_snprintf(dst, cap, "%d bpp", snapshot.bpp);
        else rt_snprintf(dst, cap, "BPP --");
        return 1;
    }
    return 0;
}

static const char *vb_runtime_rgb_color_name(rt_uint32_t color)
{
    switch (color & 0xffffffu)
    {
    case 0x000000u: return "off";
    case 0xff0000u: return "red";
    case 0x00ff00u: return "green";
    case 0x0000ffu: return "blue";
    case 0xffff00u: return "yellow";
    case 0x00ffffu: return "cyan";
    case 0xff00ffu: return "magenta";
    case 0xffffffu: return "white";
    default: return "custom";
    }
}

static int vb_runtime_rgb_parse_color(const char *text, rt_uint32_t *color)
{
    char buf[24];
    const char *p;
    char *end;
    unsigned long value;
    rt_size_t i;

    if (!text || !color) return -RT_EINVAL;
    while (*text == ' ' || *text == '\t') text++;
    if (!*text) return -RT_EINVAL;
    vb_safe_copy(buf, sizeof(buf), text);
    for (i = 0; buf[i]; i++)
    {
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
    }
    if (rt_strcmp(buf, "off") == 0 || rt_strcmp(buf, "black") == 0)
    {
        *color = 0x000000u;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "red") == 0)
    {
        *color = 0xff0000u;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "green") == 0)
    {
        *color = 0x00ff00u;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "blue") == 0)
    {
        *color = 0x0000ffu;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "yellow") == 0)
    {
        *color = 0xffff00u;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "cyan") == 0)
    {
        *color = 0x00ffffu;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "magenta") == 0 || rt_strcmp(buf, "purple") == 0)
    {
        *color = 0xff00ffu;
        return RT_EOK;
    }
    if (rt_strcmp(buf, "white") == 0)
    {
        *color = 0xffffffu;
        return RT_EOK;
    }

    p = buf;
    if (*p == '#') p++;
    else if (p[0] == '0' && p[1] == 'x') p += 2;
    if (rt_strlen(p) != 6) return -RT_EINVAL;
    value = strtoul(p, &end, 16);
    if (!end || *end) return -RT_EINVAL;
    *color = (rt_uint32_t)(value & 0xffffffu);
    return RT_EOK;
}

static int vb_runtime_rgb_apply(rt_uint32_t color)
{
#if VB_RUNTIME_HAS_RGB
    rt_device_t dev;
    struct rt_rgbled_configuration cfg;

    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO3_3V3, true, true);
    HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, PIN_NOPULL, 1);

    dev = rt_device_find(RGB_USING_SK6812MINI_HS_DEV_NAME);
    if (!dev)
    {
        g_vb_runtime.rgb_last_ready = 0;
        return -RT_ERROR;
    }
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.color_rgb = color & 0xffffffu;
    if (rt_device_control(dev, PWM_CMD_SET_COLOR, &cfg) != RT_EOK)
    {
        g_vb_runtime.rgb_last_ready = 0;
        return -RT_ERROR;
    }
    g_vb_runtime.rgb_color = color & 0xffffffu;
    g_vb_runtime.rgb_last_ready = 1;
    return RT_EOK;
#else
    (void)color;
    g_vb_runtime.rgb_last_ready = 0;
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_rgb_save_state(void)
{
    char json[96];
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    rt_snprintf(json, sizeof(json),
                "{\"api\":\"%s\",\"color\":\"%06lx\"}\n",
                VIBEBOARD_RUNTIME_RGB_API_VERSION,
                (unsigned long)(g_vb_runtime.rgb_color & 0xffffffu));
    json[sizeof(json) - 1] = '\0';
    return vb_write_text_file(VIBEBOARD_RGB_STATE_FILE, json);
}

static int vb_runtime_rgb_load_state(void)
{
    char json[128];
    char api[64];
    char color_text[16];
    rt_uint32_t color = 0;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    if (vb_read_text_file(VIBEBOARD_RGB_STATE_FILE, json, sizeof(json)) <= 0) return -RT_ERROR;
    vb_json_copy_string(json, RT_NULL, "api", api, sizeof(api), "");
    if (rt_strcmp(api, VIBEBOARD_RUNTIME_RGB_API_VERSION) != 0) return -RT_EINVAL;
    vb_json_copy_string(json, RT_NULL, "color", color_text, sizeof(color_text), "");
    if (vb_runtime_rgb_parse_color(color_text, &color) != RT_EOK) return -RT_EINVAL;
    return vb_runtime_rgb_apply(color);
}

static int vb_runtime_rgb_write_json(char *dst, rt_size_t cap, int rc)
{
    int used;
    int available = VB_RUNTIME_HAS_RGB ? 1 : 0;
    int ready = (rc == RT_EOK || g_vb_runtime.rgb_last_ready) ? 1 : 0;
    const char *dev_name = "";
    int led_count = 0;
    if (!dst || cap == 0) return -RT_EINVAL;
#if VB_RUNTIME_HAS_RGB
    dev_name = RGB_USING_SK6812MINI_HS_DEV_NAME;
#endif
#if defined(BSP_RGB_LED_COUNT)
    led_count = BSP_RGB_LED_COUNT;
#endif
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"ok\":%d,\"dev\":\"%s\",\"count\":%d,\"color\":\"%06lx\",\"name\":\"%s\"}",
                       VIBEBOARD_RUNTIME_RGB_API_VERSION,
                       available,
                       ready,
                       rc == RT_EOK ? 1 : 0,
                       dev_name,
                       led_count,
                       (unsigned long)(g_vb_runtime.rgb_color & 0xffffffu),
                       vb_runtime_rgb_color_name(g_vb_runtime.rgb_color));
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_RGB_API_VERSION,
                    available,
                    ready);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return rc;
}

static int vb_runtime_rgb_read_json(char *dst, rt_size_t cap)
{
#if VB_RUNTIME_HAS_RGB
    rt_device_t dev = rt_device_find(RGB_USING_SK6812MINI_HS_DEV_NAME);
    int rc = dev ? RT_EOK : -RT_ERROR;
    if (dev)
    {
        if (!g_vb_runtime.rgb_last_ready)
        {
            (void)vb_runtime_rgb_load_state();
        }
        else
        {
            (void)vb_runtime_rgb_apply(g_vb_runtime.rgb_color);
        }
        g_vb_runtime.rgb_last_ready = 1;
        rc = RT_EOK;
    }
    return vb_runtime_rgb_write_json(dst, cap, rc);
#else
    return vb_runtime_rgb_write_json(dst, cap, -RT_ENOSYS);
#endif
}

static int vb_runtime_rgb_set_text(const char *color_text, char *dst, rt_size_t cap)
{
    rt_uint32_t color = 0;
    int rc = vb_runtime_rgb_parse_color(color_text, &color);
    if (rc == RT_EOK) rc = vb_runtime_rgb_apply(color);
    if (rc == RT_EOK) (void)vb_runtime_rgb_save_state();
    return vb_runtime_rgb_write_json(dst, cap, rc);
}

static const char *vb_runtime_touch_event_name(int event)
{
    switch (event)
    {
    case LV_EVENT_PRESSED:
        return "pressed";
    case LV_EVENT_PRESSING:
        return "pressing";
    case LV_EVENT_RELEASED:
        return "released";
    case LV_EVENT_CLICKED:
        return "clicked";
    case LV_EVENT_GESTURE:
        return "gesture";
    default:
        return "idle";
    }
}

static const char *vb_runtime_touch_gesture_name(int dir)
{
    switch (dir)
    {
    case LV_DIR_LEFT:
        return "left";
    case LV_DIR_RIGHT:
        return "right";
    case LV_DIR_TOP:
        return "up";
    case LV_DIR_BOTTOM:
        return "down";
    default:
        return "none";
    }
}

static uint32_t vb_runtime_touch_tick_elapsed_ms(uint32_t start_tick, uint32_t end_tick)
{
    uint32_t delta = end_tick >= start_tick ? (end_tick - start_tick) : 0;
    return (uint32_t)((delta * 1000u) / RT_TICK_PER_SECOND);
}

static int vb_runtime_touch_read_json(char *dst, rt_size_t cap)
{
    int used;
    if (!dst || cap == 0) return -RT_EINVAL;
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"active\":%d,\"count\":%lu,"
                       "\"x\":%d,\"y\":%d,\"event\":\"%s\",\"gesture\":\"%s\","
                       "\"dx\":%d,\"dy\":%d,\"duration_ms\":%lu,\"tick\":%lu}",
                       VIBEBOARD_RUNTIME_TOUCH_API_VERSION,
                       g_vb_runtime.touch.available,
                       g_vb_runtime.touch.ready,
                       g_vb_runtime.touch.active,
                       (unsigned long)g_vb_runtime.touch.count,
                       g_vb_runtime.touch.point.x,
                       g_vb_runtime.touch.point.y,
                       vb_runtime_touch_event_name(g_vb_runtime.touch.last_event),
                       vb_runtime_touch_gesture_name(g_vb_runtime.touch.gesture_dir),
                       g_vb_runtime.touch.delta.x,
                       g_vb_runtime.touch.delta.y,
                       (unsigned long)g_vb_runtime.touch.last_duration_ms,
                       (unsigned long)g_vb_runtime.touch.last_tick);
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_TOUCH_API_VERSION);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int vb_runtime_gpio_is_pressed(int level, int active_high)
{
    return active_high ? (level != 0) : (level == 0);
}

static int vb_runtime_gpio_refresh(void)
{
    int ready = 0;

    g_vb_runtime.gpio.available = VB_RUNTIME_HAS_GPIO ? 1 : 0;
    g_vb_runtime.gpio.ready = 0;
    g_vb_runtime.gpio.key1_ok = 0;
    g_vb_runtime.gpio.key2_ok = 0;
    g_vb_runtime.gpio.key1_level = 0;
    g_vb_runtime.gpio.key2_level = 0;
    g_vb_runtime.gpio.key1_pressed = 0;
    g_vb_runtime.gpio.key2_pressed = 0;

#if VB_RUNTIME_HAS_GPIO
    if (!g_vb_runtime.gpio.configured)
    {
#if defined(BSP_USING_KEY1) && defined(BSP_KEY1_PIN)
        rt_pin_mode(BSP_KEY1_PIN, PIN_MODE_INPUT);
#endif
#if defined(BSP_USING_KEY2) && defined(BSP_KEY2_PIN)
        rt_pin_mode(BSP_KEY2_PIN, PIN_MODE_INPUT);
#endif
        g_vb_runtime.gpio.configured = 1;
    }
#if defined(BSP_USING_KEY1) && defined(BSP_KEY1_PIN)
    {
        int level = rt_pin_read(BSP_KEY1_PIN);
        g_vb_runtime.gpio.key1_ok = 1;
        g_vb_runtime.gpio.key1_level = level ? 1 : 0;
        g_vb_runtime.gpio.key1_pressed = vb_runtime_gpio_is_pressed(g_vb_runtime.gpio.key1_level,
                                                                    VB_RUNTIME_GPIO_KEY1_ACTIVE_HIGH);
        ready = 1;
    }
#endif
#if defined(BSP_USING_KEY2) && defined(BSP_KEY2_PIN)
    {
        int level = rt_pin_read(BSP_KEY2_PIN);
        g_vb_runtime.gpio.key2_ok = 1;
        g_vb_runtime.gpio.key2_level = level ? 1 : 0;
        g_vb_runtime.gpio.key2_pressed = vb_runtime_gpio_is_pressed(g_vb_runtime.gpio.key2_level,
                                                                    VB_RUNTIME_GPIO_KEY2_ACTIVE_HIGH);
        ready = 1;
    }
#endif
    g_vb_runtime.gpio.ready = ready;
    g_vb_runtime.gpio.read_count++;
    return ready ? RT_EOK : -RT_ERROR;
#else
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_gpio_read_json(char *dst, rt_size_t cap)
{
    int used;
    int rc;
    int count = 0;
    int key1_pin = -1;
    int key1_active_high = 0;
    int key2_pin = -1;
    int key2_active_high = 0;

    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';

#if defined(BSP_USING_KEY1) && defined(BSP_KEY1_PIN)
    count++;
    key1_pin = BSP_KEY1_PIN;
    key1_active_high = VB_RUNTIME_GPIO_KEY1_ACTIVE_HIGH;
#endif
#if defined(BSP_USING_KEY2) && defined(BSP_KEY2_PIN)
    count++;
    key2_pin = BSP_KEY2_PIN;
    key2_active_high = VB_RUNTIME_GPIO_KEY2_ACTIVE_HIGH;
#endif

    rc = vb_runtime_gpio_refresh();
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"count\":%d,\"inputs_only\":1,"
                       "\"key1\":{\"ok\":%d,\"pin\":%d,\"active_high\":%d,\"level\":%d,\"pressed\":%d},"
                       "\"key2\":{\"ok\":%d,\"pin\":%d,\"active_high\":%d,\"level\":%d,\"pressed\":%d}}",
                       VIBEBOARD_RUNTIME_GPIO_API_VERSION,
                       g_vb_runtime.gpio.available,
                       g_vb_runtime.gpio.ready,
                       count,
                       g_vb_runtime.gpio.key1_ok,
                       key1_pin,
                       key1_active_high,
                       g_vb_runtime.gpio.key1_level,
                       g_vb_runtime.gpio.key1_pressed,
                       g_vb_runtime.gpio.key2_ok,
                       key2_pin,
                       key2_active_high,
                       g_vb_runtime.gpio.key2_level,
                       g_vb_runtime.gpio.key2_pressed);
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"available\":%d,\"ready\":%d,\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_GPIO_API_VERSION,
                    g_vb_runtime.gpio.available,
                    g_vb_runtime.gpio.ready);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return g_vb_runtime.gpio.ready ? RT_EOK : rc;
}

static int vb_runtime_gpio_format_text(const char *selector, char *dst, rt_size_t cap)
{
    if (!selector || !dst || cap == 0) return 0;
    vb_runtime_gpio_refresh();
    if (rt_strcmp(selector, "key1") == 0)
    {
        rt_snprintf(dst, cap, g_vb_runtime.gpio.key1_ok ?
                    (g_vb_runtime.gpio.key1_pressed ? "pressed" : "released") : "--");
        return 1;
    }
    if (rt_strcmp(selector, "key1.level") == 0)
    {
        rt_snprintf(dst, cap, g_vb_runtime.gpio.key1_ok ? "%d" : "--",
                    g_vb_runtime.gpio.key1_level);
        return 1;
    }
    if (rt_strcmp(selector, "key2") == 0)
    {
        rt_snprintf(dst, cap, g_vb_runtime.gpio.key2_ok ?
                    (g_vb_runtime.gpio.key2_pressed ? "pressed" : "released") : "--");
        return 1;
    }
    if (rt_strcmp(selector, "key2.level") == 0)
    {
        rt_snprintf(dst, cap, g_vb_runtime.gpio.key2_ok ? "%d" : "--",
                    g_vb_runtime.gpio.key2_level);
        return 1;
    }
    return 0;
}

static int vb_runtime_capabilities_json(char *dst, rt_size_t cap)
{
    int fs;
    int sensor_api;
    int used;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    fs = vb_prepare_filesystem() == RT_EOK ? 1 : 0;
#if defined(RT_USING_SENSOR)
    sensor_api = 1;
#else
    sensor_api = 0;
#endif
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"rt\":\"%s\",\"ble\":\"%s\",\"sens\":\"%s\",\"touch\":\"%s\","
                       "\"flow\":\"%s\",\"voice\":\"%s\",\"pwr\":\"%s\",\"disp\":\"%s\",\"gpio\":\"%s\",\"rgb\":\"%s\",\"fs\":%d,"
                       "\"ins\":{\"ser\":1,\"ble\":%d,\"max\":%d},"
                       "\"app\":{\"lua\":\"%s\",\"comp\":%d},"
                       "\"hw\":{\"disp\":%d,\"touch\":1,\"sens\":%d,\"voice\":%d,\"flow\":1,"
                       "\"batt\":%d,\"chg\":%d,\"gpio\":%d,\"rgb\":%d}}",
                       VIBEBOARD_RUNTIME_CAPABILITY_API_VERSION,
                       VIBEBOARD_RUNTIME_API_VERSION,
                       VIBEBOARD_RUNTIME_BLE_API_VERSION,
                       VIBEBOARD_RUNTIME_SENSOR_API_VERSION,
                       VIBEBOARD_RUNTIME_TOUCH_API_VERSION,
                       VIBEBOARD_RUNTIME_FLOW_API_VERSION,
                       VIBEBOARD_RUNTIME_VOICE_API_VERSION,
                       VIBEBOARD_RUNTIME_POWER_API_VERSION,
                       VIBEBOARD_RUNTIME_DISPLAY_API_VERSION,
                       VIBEBOARD_RUNTIME_GPIO_API_VERSION,
                       VIBEBOARD_RUNTIME_RGB_API_VERSION,
                       fs,
                       VB_RUNTIME_HAS_BLE_INSTALL,
                       VB_RUNTIME_INSTALL_MAX_CHUNK_BYTES,
                       vibeboard_lua_runtime_available() ? vibeboard_lua_runtime_name() : "manifest-fallback",
                       VB_MAX_COMPONENTS,
                       VB_RUNTIME_HAS_DISPLAY,
                       sensor_api,
                       VB_RUNTIME_HAS_VOICE_CAPTURE,
                       VB_RUNTIME_HAS_POWER_BATTERY,
                       VB_RUNTIME_HAS_POWER_CHARGER,
                       VB_RUNTIME_HAS_GPIO,
                       VB_RUNTIME_HAS_RGB);
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_CAPABILITY_API_VERSION);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int vb_runtime_capabilities_status_command(void)
{
    char json[VB_BLE_STATUS_MAX];
    int result = vb_runtime_capabilities_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
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

static uint8_t vb_ble_advertising_force_restart(void);

static uint8_t vb_ble_adv_context_state(void)
{
    return g_vb_ble.adv_configured ? g_vb_ble_install_adv_context->state : 0xff;
}

static uint8_t vb_ble_adv_context_index(void)
{
    return g_vb_ble.adv_configured ? g_vb_ble_install_adv_context->adv_idx : 0xff;
}

static uint8_t vb_ble_adv_context_transist(void)
{
    return g_vb_ble.adv_configured ? g_vb_ble_install_adv_context->adv_transist : 0xff;
}

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
    if (rt_strcmp(argv[0], "capabilities") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_capabilities") == 0)
    {
        int result = vb_runtime_capabilities_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] capabilities rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if ((rt_strcmp(argv[0], "json_read") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_json_read") == 0) && argc >= 4)
    {
        uint32_t offset = (uint32_t)strtoul(argv[2], RT_NULL, 10);
        uint32_t max_bytes = (uint32_t)strtoul(argv[3], RT_NULL, 10);
        int result = vb_runtime_ble_json_read(argv[1], offset, max_bytes, g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] json_read rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "status") == 0 || rt_strcmp(argv[0], "vb_runtime_status") == 0)
    {
        char active[VB_MAX_APP_ID];
        vb_read_active_app(active, sizeof(active));
        vb_ble_set_status("ok status api=%s active=%s flow=%lu", VIBEBOARD_RUNTIME_BLE_API_VERSION,
                          active[0] ? active : "(unknown)",
                          (unsigned long)g_vb_flow.total_count);
        return RT_EOK;
    }
    if (rt_strcmp(argv[0], "ble_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_ble_status") == 0)
    {
        vb_ble_set_status("ok ble api=%s init=%d power=%d service=%d adv=%d conn=%d mtu=%d state=%d idx=%d trans=%d start_rc=%d stop_rc=%d reason=%d starts=%lu stops=%lu restarts=%lu",
                          VIBEBOARD_RUNTIME_BLE_API_VERSION,
                          g_vb_ble.initialized, g_vb_ble.power_on, g_vb_ble.service_ready,
                          g_vb_ble.advertising, g_vb_ble.connected, g_vb_ble.mtu,
                          (int)vb_ble_adv_context_state(),
                          (int)vb_ble_adv_context_index(),
                          (int)vb_ble_adv_context_transist(),
                          (int)g_vb_ble.last_adv_start_rc,
                          (int)g_vb_ble.last_adv_stop_rc,
                          (int)g_vb_ble.last_adv_stop_reason,
                          (unsigned long)g_vb_ble.adv_start_events,
                          (unsigned long)g_vb_ble.adv_stop_events,
                          (unsigned long)g_vb_ble.adv_restart_requests);
        return RT_EOK;
    }
    if (rt_strcmp(argv[0], "ble_restart") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_ble_restart") == 0)
    {
        uint8_t result = vb_ble_advertising_force_restart();
        return result == SIBLES_ADV_NO_ERR ? RT_EOK : -RT_ERROR;
    }
    if (rt_strcmp(argv[0], "app") == 0 ||
        rt_strcmp(argv[0], "app_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_app") == 0)
    {
        int result = vb_runtime_app_status_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] app rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "apps") == 0 ||
        rt_strcmp(argv[0], "app_list") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_apps") == 0)
    {
        int result = vb_runtime_app_list_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] apps rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if ((rt_strcmp(argv[0], "apps_page") == 0 ||
         rt_strcmp(argv[0], "app_page") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_apps_page") == 0))
    {
        int offset = argc >= 2 ? (int)strtol(argv[1], RT_NULL, 10) : 0;
        int limit = argc >= 3 ? (int)strtol(argv[2], RT_NULL, 10) : VB_LAUNCHER_MAX_ITEMS;
        int result = vb_runtime_app_list_page_json(g_vb_ble.status, sizeof(g_vb_ble.status), offset, limit);
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] apps_page rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if ((rt_strcmp(argv[0], "launch") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_launch") == 0) && argc >= 2)
    {
        int result = vb_runtime_app_launch(argv[1]);
        vb_ble_set_status("%s launch app=%s rc=%d", result == RT_EOK ? "ok" : "err", argv[1], result);
        return result;
    }
    if (rt_strcmp(argv[0], "stop") == 0 || rt_strcmp(argv[0], "vb_runtime_stop") == 0)
    {
        int result = vb_runtime_app_stop();
        vb_ble_set_status("%s stop rc=%d", result == RT_EOK ? "ok" : "err", result);
        return result;
    }
    if ((rt_strcmp(argv[0], "delete") == 0 ||
         rt_strcmp(argv[0], "app_delete") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_delete") == 0) && argc >= 2)
    {
        int result = vb_runtime_app_delete(argv[1]);
        vb_ble_set_status("%s delete app=%s rc=%d", result == RT_EOK ? "ok" : "err", argv[1], result);
        return result;
    }
    if (rt_strcmp(argv[0], "staging_clear") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_staging_clear") == 0)
    {
        int removed = 0;
        int result = vb_runtime_staging_clear_all(&removed);
        vb_ble_set_status("%s staging_clear removed=%d rc=%d", result == RT_EOK ? "ok" : "err", removed, result);
        return result;
    }
    if (rt_strcmp(argv[0], "power") == 0 ||
        rt_strcmp(argv[0], "battery") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_power") == 0)
    {
        int result = vb_runtime_power_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] power rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "display") == 0 || rt_strcmp(argv[0], "display_status") == 0 ||
        rt_strcmp(argv[0], "screen") == 0 || rt_strcmp(argv[0], "vb_runtime_display") == 0)
    {
        int result;
        if (argc >= 2) result = vb_runtime_display_set_brightness_text(argv[1], g_vb_ble.status, sizeof(g_vb_ble.status));
        else result = vb_runtime_display_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] display rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if ((rt_strcmp(argv[0], "display_brightness") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_display_brightness") == 0) && argc >= 2)
    {
        int result = vb_runtime_display_set_brightness_text(argv[1], g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] display_brightness rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "gpio") == 0 ||
        rt_strcmp(argv[0], "gpio_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_gpio") == 0)
    {
        int result = vb_runtime_gpio_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] gpio rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "touch") == 0 ||
        rt_strcmp(argv[0], "touch_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_touch") == 0)
    {
        int result = vb_runtime_touch_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] touch rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "rgb") == 0 || rt_strcmp(argv[0], "rgb_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_rgb") == 0)
    {
        int result;
        if (argc >= 2) result = vb_runtime_rgb_set_text(argv[1], g_vb_ble.status, sizeof(g_vb_ble.status));
        else result = vb_runtime_rgb_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] rgb rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if ((rt_strcmp(argv[0], "rgb_set") == 0 || rt_strcmp(argv[0], "vb_runtime_rgb_set") == 0) && argc >= 2)
    {
        int result = vb_runtime_rgb_set_text(argv[1], g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] rgb_set rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if ((rt_strcmp(argv[0], "flow_send") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_flow_send") == 0) && argc >= 4)
    {
        uint32_t sequence = (uint32_t)strtoul(argv[2], RT_NULL, 10);
        int result = vb_runtime_flow_send_hex(argv[1], sequence, argv[3]);
        vb_ble_set_status("%s flow_send channel=%s seq=%lu bytes=%d total=%lu",
                          result >= 0 ? "ok" : "err",
                          argv[1],
                          (unsigned long)sequence,
                          result >= 0 ? result : 0,
                          (unsigned long)g_vb_flow.total_count);
        return result >= 0 ? RT_EOK : result;
    }
    if (rt_strcmp(argv[0], "flow_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_flow_status") == 0)
    {
        int index = vb_runtime_flow_latest_index();
        if (index >= 0)
        {
            vb_info_flow_item_t *item = &g_vb_flow.items[index];
            vb_ble_set_status("ok flow api=%s total=%lu retained=%d seq=%lu channel=%s bytes=%lu",
                              VIBEBOARD_RUNTIME_FLOW_API_VERSION,
                              (unsigned long)g_vb_flow.total_count,
                              g_vb_flow.count,
                              (unsigned long)item->sequence,
                              item->channel,
                              (unsigned long)item->bytes);
        }
        else
        {
            vb_ble_set_status("ok flow api=%s total=0 retained=0",
                              VIBEBOARD_RUNTIME_FLOW_API_VERSION);
        }
        return RT_EOK;
    }
    if (rt_strcmp(argv[0], "flow_clear") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_flow_clear") == 0)
    {
        vb_runtime_flow_clear_state();
        vb_ble_set_status("ok flow_clear total=0");
        return RT_EOK;
    }
    if (rt_strcmp(argv[0], "voice") == 0 ||
        rt_strcmp(argv[0], "audio") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_voice") == 0)
    {
        int result = vb_runtime_voice_read_json(g_vb_ble.status, sizeof(g_vb_ble.status));
        g_vb_ble.status[sizeof(g_vb_ble.status) - 1] = '\0';
        rt_kprintf("[vb_runtime][ble] voice rc=%d %s\n", result, g_vb_ble.status);
        return result;
    }
    if (rt_strcmp(argv[0], "voice_status") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_voice_status") == 0)
    {
        return vb_runtime_voice_status(g_vb_ble.status, sizeof(g_vb_ble.status));
    }
    if ((rt_strcmp(argv[0], "voice_start") == 0 ||
         rt_strcmp(argv[0], "vb_runtime_voice_start") == 0))
    {
        uint32_t duration_ms = argc >= 2 ? (uint32_t)strtoul(argv[1], RT_NULL, 10) : VB_VOICE_DEFAULT_MS;
        int result = vb_runtime_voice_start(duration_ms);
        vb_ble_set_status("%s voice_start seq=%lu bytes=%lu ms=%lu rc=%d built=%d",
                          result == RT_EOK ? "ok" : "err",
                          (unsigned long)g_vb_voice.sequence,
                          (unsigned long)g_vb_voice.recorded_bytes,
                          (unsigned long)g_vb_voice.requested_ms,
                          result,
                          VB_RUNTIME_HAS_VOICE_CAPTURE);
        return result;
    }
    if (rt_strcmp(argv[0], "voice_read") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_voice_read") == 0)
    {
        uint32_t offset = argc >= 2 ? (uint32_t)strtoul(argv[1], RT_NULL, 10) : 0;
        uint32_t max_bytes = argc >= 3 ? (uint32_t)strtoul(argv[2], RT_NULL, 10) : VB_VOICE_CHUNK_BYTES;
        int result = vb_runtime_voice_read_hex(offset, max_bytes, g_vb_ble.status, sizeof(g_vb_ble.status));
        if (result != RT_EOK)
        {
            vb_ble_set_status("err voice_read offset=%lu rc=%d ready=%d bytes=%lu",
                              (unsigned long)offset,
                              result,
                              g_vb_voice.ready,
                              (unsigned long)g_vb_voice.recorded_bytes);
        }
        return result;
    }
    if (rt_strcmp(argv[0], "voice_clear") == 0 ||
        rt_strcmp(argv[0], "vb_runtime_voice_clear") == 0)
    {
        vb_runtime_voice_clear();
        vb_ble_set_status("ok voice_clear");
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
        vb_ble_set_status("%s install_begin app=%s rc=%d",
                          result == RT_EOK ? "ok" : "err",
                          argv[1],
                          result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_install_file") == 0 && argc >= 5)
    {
        int result = vb_runtime_install_file_chunk(argv[1], argv[2], argv[3], argv[4]);
        vb_ble_set_status("%s install_file app=%s path=%s offset=%s rc=%d",
                          result == RT_EOK ? "ok" : "err",
                          argv[1],
                          argv[2],
                          argv[3],
                          result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_install_abort") == 0 && argc >= 2)
    {
        int result = vb_runtime_install_abort_app(argv[1]);
        vb_ble_set_status("%s install_abort app=%s rc=%d",
                          result == RT_EOK ? "ok" : "err",
                          argv[1],
                          result);
        return result;
    }
    if (rt_strcmp(argv[0], "vb_runtime_install_end") == 0 && argc >= 2)
    {
        int result = vb_runtime_install_end_app(argv[1]);
        if (result == RT_EOK)
        {
            vb_ble_set_status("ok install_end app=%s active=%s rc=%d",
                              argv[1],
                              argv[1],
                              result);
        }
        else
        {
            vb_ble_set_status("err install_end app=%s rc=%d", argv[1], result);
        }
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
        uint8_t status = evt ? evt->status : 0xff;
        g_vb_ble.last_adv_start_rc = status;
        g_vb_ble.adv_start_events++;
        g_vb_ble.advertising = status == 0;
        vb_ble_set_status("adv started name=%s status=%u mode=%d state=%u events=%lu",
                          VIBEBOARD_BLE_NAME, (unsigned)status, evt ? evt->adv_mode : -1,
                          (unsigned)vb_ble_adv_context_state(),
                          (unsigned long)g_vb_ble.adv_start_events);
        break;
    }
    case SIBLES_ADV_EVT_ADV_STOPPED:
    {
        sibles_adv_evt_stopped_t *evt = (sibles_adv_evt_stopped_t *)data;
        uint8_t reason = evt ? evt->reason : 0xff;
        g_vb_ble.last_adv_stop_reason = reason;
        g_vb_ble.adv_stop_events++;
        g_vb_ble.advertising = 0;
        vb_ble_set_status("adv stopped reason=%u mode=%d state=%u events=%lu",
                          (unsigned)reason, evt ? evt->adv_mode : -1,
                          (unsigned)vb_ble_adv_context_state(),
                          (unsigned long)g_vb_ble.adv_stop_events);
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

static uint8_t vb_ble_advertising_start(void)
{
    sibles_advertising_para_t para = {0};
    uint8_t ret = 1;
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

    if (g_vb_ble.adv_configured)
    {
        if (g_vb_ble.connected)
        {
            ret = SIBLES_ADV_NOT_ALLOWED;
            g_vb_ble.last_adv_start_rc = ret;
            vb_ble_set_status("adv start blocked connected=1 state=%u",
                              (unsigned)vb_ble_adv_context_state());
            return ret;
        }
        if (g_vb_ble.advertising)
        {
            ret = SIBLES_ADV_NO_ERR;
            g_vb_ble.last_adv_start_rc = ret;
            vb_ble_set_status("adv already started name=%s state=%u idx=%u trans=%u",
                              VIBEBOARD_BLE_NAME,
                              (unsigned)vb_ble_adv_context_state(),
                              (unsigned)vb_ble_adv_context_index(),
                              (unsigned)vb_ble_adv_context_transist());
            return ret;
        }
        ret = sibles_advertising_start(g_vb_ble_install_adv_context);
        g_vb_ble.last_adv_start_rc = ret;
        vb_ble_set_status("adv restart requested name=%s rc=%u state=%u idx=%u trans=%u",
                          VIBEBOARD_BLE_NAME, (unsigned)ret,
                          (unsigned)vb_ble_adv_context_state(),
                          (unsigned)vb_ble_adv_context_index(),
                          (unsigned)vb_ble_adv_context_transist());
        return ret;
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
        g_vb_ble.adv_configured = 1;
        ret = sibles_advertising_start(g_vb_ble_install_adv_context);
        g_vb_ble.last_adv_start_rc = ret;
        vb_ble_set_status("adv start requested name=%s rc=%u state=%u idx=%u trans=%u",
                          VIBEBOARD_BLE_NAME, (unsigned)ret,
                          (unsigned)vb_ble_adv_context_state(),
                          (unsigned)vb_ble_adv_context_index(),
                          (unsigned)vb_ble_adv_context_transist());
    }
    else
    {
        vb_ble_set_status("adv init failed rc=%d", ret);
    }

cleanup:
    if (para.adv_data.completed_uuid) rt_free(para.adv_data.completed_uuid);
    if (para.rsp_data.completed_name) rt_free(para.rsp_data.completed_name);
    if (para.adv_data.manufacturer_data) rt_free(para.adv_data.manufacturer_data);
    g_vb_ble.last_adv_start_rc = ret;
    return ret;
}

static uint8_t vb_ble_advertising_force_restart(void)
{
    uint8_t stop_rc = SIBLES_ADV_NOT_ALLOWED;
    uint8_t start_rc;
    uint32_t stop_events;
    int attempt;

    g_vb_ble.adv_restart_requests++;
    if (!g_vb_ble.power_on || !g_vb_ble.service_ready)
    {
        start_rc = SIBLES_ADV_NOT_ALLOWED;
        g_vb_ble.last_adv_start_rc = start_rc;
        vb_ble_set_status("ble_restart blocked power=%d service=%d rc=%u req=%lu",
                          g_vb_ble.power_on, g_vb_ble.service_ready,
                          (unsigned)start_rc,
                          (unsigned long)g_vb_ble.adv_restart_requests);
        return start_rc;
    }
    if (g_vb_ble.connected)
    {
        start_rc = SIBLES_ADV_NOT_ALLOWED;
        g_vb_ble.last_adv_start_rc = start_rc;
        vb_ble_set_status("ble_restart blocked connected=1 rc=%u req=%lu",
                          (unsigned)start_rc,
                          (unsigned long)g_vb_ble.adv_restart_requests);
        return start_rc;
    }

    stop_events = g_vb_ble.adv_stop_events;
    if (g_vb_ble.adv_configured)
    {
        stop_rc = sibles_advertising_stop(g_vb_ble_install_adv_context);
        g_vb_ble.last_adv_stop_rc = stop_rc;
        if (stop_rc == SIBLES_ADV_NO_ERR)
        {
            for (attempt = 0; attempt < VB_BLE_ADV_FORCE_RESTART_ATTEMPTS; attempt++)
            {
                if (g_vb_ble.adv_stop_events != stop_events)
                {
                    break;
                }
                rt_thread_mdelay(VB_BLE_ADV_FORCE_RESTART_DELAY_MS);
            }
        }
    }

    g_vb_ble.advertising = 0;
    start_rc = vb_ble_advertising_start();
    g_vb_ble.last_adv_start_rc = start_rc;
    vb_ble_set_status("ble_restart stop_rc=%u start_rc=%u adv=%d state=%u idx=%u trans=%u starts=%lu stops=%lu req=%lu",
                      (unsigned)stop_rc, (unsigned)start_rc, g_vb_ble.advertising,
                      (unsigned)vb_ble_adv_context_state(),
                      (unsigned)vb_ble_adv_context_index(),
                      (unsigned)vb_ble_adv_context_transist(),
                      (unsigned long)g_vb_ble.adv_start_events,
                      (unsigned long)g_vb_ble.adv_stop_events,
                      (unsigned long)g_vb_ble.adv_restart_requests);
    return start_rc;
}

static void vb_ble_worker_entry(void *parameter)
{
    uint32_t value;
    int attempt;
    uint8_t ret;
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
        else if (value == VB_BLE_EVT_RESTART_ADV)
        {
            g_vb_ble.adv_restart_requests++;
            rt_thread_mdelay(VB_BLE_ADV_RESTART_DELAY_MS);
            for (attempt = 0; attempt < VB_BLE_ADV_RESTART_ATTEMPTS; attempt++)
            {
                if (!g_vb_ble.power_on || !g_vb_ble.service_ready || g_vb_ble.connected)
                {
                    break;
                }
                rt_kprintf("[vb_runtime][ble] restart advertising after disconnect attempt=%d\n", attempt + 1);
                ret = vb_ble_advertising_start();
                if (ret == SIBLES_ADV_NO_ERR)
                {
                    break;
                }
                rt_thread_mdelay(VB_BLE_ADV_RESTART_INTERVAL_MS);
            }
        }
    }
}

static int vb_ble_install_init(void)
{
    rt_thread_t thread;
    vb_runtime_state_bootstrap();
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
        if (g_vb_ble.mailbox)
        {
            rt_mb_send(g_vb_ble.mailbox, VB_BLE_EVT_RESTART_ADV);
        }
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

static void vb_runtime_install_clear_session(void)
{
    g_vb_runtime.install_app[0] = '\0';
}

static int vb_runtime_recover_install_state(void)
{
    DIR *dir;
    struct dirent *entry;
    int result = RT_EOK;
    int staging_prefix_len = rt_strlen(VIBEBOARD_STAGING_PREFIX);
    int backup_prefix_len = rt_strlen(VIBEBOARD_BACKUP_PREFIX);

    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;

    dir = opendir(VIBEBOARD_APP_ROOT);
    if (!dir)
    {
        rt_kprintf("[vb_runtime] install recovery failed: open %s\n", VIBEBOARD_APP_ROOT);
        return -RT_ERROR;
    }

    while ((entry = readdir(dir)) != RT_NULL)
    {
        const char *name = entry->d_name;
        char path[VB_MAX_PATH];

        if (!name || rt_strcmp(name, ".") == 0 || rt_strcmp(name, "..") == 0) continue;
        rt_snprintf(path, sizeof(path), "%s/%s", VIBEBOARD_APP_ROOT, name);
        path[sizeof(path) - 1] = '\0';

        if (rt_strncmp(name, VIBEBOARD_STAGING_PREFIX, staging_prefix_len) == 0)
        {
            if (vb_remove_tree(path) != RT_EOK)
            {
                rt_kprintf("[vb_runtime] install recovery failed: remove staging %s\n", path);
                result = -RT_ERROR;
            }
            else
            {
                rt_kprintf("[vb_runtime] removed stale staging: %s\n", name);
            }
            continue;
        }

        if (rt_strncmp(name, VIBEBOARD_BACKUP_PREFIX, backup_prefix_len) == 0)
        {
            const char *app_id = name + backup_prefix_len;
            char app_path[VB_MAX_PATH];

            if (!vb_is_safe_app_id(app_id))
            {
                if (vb_remove_tree(path) != RT_EOK)
                {
                    rt_kprintf("[vb_runtime] install recovery failed: remove backup %s\n", path);
                    result = -RT_ERROR;
                }
                continue;
            }

            rt_snprintf(app_path, sizeof(app_path), "%s/%s", VIBEBOARD_APP_ROOT, app_id);
            app_path[sizeof(app_path) - 1] = '\0';
            if (access(app_path, 0) == 0)
            {
                if (vb_remove_tree(path) != RT_EOK)
                {
                    rt_kprintf("[vb_runtime] install recovery failed: clean backup %s\n", path);
                    result = -RT_ERROR;
                }
                else
                {
                    rt_kprintf("[vb_runtime] removed completed backup: %s\n", name);
                }
            }
            else if (rename(path, app_path) != 0)
            {
                rt_kprintf("[vb_runtime] install recovery failed: restore %s -> %s\n", path, app_path);
                result = -RT_ERROR;
            }
            else
            {
                rt_kprintf("[vb_runtime] restored backup app: %s\n", app_id);
            }
        }
    }

    closedir(dir);
    return result;
}

static int vb_runtime_install_abort_app(const char *app_id)
{
    const char *target_app = app_id;
    char staging_dir[VB_MAX_PATH];
    int result = RT_EOK;

    if (target_app && target_app[0] && !vb_is_safe_app_id(target_app))
    {
        rt_kprintf("usage: vb_runtime_install_abort <app_id>\n");
        return -RT_EINVAL;
    }
    if ((!target_app || !target_app[0]) && g_vb_runtime.install_app[0])
    {
        target_app = g_vb_runtime.install_app;
    }
    if (!target_app || !target_app[0]) return RT_EOK;
    if (g_vb_runtime.install_app[0] && rt_strcmp(g_vb_runtime.install_app, target_app) != 0)
    {
        rt_kprintf("[vb_runtime] install abort failed: session=%s app=%s\n",
                   g_vb_runtime.install_app, target_app);
        return -RT_EBUSY;
    }
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;

    vb_runtime_install_close_file();
    vb_build_install_dir(staging_dir, sizeof(staging_dir), VIBEBOARD_STAGING_PREFIX, target_app);
    if (access(staging_dir, 0) == 0 && vb_remove_tree(staging_dir) != RT_EOK)
    {
        rt_kprintf("[vb_runtime] install abort failed: clean staging %s\n", staging_dir);
        result = -RT_ERROR;
    }
    if (g_vb_runtime.install_app[0] && rt_strcmp(g_vb_runtime.install_app, target_app) == 0)
    {
        vb_runtime_install_clear_session();
    }
    rt_kprintf("[vb_runtime] install abort: %s rc=%d\n", target_app, result);
    return result;
}

static int vb_runtime_staging_clear_all(int *removed)
{
    DIR *dir;
    struct dirent *entry;
    int staging_prefix_len = rt_strlen(VIBEBOARD_STAGING_PREFIX);
    int count = 0;
    int result = RT_EOK;

    if (removed) *removed = 0;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;

    vb_runtime_install_close_file();
    vb_runtime_install_clear_session();

    dir = opendir(VIBEBOARD_APP_ROOT);
    if (!dir) return -RT_ERROR;
    while ((entry = readdir(dir)) != RT_NULL)
    {
        const char *name = entry->d_name;
        char path[VB_MAX_PATH];
        if (!name || rt_strcmp(name, ".") == 0 || rt_strcmp(name, "..") == 0) continue;
        if (rt_strncmp(name, VIBEBOARD_STAGING_PREFIX, staging_prefix_len) != 0) continue;
        rt_snprintf(path, sizeof(path), "%s/%s", VIBEBOARD_APP_ROOT, name);
        path[sizeof(path) - 1] = '\0';
        if (vb_remove_tree(path) != RT_EOK)
        {
            result = -RT_ERROR;
        }
        else
        {
            count++;
        }
    }
    closedir(dir);
    if (removed) *removed = count;
    rt_kprintf("[vb_runtime] staging clear removed=%d rc=%d\n", count, result);
    return result;
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

static void vb_runtime_set_app_result(int status, const char *message)
{
    g_vb_runtime.app_last_status = status;
    vb_safe_copy(g_vb_runtime.app_last_error, sizeof(g_vb_runtime.app_last_error), message ? message : "");
}

static void vb_runtime_return_home(void)
{
    g_vb_runtime.pending_stop = 0;
    g_vb_runtime.pending_reload = 0;
    g_vb_runtime.pending_manager_refresh = 0;
    vibeboard_lua_stop_app();
    vb_runtime_clear_overlay_controls();
    vb_weather_release_image();
    g_vb_runtime.app_running = 0;
    g_vb_runtime.app_failed = 0;
    g_vb_runtime.status_label = RT_NULL;
    g_vb_runtime.clock_label = RT_NULL;
    g_vb_runtime.flow_label = RT_NULL;
    g_vb_runtime.script_flow_label = RT_NULL;
    g_vb_runtime.component_count = 0;
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    (void)vb_write_active_app(VIBEBOARD_DEFAULT_APP_ID);
    vb_safe_copy(g_vb_runtime.active_app, sizeof(g_vb_runtime.active_app), VIBEBOARD_DEFAULT_APP_ID);
    vb_runtime_set_app_result(RT_EOK, "home requested");
    rt_kprintf("[vb_runtime] return home active=%s\n", g_vb_runtime.active_app);
    gui_app_run("Main");
}


static const char *vb_runtime_app_state_name(void)
{
    if (g_vb_runtime.app_failed) return "failed";
    if (g_vb_runtime.app_running) return "running";
    return "idle";
}

static int vb_json_appendf(char *dst, rt_size_t cap, int *used, const char *fmt, ...)
{
    char temp[256];
    va_list ap;
    int written;
    if (!dst || !used || *used < 0 || (rt_size_t)*used >= cap) return -RT_EINVAL;
    va_start(ap, fmt);
    written = rt_vsnprintf(temp, sizeof(temp), fmt, ap);
    va_end(ap);
    if (written < 0 || written >= (int)sizeof(temp)) return -RT_ERROR;
    if ((rt_size_t)(*used + written) >= cap) return -RT_ERROR;
    memcpy(dst + *used, temp, written);
    *used += written;
    dst[*used] = '\0';
    return RT_EOK;
}

static int vb_json_append_string(char *dst, rt_size_t cap, int *used, const char *src, rt_size_t max_chars)
{
    rt_size_t needed = 2;
    rt_size_t index = 0;
    if (!dst || !used || *used < 0 || (rt_size_t)*used >= cap) return -RT_EINVAL;
    if (!src) src = "";
    while (src[index] && (!max_chars || index < max_chars))
    {
        char c = src[index++];
        needed += (c == '"' || c == '\\') ? 2 : 1;
    }
    if ((rt_size_t)(*used) + needed >= cap) return -RT_ERROR;
    dst[(*used)++] = '"';
    index = 0;
    while (src[index] && (!max_chars || index < max_chars))
    {
        char c = src[index++];
        if (c == '"' || c == '\\')
        {
            dst[(*used)++] = '\\';
            dst[(*used)++] = c;
        }
        else if (c == '\r' || c == '\n' || c == '\t')
        {
            dst[(*used)++] = ' ';
        }
        else
        {
            dst[(*used)++] = c;
        }
    }
    dst[(*used)++] = '"';
    dst[*used] = '\0';
    return RT_EOK;
}

static void vb_app_info_copy_value(const char *text, const char *key, char *dst, rt_size_t cap, const char *fallback)
{
    const char *line;
    rt_size_t key_len;
    vb_safe_copy(dst, cap, fallback ? fallback : "");
    if (!text || !key || !dst || cap == 0) return;
    key_len = rt_strlen(key);
    line = text;
    while (*line)
    {
        const char *next = strchr(line, '\n');
        const char *p = line;
        const char *value;
        const char *end = next ? next : line + rt_strlen(line);
        char local[VB_MAX_TEXT];
        rt_size_t len;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if ((rt_size_t)(end - p) > key_len && strncmp(p, key, key_len) == 0)
        {
            p += key_len;
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == '=')
            {
                p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                value = p;
                while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
                len = (rt_size_t)(end - value);
                if (len >= sizeof(local)) len = sizeof(local) - 1;
                memcpy(local, value, len);
                local[len] = '\0';
                vb_safe_copy(dst, cap, local);
                return;
            }
        }
        if (!next) break;
        line = next + 1;
    }
}

static int vb_runtime_read_app_summary(const char *app_id, vb_app_summary_t *summary)
{
    char path[VB_MAX_PATH];
    char *json;
    char info[512];
    if (!summary) return -RT_EINVAL;
    rt_memset(summary, 0, sizeof(*summary));
    if (!vb_is_safe_app_id(app_id)) return -RT_EINVAL;
    vb_safe_copy(summary->id, sizeof(summary->id), app_id);
    vb_safe_copy(summary->name, sizeof(summary->name), app_id);
    vb_safe_copy(summary->description, sizeof(summary->description), "Runtime app package");
    vb_safe_copy(summary->category, sizeof(summary->category), "General");
    vb_safe_copy(summary->icon, sizeof(summary->icon), "app");
    vb_safe_copy(summary->author, sizeof(summary->author), "Unknown");
    vb_safe_copy(summary->screenshot, sizeof(summary->screenshot), "generated:runtime");
    vb_safe_copy(summary->requirements, sizeof(summary->requirements), "Runtime");

    vb_build_app_path(path, sizeof(path), app_id, "main.lua");
    summary->main_lua = vb_file_exists(path);

    vb_build_app_path(path, sizeof(path), app_id, "manifest.json");
    json = (char *)rt_malloc(VB_MAX_MANIFEST);
    if (json)
    {
        if (vb_read_text_file(path, json, VB_MAX_MANIFEST) > 0)
        {
            summary->manifest = 1;
            vb_json_copy_string(json, RT_NULL, "name", summary->name, sizeof(summary->name), app_id);
            vb_json_copy_string(json, RT_NULL, "description", summary->description, sizeof(summary->description), summary->description);
            vb_json_copy_string(json, RT_NULL, "category", summary->category, sizeof(summary->category), summary->category);
            vb_json_copy_string(json, RT_NULL, "icon", summary->icon, sizeof(summary->icon), summary->icon);
            vb_json_copy_string(json, RT_NULL, "author", summary->author, sizeof(summary->author), summary->author);
            vb_json_copy_string(json, RT_NULL, "screenshot", summary->screenshot, sizeof(summary->screenshot), summary->screenshot);
            vb_json_copy_string_array_csv(json, RT_NULL, "requirements", summary->requirements, sizeof(summary->requirements), summary->requirements);
        }
        rt_free(json);
    }

    vb_build_app_path(path, sizeof(path), app_id, "app.info");
    if (vb_read_text_file(path, info, sizeof(info)) > 0)
    {
        summary->app_info = 1;
        if (!summary->manifest)
        {
            vb_app_info_copy_value(info, "name", summary->name, sizeof(summary->name), app_id);
            vb_app_info_copy_value(info, "description", summary->description, sizeof(summary->description), summary->description);
            vb_app_info_copy_value(info, "category", summary->category, sizeof(summary->category), summary->category);
            vb_app_info_copy_value(info, "icon", summary->icon, sizeof(summary->icon), summary->icon);
            vb_app_info_copy_value(info, "author", summary->author, sizeof(summary->author), summary->author);
            vb_app_info_copy_value(info, "screenshot", summary->screenshot, sizeof(summary->screenshot), summary->screenshot);
            vb_app_info_copy_value(info, "requirements", summary->requirements, sizeof(summary->requirements), summary->requirements);
        }
    }

    summary->compatible = summary->main_lua && (summary->manifest || summary->app_info);
    return summary->compatible ? RT_EOK : -RT_ERROR;
}

static int vb_runtime_app_dir_summary(const char *name, const char *active, vb_app_summary_t *summary)
{
    char path[VB_MAX_PATH];
    if (!vb_is_safe_app_id(name)) return -RT_EINVAL;
    rt_snprintf(path, sizeof(path), "%s/%s", VIBEBOARD_APP_ROOT, name);
    path[sizeof(path) - 1] = '\0';
    if (!vb_path_is_dir(path)) return -RT_ERROR;
    (void)vb_runtime_read_app_summary(name, summary);
    if (summary) summary->active = active && rt_strcmp(name, active) == 0;
    return RT_EOK;
}

static int vb_runtime_app_exists(const char *app_id, vb_app_summary_t *summary)
{
    char active[VB_MAX_APP_ID];
    vb_read_active_app(active, sizeof(active));
    return vb_runtime_app_dir_summary(app_id, active, summary);
}

static void vb_runtime_sort_apps(vb_app_summary_t *items, int count)
{
    int i;
    if (!items || count <= 1) return;
    for (i = 1; i < count; i++)
    {
        vb_app_summary_t current = items[i];
        int j = i - 1;
        while (j >= 0 && rt_strcmp(items[j].id, current.id) > 0)
        {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = current;
    }
}

static int vb_runtime_grow_app_list(vb_app_summary_t **items, int *cap, int used, int needed)
{
    vb_app_summary_t *next;
    int new_cap;
    if (!items || !cap || used < 0) return -RT_EINVAL;
    if (*cap >= needed) return RT_EOK;
    new_cap = *cap > 0 ? *cap * 2 : VB_LAUNCHER_MAX_ITEMS;
    while (new_cap < needed) new_cap *= 2;
    next = (vb_app_summary_t *)rt_malloc(sizeof(vb_app_summary_t) * new_cap);
    if (!next) return -RT_ENOMEM;
    if (*items && used > 0)
    {
        rt_memcpy(next, *items, sizeof(vb_app_summary_t) * used);
        rt_free(*items);
    }
    *items = next;
    *cap = new_cap;
    return RT_EOK;
}

static int vb_runtime_load_apps(vb_app_summary_t **items, int *total)
{
    DIR *dir;
    struct dirent *entry;
    char active[VB_MAX_APP_ID];
    int count = 0;
    int cap = 0;
    int result = RT_EOK;
    if (!items || !total) return -RT_EINVAL;
    *items = RT_NULL;
    *total = 0;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    vb_read_active_app(active, sizeof(active));
    dir = opendir(VIBEBOARD_APP_ROOT);
    if (!dir) return -RT_ERROR;
    while ((entry = readdir(dir)) != RT_NULL)
    {
        vb_app_summary_t summary;
        if (!entry->d_name || entry->d_name[0] == '.') continue;
        if (vb_runtime_app_dir_summary(entry->d_name, active, &summary) != RT_EOK) continue;
        result = vb_runtime_grow_app_list(items, &cap, count, count + 1);
        if (result != RT_EOK)
        {
            break;
        }
        (*items)[count] = summary;
        count++;
    }
    closedir(dir);
    if (result != RT_EOK)
    {
        if (*items) rt_free(*items);
        *items = RT_NULL;
        return result;
    }
    vb_runtime_sort_apps(*items, count);
    *total = count;
    return RT_EOK;
}

static int vb_runtime_collect_apps(vb_app_summary_t *items, int cap, int offset, int *total)
{
    vb_app_summary_t *all = RT_NULL;
    int count = 0;
    int included = 0;
    int i;
    int result;
    if (total) *total = 0;
    if (offset < 0) offset = 0;
    result = vb_runtime_load_apps(&all, &count);
    if (result != RT_EOK) return result;
    if (total) *total = count;
    if (items && cap > 0)
    {
        for (i = offset; i < count && included < cap; i++)
        {
            items[included++] = all[i];
        }
    }
    if (all) rt_free(all);
    return included;
}

static int vb_runtime_app_status_json(char *dst, rt_size_t cap)
{
    char active[VB_MAX_APP_ID];
    int used = 0;
    int result = RT_EOK;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    vb_read_active_app(active, sizeof(active));
    result |= vb_json_appendf(dst, cap, &used, "{\"api\":");
    result |= vb_json_append_string(dst, cap, &used, VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION, 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"active\":");
    result |= vb_json_append_string(dst, cap, &used, active, 0);
    result |= vb_json_appendf(dst, cap, &used,
                              ",\"state\":");
    result |= vb_json_append_string(dst, cap, &used, vb_runtime_app_state_name(), 0);
    result |= vb_json_appendf(dst, cap, &used,
                              ",\"running\":%d,\"failed\":%d,\"last_status\":%d,\"last_error\":",
                              g_vb_runtime.app_running,
                              g_vb_runtime.app_failed,
                              g_vb_runtime.app_last_status);
    result |= vb_json_append_string(dst, cap, &used, g_vb_runtime.app_last_error, 0);
    result |= vb_json_appendf(dst, cap, &used,
                              ",\"launches\":%lu,\"stops\":%lu,\"pending_reload\":%d,\"pending_stop\":%d,\"launcher_page\":%d,\"launcher_total\":%d,\"launcher_count\":%d,\"pending_delete\":",
                              (unsigned long)g_vb_runtime.app_launch_count,
                              (unsigned long)g_vb_runtime.app_stop_count,
                              g_vb_runtime.pending_reload ? 1 : 0,
                              g_vb_runtime.pending_stop ? 1 : 0,
                              g_vb_runtime.launcher_page,
                              g_vb_runtime.launcher_total,
                              g_vb_runtime.launcher_count);
    result |= vb_json_append_string(dst, cap, &used, g_vb_runtime.launcher_pending_delete, 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"lua\":");
    result |= vb_json_append_string(dst, cap, &used,
                                    vibeboard_lua_runtime_available() ? vibeboard_lua_runtime_name() : "manifest-fallback",
                                    0);
    result |= vb_json_appendf(dst, cap, &used, "}");
    if (result != RT_EOK)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int vb_runtime_append_app_summary_json(char *dst, rt_size_t cap, int *used, const vb_app_summary_t *summary)
{
    int result = RT_EOK;
    if (!summary) return -RT_EINVAL;
    result |= vb_json_appendf(dst, cap, used, "{\"id\":");
    result |= vb_json_append_string(dst, cap, used, summary->id, 0);
    result |= vb_json_appendf(dst, cap, used, ",\"name\":");
    result |= vb_json_append_string(dst, cap, used, summary->name, 0);
    result |= vb_json_appendf(dst, cap, used, ",\"description\":");
    result |= vb_json_append_string(dst, cap, used, summary->description, 80);
    result |= vb_json_appendf(dst, cap, used, ",\"category\":");
    result |= vb_json_append_string(dst, cap, used, summary->category, 32);
    result |= vb_json_appendf(dst, cap, used, ",\"icon\":");
    result |= vb_json_append_string(dst, cap, used, summary->icon, 32);
    result |= vb_json_appendf(dst, cap, used, ",\"author\":");
    result |= vb_json_append_string(dst, cap, used, summary->author, 48);
    result |= vb_json_appendf(dst, cap, used, ",\"screenshot\":");
    result |= vb_json_append_string(dst, cap, used, summary->screenshot, 80);
    result |= vb_json_appendf(dst, cap, used, ",\"requirements\":");
    result |= vb_json_append_string(dst, cap, used, summary->requirements, 80);
    result |= vb_json_appendf(dst, cap, used,
                              ",\"active\":%d,\"compatible\":%d,\"manifest\":%d,\"app_info\":%d,\"main_lua\":%d}",
                              summary->active,
                              summary->compatible,
                              summary->manifest,
                              summary->app_info,
                              summary->main_lua);
    return result;
}

static int vb_runtime_app_list_page_json(char *dst, rt_size_t cap, int offset, int limit)
{
    static vb_app_summary_t apps[VB_LAUNCHER_MAX_ITEMS];
    char active[VB_MAX_APP_ID];
    int total = 0;
    int count;
    int used = 0;
    int first = 1;
    int i;
    int result = RT_EOK;
    if (!dst || cap == 0) return -RT_EINVAL;
    if (offset < 0) offset = 0;
    if (limit <= 0 || limit > VB_LAUNCHER_MAX_ITEMS) limit = VB_LAUNCHER_MAX_ITEMS;
    dst[0] = '\0';
    count = vb_runtime_collect_apps(apps, limit, offset, &total);
    if (count < 0)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"fs\":0,\"offset\":%d,\"limit\":%d,\"count\":0,\"included\":0,\"apps\":[]}",
                    VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION, offset, limit);
        dst[cap - 1] = '\0';
        return count;
    }
    vb_read_active_app(active, sizeof(active));
    result |= vb_json_appendf(dst, cap, &used, "{\"api\":");
    result |= vb_json_append_string(dst, cap, &used, VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION, 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"active\":");
    result |= vb_json_append_string(dst, cap, &used, active, 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"state\":");
    result |= vb_json_append_string(dst, cap, &used, vb_runtime_app_state_name(), 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"offset\":%d,\"limit\":%d,\"apps\":[", offset, limit);
    for (i = 0; i < count; i++)
    {
        if (!first) result |= vb_json_appendf(dst, cap, &used, ",");
        result |= vb_runtime_append_app_summary_json(dst, cap, &used, &apps[i]);
        first = 0;
    }
    result |= vb_json_appendf(dst, cap, &used,
                              "],\"count\":%d,\"included\":%d,\"truncated\":0}",
                              total,
                              count);
    if (result != RT_EOK)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"error\":\"truncated\",\"offset\":%d,\"limit\":%d,\"count\":%d}",
                    VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION, offset, limit, total);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int vb_runtime_app_list_json(char *dst, rt_size_t cap)
{
    vb_app_summary_t *apps = RT_NULL;
    char active[VB_MAX_APP_ID];
    int used = 0;
    int first = 1;
    int count = 0;
    int included = 0;
    int truncated = 0;
    int i;
    int result = RT_EOK;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    result = vb_runtime_load_apps(&apps, &count);
    if (result != RT_EOK)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"fs\":0,\"count\":0,\"apps\":[]}",
                    VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION);
        dst[cap - 1] = '\0';
        return result;
    }
    vb_read_active_app(active, sizeof(active));
    result = RT_EOK;
    result |= vb_json_appendf(dst, cap, &used, "{\"api\":");
    result |= vb_json_append_string(dst, cap, &used, VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION, 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"active\":");
    result |= vb_json_append_string(dst, cap, &used, active, 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"state\":");
    result |= vb_json_append_string(dst, cap, &used, vb_runtime_app_state_name(), 0);
    result |= vb_json_appendf(dst, cap, &used, ",\"apps\":[");
    if (result != RT_EOK) truncated = 1;

    for (i = 0; i < count; i++)
    {
        if (truncated) continue;
        if (!first && vb_json_appendf(dst, cap, &used, ",") != RT_EOK)
        {
            truncated = 1;
            continue;
        }
        if (vb_runtime_append_app_summary_json(dst, cap, &used, &apps[i]) != RT_EOK)
        {
            truncated = 1;
            continue;
        }
        first = 0;
        included++;
    }
    if (vb_json_appendf(dst, cap, &used,
                        "],\"count\":%d,\"included\":%d,\"truncated\":%d}",
                        count,
                        included,
                        truncated) != RT_EOK)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"error\":\"truncated\",\"count\":%d}",
                    VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION,
                    count);
        dst[cap - 1] = '\0';
        if (apps) rt_free(apps);
        return -RT_ERROR;
    }
    if (apps) rt_free(apps);
    return truncated ? -RT_ERROR : RT_EOK;
}

static int vb_runtime_app_launch(const char *app_id)
{
    vb_app_summary_t summary;
    int result;
    if (!vb_is_safe_app_id(app_id)) return -RT_EINVAL;
    result = vb_runtime_app_exists(app_id, &summary);
    if (result != RT_EOK || !summary.compatible)
    {
        vb_runtime_set_app_result(-RT_ERROR, "app not found or incompatible");
        return -RT_ERROR;
    }
    result = vb_runtime_select_app(app_id);
    if (result == RT_EOK)
    {
        g_vb_runtime.app_launch_count++;
        g_vb_runtime.app_failed = 0;
        vb_runtime_set_app_result(RT_EOK, "launch requested");
    }
    return result;
}

static int vb_runtime_app_stop(void)
{
    if (!g_vb_runtime.running)
    {
        g_vb_runtime.app_running = 0;
        g_vb_runtime.app_failed = 0;
        vb_runtime_set_app_result(RT_EOK, "already on home");
        return RT_EOK;
    }
    g_vb_runtime.pending_stop = 1;
    vb_runtime_set_app_result(RT_EOK, "stop requested");
    return RT_EOK;
}

static int vb_runtime_app_delete(const char *app_id)
{
    char active[VB_MAX_APP_ID];
    char path[VB_MAX_PATH];
    int is_active;
    if (!vb_is_safe_app_id(app_id)) return -RT_EINVAL;
    if (g_vb_runtime.install_app[0] && rt_strcmp(g_vb_runtime.install_app, app_id) == 0)
    {
        vb_runtime_set_app_result(-RT_EBUSY, "install session active");
        return -RT_EBUSY;
    }
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    vb_read_active_app(active, sizeof(active));
    is_active = rt_strcmp(active, app_id) == 0;
    if (is_active && (g_vb_runtime.app_running || g_vb_runtime.pending_reload))
    {
        vb_runtime_set_app_result(-RT_EBUSY, "stop active app before delete");
        return -RT_EBUSY;
    }
    rt_snprintf(path, sizeof(path), "%s/%s", VIBEBOARD_APP_ROOT, app_id);
    path[sizeof(path) - 1] = '\0';
    if (!vb_path_is_dir(path))
    {
        vb_runtime_set_app_result(-RT_ERROR, "app not found");
        return -RT_ERROR;
    }
    if (vb_remove_tree(path) != RT_EOK)
    {
        vb_runtime_set_app_result(-RT_ERROR, "delete failed");
        return -RT_ERROR;
    }
    if (is_active)
    {
        (void)vb_write_active_app(VIBEBOARD_DEFAULT_APP_ID);
        vb_safe_copy(g_vb_runtime.active_app, sizeof(g_vb_runtime.active_app), VIBEBOARD_DEFAULT_APP_ID);
    }
    vb_runtime_set_app_result(RT_EOK, "app deleted");
    return RT_EOK;
}

static int vb_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int vb_write_hex_chunk(int fd, const char *hex, int *written_bytes)
{
    uint8_t buffer[128];
    int count = 0;
    int total = 0;
    int i;
    int len;
    if (written_bytes) *written_bytes = 0;
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
            total += count;
            count = 0;
        }
    }
    if (count > 0)
    {
        if (write(fd, buffer, count) != count) return -RT_ERROR;
        total += count;
    }
    if (written_bytes) *written_bytes = total;
    return RT_EOK;
}

static int vb_is_safe_flow_channel(const char *channel)
{
    int i;
    int len;
    if (!channel || !channel[0]) return 0;
    len = rt_strlen(channel);
    if (len <= 0 || len >= VB_FLOW_MAX_CHANNEL) return 0;
    for (i = 0; i < len; i++)
    {
        char c = channel[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
        {
            continue;
        }
        return 0;
    }
    return 1;
}

static int vb_decode_hex_text(const char *hex, char *dst, rt_size_t cap, uint32_t *bytes_out)
{
    int i;
    int len;
    uint32_t bytes = 0;

    if (!hex || !dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    if (bytes_out) *bytes_out = 0;
    if (rt_strcmp(hex, "-") == 0) return RT_EOK;

    len = rt_strlen(hex);
    if ((len % 2) != 0 || len > VB_FLOW_MAX_PAYLOAD * 2) return -RT_EINVAL;
    for (i = 0; i < len; i += 2)
    {
        int hi = vb_hex_value(hex[i]);
        int lo = vb_hex_value(hex[i + 1]);
        char ch;
        if (hi < 0 || lo < 0) return -RT_EINVAL;
        if (bytes + 1 >= cap) return -RT_EINVAL;
        ch = (char)((hi << 4) | lo);
        if (ch == '\0' || ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        dst[bytes++] = ch;
    }
    dst[bytes] = '\0';
    if (bytes_out) *bytes_out = bytes;
    return RT_EOK;
}

static int vb_hex_encode_bytes(const uint8_t *src, uint32_t len, char *dst, rt_size_t cap)
{
    static const char hexdigits[] = "0123456789abcdef";
    uint32_t i;
    if (!src || !dst || cap == 0) return -RT_EINVAL;
    if ((rt_size_t)len * 2 + 1 > cap) return -RT_EINVAL;
    for (i = 0; i < len; i++)
    {
        dst[i * 2] = hexdigits[(src[i] >> 4) & 0x0f];
        dst[i * 2 + 1] = hexdigits[src[i] & 0x0f];
    }
    dst[len * 2] = '\0';
    return RT_EOK;
}

static int vb_runtime_ble_json_read(const char *kind, uint32_t offset, uint32_t max_bytes, char *dst, rt_size_t cap)
{
    char json[VB_JSON_READ_MAX];
    const uint8_t *src;
    uint32_t total;
    uint32_t count;
    int result = -RT_EINVAL;

    if (!kind || !dst || cap == 0) return -RT_EINVAL;
    if (max_bytes == 0 || max_bytes > 160) max_bytes = 160;

    if (rt_strcmp(kind, "capabilities") == 0)
    {
        result = vb_runtime_capabilities_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "sensors") == 0)
    {
        result = vb_runtime_sensors_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "power") == 0)
    {
        result = vb_runtime_power_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "display") == 0 || rt_strcmp(kind, "screen") == 0)
    {
        result = vb_runtime_display_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "gpio") == 0)
    {
        result = vb_runtime_gpio_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "touch") == 0)
    {
        result = vb_runtime_touch_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "rgb") == 0)
    {
        result = vb_runtime_rgb_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "voice") == 0 || rt_strcmp(kind, "audio") == 0)
    {
        result = vb_runtime_voice_read_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "app") == 0 || rt_strcmp(kind, "app_status") == 0)
    {
        result = vb_runtime_app_status_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "apps") == 0 || rt_strcmp(kind, "app_list") == 0)
    {
        result = vb_runtime_app_list_json(json, sizeof(json));
    }
    else if (rt_strcmp(kind, "apps_page") == 0 || rt_strcmp(kind, "app_page") == 0)
    {
        result = vb_runtime_app_list_page_json(json, sizeof(json), 0, VB_LAUNCHER_MAX_ITEMS);
    }
    else
    {
        rt_snprintf(dst, cap, "err json_read kind=%s rc=%d", kind, -RT_EINVAL);
        dst[cap - 1] = '\0';
        return -RT_EINVAL;
    }

    if (result != RT_EOK)
    {
        rt_snprintf(dst, cap, "err json_read kind=%s rc=%d", kind, result);
        dst[cap - 1] = '\0';
        return result;
    }

    total = (uint32_t)rt_strlen(json);
    if (offset > total) offset = total;
    count = total - offset;
    if (count > max_bytes) count = max_bytes;
    src = (const uint8_t *)json + offset;

    rt_snprintf(dst, cap, "ok json_read kind=%s offset=%lu total=%lu bytes=%lu hex=",
                kind,
                (unsigned long)offset,
                (unsigned long)total,
                (unsigned long)count);
    dst[cap - 1] = '\0';
    if (vb_hex_encode_bytes(src, count, dst + rt_strlen(dst), cap - rt_strlen(dst)) != RT_EOK)
    {
        rt_snprintf(dst, cap, "err json_read kind=%s rc=%d", kind, -RT_ERROR);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return RT_EOK;
}

#if VB_RUNTIME_HAS_VOICE_CAPTURE
static int vb_runtime_voice_audio_callback(audio_server_callback_cmt_t cmd,
                                           void *callback_userdata,
                                           uint32_t reserved)
{
    vb_voice_state_t *voice = (vb_voice_state_t *)callback_userdata;
    if (!voice) return 0;
    if (cmd == as_callback_cmd_data_coming)
    {
        audio_server_coming_data_t *coming = (audio_server_coming_data_t *)reserved;
        uint32_t copy_len;
        if (!voice->recording || !coming || !coming->data || coming->data_len == 0)
        {
            return 0;
        }
        copy_len = coming->data_len;
        if (voice->recorded_bytes + copy_len > voice->max_bytes)
        {
            copy_len = voice->max_bytes > voice->recorded_bytes ?
                       voice->max_bytes - voice->recorded_bytes : 0;
        }
        if (copy_len > 0)
        {
            rt_memcpy(voice->buffer + voice->recorded_bytes, coming->data, copy_len);
            voice->recorded_bytes += copy_len;
        }
        if (copy_len < coming->data_len)
        {
            voice->dropped_bytes += coming->data_len - copy_len;
        }
    }
    else if (cmd == as_callback_cmd_closed || cmd == as_callback_cmd_suspended)
    {
        if (voice->done_sem)
        {
            rt_sem_release(voice->done_sem);
        }
    }
    return 0;
}
#endif

static int vb_runtime_voice_ensure_buffer(void)
{
    if (g_vb_voice.buffer) return RT_EOK;
    g_vb_voice.max_bytes = VB_VOICE_MAX_BYTES;
    g_vb_voice.buffer = (uint8_t *)rt_malloc(g_vb_voice.max_bytes);
    if (!g_vb_voice.buffer)
    {
        g_vb_voice.max_bytes = 0;
        return -RT_ENOMEM;
    }
    return RT_EOK;
}

#if VB_RUNTIME_HAS_VOICE_CAPTURE
static void vb_runtime_voice_wait_for_worker(void)
{
    while (g_vb_voice.worker)
    {
        rt_thread_mdelay(20);
    }
}

static void vb_runtime_voice_stop_client(void)
{
    if (g_vb_voice.client)
    {
        audio_close(g_vb_voice.client);
        g_vb_voice.client = RT_NULL;
    }
}
#endif

static void vb_runtime_voice_clear(void)
{
#if VB_RUNTIME_HAS_VOICE_CAPTURE
    if (g_vb_voice.worker)
    {
        g_vb_voice.stop_requested = 1;
        vb_runtime_voice_stop_client();
        vb_runtime_voice_wait_for_worker();
    }
    else
    {
        vb_runtime_voice_stop_client();
    }
    g_vb_voice.stop_requested = 0;
#endif
    g_vb_voice.recording = 0;
    g_vb_voice.ready = 0;
    g_vb_voice.last_error = 0;
    g_vb_voice.requested_ms = 0;
    g_vb_voice.recorded_bytes = 0;
    g_vb_voice.dropped_bytes = 0;
    if (g_vb_voice.buffer && g_vb_voice.max_bytes > 0)
    {
        rt_memset(g_vb_voice.buffer, 0, g_vb_voice.max_bytes);
    }
}

#if VB_RUNTIME_HAS_VOICE_CAPTURE
static void vb_runtime_voice_worker_entry(void *parameter)
{
    uint32_t duration_ms = (uint32_t)(rt_uint32_t)parameter;
    audio_parameter_t param = {0};

    if (!g_vb_voice.done_sem)
    {
        g_vb_voice.done_sem = rt_sem_create("vbv_done", 0, RT_IPC_FLAG_FIFO);
        if (!g_vb_voice.done_sem)
        {
            g_vb_voice.recording = 0;
            g_vb_voice.last_error = -RT_ENOMEM;
            g_vb_voice.worker = RT_NULL;
            return;
        }
    }

    param.write_bits_per_sample = VB_VOICE_BITS_PER_SAMPLE;
    param.write_channnel_num = VB_VOICE_CHANNELS;
    param.write_samplerate = VB_VOICE_SAMPLE_RATE;
    param.write_cache_size = VB_VOICE_CACHE_SIZE;
    param.read_bits_per_sample = VB_VOICE_BITS_PER_SAMPLE;
    param.read_channnel_num = VB_VOICE_CHANNELS;
    param.read_samplerate = VB_VOICE_SAMPLE_RATE;
    param.read_cache_size = VB_VOICE_CACHE_SIZE;

    g_vb_voice.client = audio_open(AUDIO_TYPE_LOCAL_RECORD, AUDIO_RX, &param,
                                   vb_runtime_voice_audio_callback, &g_vb_voice);
    if (!g_vb_voice.client)
    {
        g_vb_voice.recording = 0;
        g_vb_voice.last_error = -RT_ERROR;
        g_vb_voice.worker = RT_NULL;
        rt_kprintf("[vb_runtime][voice] audio_open failed\n");
        return;
    }

    while (duration_ms > 0 && !g_vb_voice.stop_requested)
    {
        uint32_t step = duration_ms > 50 ? 50 : duration_ms;
        rt_thread_mdelay(step);
        duration_ms -= step;
    }
    vb_runtime_voice_stop_client();
    g_vb_voice.client = RT_NULL;
    g_vb_voice.recording = 0;
    g_vb_voice.ready = g_vb_voice.recorded_bytes > 0 ? 1 : 0;
    g_vb_voice.last_error = g_vb_voice.stop_requested ? -RT_EINTR :
                            (g_vb_voice.ready ? RT_EOK : -RT_ERROR);
    rt_kprintf("[vb_runtime][voice] captured seq=%lu ms=%lu bytes=%lu dropped=%lu\n",
               (unsigned long)g_vb_voice.sequence,
               (unsigned long)g_vb_voice.requested_ms,
               (unsigned long)g_vb_voice.recorded_bytes,
               (unsigned long)g_vb_voice.dropped_bytes);
    g_vb_voice.stop_requested = 0;
    g_vb_voice.worker = RT_NULL;
}
#endif

static int vb_runtime_voice_start(uint32_t duration_ms)
{
#if VB_RUNTIME_HAS_VOICE_CAPTURE
    int result;
    rt_thread_t worker;

    if (duration_ms == 0) duration_ms = VB_VOICE_DEFAULT_MS;
    if (duration_ms > VB_VOICE_MAX_MS) duration_ms = VB_VOICE_MAX_MS;
    if (g_vb_voice.recording || g_vb_voice.worker) return -RT_EBUSY;

    result = vb_runtime_voice_ensure_buffer();
    if (result != RT_EOK)
    {
        g_vb_voice.last_error = result;
        return result;
    }

    vb_runtime_voice_clear();
    g_vb_voice.requested_ms = duration_ms;
    g_vb_voice.sequence++;
    g_vb_voice.recording = 1;
    g_vb_voice.ready = 0;
    g_vb_voice.last_error = 1;

    worker = rt_thread_create("vbvoice", vb_runtime_voice_worker_entry,
                              (void *)(rt_uint32_t)duration_ms,
                              VB_VOICE_THREAD_STACK,
                              RT_THREAD_PRIORITY_MIDDLE + 4,
                              RT_THREAD_TICK_DEFAULT);
    if (!worker)
    {
        g_vb_voice.recording = 0;
        g_vb_voice.last_error = -RT_ENOMEM;
        return -RT_ENOMEM;
    }
    g_vb_voice.worker = worker;
    rt_thread_startup(worker);
    rt_kprintf("[vb_runtime][voice] capture started seq=%lu ms=%lu\n",
               (unsigned long)g_vb_voice.sequence,
               (unsigned long)duration_ms);
    return RT_EOK;
#else
    (void)duration_ms;
    g_vb_voice.last_error = -RT_ENOSYS;
    return -RT_ENOSYS;
#endif
}

static int vb_runtime_voice_status(char *dst, rt_size_t cap)
{
    if (!dst || cap == 0) return -RT_EINVAL;
    rt_snprintf(dst, cap,
                "ok voice api=%s built=%d ready=%d recording=%d seq=%lu bytes=%lu rate=%d bits=%d channels=%d dropped=%lu err=%d",
                VIBEBOARD_RUNTIME_VOICE_API_VERSION,
                VB_RUNTIME_HAS_VOICE_CAPTURE,
                g_vb_voice.ready,
                g_vb_voice.recording,
                (unsigned long)g_vb_voice.sequence,
                (unsigned long)g_vb_voice.recorded_bytes,
                VB_VOICE_SAMPLE_RATE,
                VB_VOICE_BITS_PER_SAMPLE,
                VB_VOICE_CHANNELS,
                (unsigned long)g_vb_voice.dropped_bytes,
                g_vb_voice.last_error);
    dst[cap - 1] = '\0';
    return RT_EOK;
}

static int vb_runtime_voice_read_json(char *dst, rt_size_t cap)
{
    int used;
    if (!dst || cap == 0) return -RT_EINVAL;
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"built\":%d,\"ready\":%d,"
                       "\"recording\":%d,\"seq\":%lu,\"requested_ms\":%lu,\"bytes\":%lu,"
                       "\"rate\":%d,\"bits\":%d,\"channels\":%d,\"dropped\":%lu,\"err\":%d}",
                       VIBEBOARD_RUNTIME_VOICE_API_VERSION,
                       VB_RUNTIME_HAS_VOICE_CAPTURE,
                       VB_RUNTIME_HAS_VOICE_CAPTURE,
                       g_vb_voice.ready,
                       g_vb_voice.recording,
                       (unsigned long)g_vb_voice.sequence,
                       (unsigned long)g_vb_voice.requested_ms,
                       (unsigned long)g_vb_voice.recorded_bytes,
                       VB_VOICE_SAMPLE_RATE,
                       VB_VOICE_BITS_PER_SAMPLE,
                       VB_VOICE_CHANNELS,
                       (unsigned long)g_vb_voice.dropped_bytes,
                       g_vb_voice.last_error);
    dst[cap - 1] = '\0';
    if (used < 0 || used >= (int)cap)
    {
        rt_snprintf(dst, cap, "{\"api\":\"%s\",\"error\":\"truncated\"}",
                    VIBEBOARD_RUNTIME_VOICE_API_VERSION);
        dst[cap - 1] = '\0';
        return -RT_ERROR;
    }
    return RT_EOK;
}

static uint32_t vb_runtime_voice_duration_from_text(const char *duration_text)
{
    const char *text = duration_text;
    uint32_t duration;
    while (text && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) text++;
    if (!text || !text[0] || rt_strcmp(text, "--") == 0) return 0;
    duration = (uint32_t)strtoul(text, RT_NULL, 10);
    if (duration > VB_VOICE_MAX_MS) duration = VB_VOICE_MAX_MS;
    return duration;
}

static int vb_runtime_voice_start_text(const char *duration_text, char *dst, rt_size_t cap)
{
    uint32_t duration_ms = vb_runtime_voice_duration_from_text(duration_text);
    int result;
    if (!dst || cap == 0) return -RT_EINVAL;
    result = vb_runtime_voice_start(duration_ms);
    vb_runtime_voice_read_json(dst, cap);
    return result;
}

static int vb_runtime_voice_clear_json(char *dst, rt_size_t cap)
{
    if (!dst || cap == 0) return -RT_EINVAL;
    vb_runtime_voice_clear();
    return vb_runtime_voice_read_json(dst, cap);
}

static int vb_runtime_voice_is_start_action(const char *capability)
{
    return capability &&
           (rt_strcmp(capability, "voice.start") == 0 ||
            rt_strcmp(capability, "voice.record") == 0 ||
            rt_strcmp(capability, "vibeboard.voice.start") == 0 ||
            rt_strcmp(capability, "vibeboard.voice.record") == 0);
}

static int vb_runtime_voice_is_clear_action(const char *capability)
{
    return capability &&
           (rt_strcmp(capability, "voice.clear") == 0 ||
            rt_strcmp(capability, "vibeboard.voice.clear") == 0);
}

static const char *vb_runtime_voice_short_selector(const char *selector)
{
    if (!selector) return "";
    if (rt_strncmp(selector, "voice.", 6) == 0) return selector + 6;
    if (rt_strncmp(selector, "audio.", 6) == 0) return selector + 6;
    if (rt_strncmp(selector, "vibeboard.voice.", 16) == 0) return selector + 16;
    return selector;
}

static int vb_runtime_voice_format_text(const char *selector, char *dst, rt_size_t cap)
{
    const char *short_selector = vb_runtime_voice_short_selector(selector);
    if (!short_selector || !dst || cap == 0) return 0;
    if (rt_strcmp(short_selector, "ready") == 0)
    {
        rt_snprintf(dst, cap, "%s", g_vb_voice.ready ? "ready" : "empty");
        return 1;
    }
    if (rt_strcmp(short_selector, "recording") == 0 || rt_strcmp(short_selector, "state") == 0)
    {
        rt_snprintf(dst, cap, "%s", g_vb_voice.recording ? "recording" : "idle");
        return 1;
    }
    if (rt_strcmp(short_selector, "seq") == 0 || rt_strcmp(short_selector, "sequence") == 0)
    {
        rt_snprintf(dst, cap, "%lu", (unsigned long)g_vb_voice.sequence);
        return 1;
    }
    if (rt_strcmp(short_selector, "bytes") == 0)
    {
        rt_snprintf(dst, cap, "%lu B", (unsigned long)g_vb_voice.recorded_bytes);
        return 1;
    }
    if (rt_strcmp(short_selector, "duration") == 0 || rt_strcmp(short_selector, "requested_ms") == 0)
    {
        rt_snprintf(dst, cap, "%lums", (unsigned long)g_vb_voice.requested_ms);
        return 1;
    }
    if (rt_strcmp(short_selector, "dropped") == 0)
    {
        rt_snprintf(dst, cap, "%lu B", (unsigned long)g_vb_voice.dropped_bytes);
        return 1;
    }
    if (rt_strcmp(short_selector, "error") == 0 || rt_strcmp(short_selector, "err") == 0)
    {
        rt_snprintf(dst, cap, "%d", g_vb_voice.last_error);
        return 1;
    }
    if (rt_strcmp(short_selector, "rate") == 0)
    {
        rt_snprintf(dst, cap, "%d Hz", VB_VOICE_SAMPLE_RATE);
        return 1;
    }
    if (rt_strcmp(short_selector, "built") == 0 || rt_strcmp(short_selector, "available") == 0)
    {
        rt_snprintf(dst, cap, "%d", VB_RUNTIME_HAS_VOICE_CAPTURE);
        return 1;
    }
    return 0;
}

static int vb_runtime_voice_read_hex(uint32_t offset, uint32_t max_bytes, char *dst, rt_size_t cap)
{
    uint32_t count;
    int result;
    if (!dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    if (!g_vb_voice.ready || !g_vb_voice.buffer) return -RT_ERROR;
    if (offset > g_vb_voice.recorded_bytes) return -RT_EINVAL;
    if (max_bytes == 0 || max_bytes > VB_VOICE_CHUNK_BYTES) max_bytes = VB_VOICE_CHUNK_BYTES;
    count = g_vb_voice.recorded_bytes - offset;
    if (count > max_bytes) count = max_bytes;
    if ((rt_size_t)count * 2 + 96 > cap) return -RT_EINVAL;
    rt_snprintf(dst, cap, "ok voice_data seq=%lu offset=%lu bytes=%lu hex=",
                (unsigned long)g_vb_voice.sequence,
                (unsigned long)offset,
                (unsigned long)count);
    result = vb_hex_encode_bytes(g_vb_voice.buffer + offset, count,
                                 dst + rt_strlen(dst), cap - rt_strlen(dst));
    return result == RT_EOK ? RT_EOK : result;
}

static int vb_runtime_voice_json_status_command(void)
{
    char json[VB_VOICE_JSON_MAX];
    int result = vb_runtime_voice_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_voice_status_command(void)
{
    char status[VB_BLE_STATUS_MAX];
    vb_runtime_voice_status(status, sizeof(status));
    rt_kprintf("[vb_runtime][voice] %s\n", status);
    return RT_EOK;
}

static void vb_runtime_flow_escape_text(const char *src, char *dst, rt_size_t cap)
{
    rt_size_t used = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (*src && used + 1 < cap)
    {
        char c = *src++;
        if ((c == '\\' || c == '"') && used + 2 < cap)
        {
            dst[used++] = '\\';
            dst[used++] = c;
        }
        else if (c == '\n' && used + 2 < cap)
        {
            dst[used++] = '\\';
            dst[used++] = 'n';
        }
        else if (c == '\r' && used + 2 < cap)
        {
            dst[used++] = '\\';
            dst[used++] = 'r';
        }
        else if ((unsigned char)c >= 0x20)
        {
            dst[used++] = c;
        }
    }
    dst[used] = '\0';
}

static void vb_runtime_flow_copy_json_string(const char *json, const char *key, char *dst, rt_size_t cap)
{
    vb_json_copy_string(json, RT_NULL, key, dst, cap, "");
}

static int vb_runtime_flow_latest_index(void)
{
    if (g_vb_flow.count <= 0) return -1;
    return (g_vb_flow.write_index + VB_FLOW_HISTORY - 1) % VB_FLOW_HISTORY;
}

static void vb_runtime_flow_format_summary(char *dst, rt_size_t cap)
{
    int index;
    vb_info_flow_item_t *item;
    if (!dst || cap == 0) return;
    index = vb_runtime_flow_latest_index();
    if (index < 0)
    {
        rt_snprintf(dst, cap, "flow ready: waiting for phone");
        dst[cap - 1] = '\0';
        return;
    }
    item = &g_vb_flow.items[index];
    rt_snprintf(dst, cap, "flow %lu/%s: %s",
                (unsigned long)item->sequence,
                item->channel,
                item->payload[0] ? item->payload : "(empty)");
    dst[cap - 1] = '\0';
}

static const char *vb_runtime_flow_field_from_capability(const char *capability)
{
    if (!capability) return RT_NULL;
    if (rt_strncmp(capability, "vibeboard.flow.", 15) == 0) return capability + 15;
    if (rt_strncmp(capability, "flow.", 5) == 0) return capability + 5;
    return capability;
}

static int vb_runtime_flow_format_text(const char *capability, char *dst, rt_size_t cap)
{
    const char *field = vb_runtime_flow_field_from_capability(capability);
    int index;
    vb_info_flow_item_t *item;
    if (!field || !dst || cap == 0) return 0;
    if (rt_strcmp(field, "total") == 0)
    {
        rt_snprintf(dst, cap, "%lu", (unsigned long)g_vb_flow.total_count);
        dst[cap - 1] = '\0';
        return 1;
    }
    if (rt_strcmp(field, "retained") == 0 || rt_strcmp(field, "count") == 0)
    {
        rt_snprintf(dst, cap, "%d", g_vb_flow.count);
        dst[cap - 1] = '\0';
        return 1;
    }
    if (rt_strcmp(field, "capacity") == 0)
    {
        rt_snprintf(dst, cap, "%d", VB_FLOW_HISTORY);
        dst[cap - 1] = '\0';
        return 1;
    }

    index = vb_runtime_flow_latest_index();
    if (rt_strcmp(field, "latest") == 0 ||
        rt_strcmp(field, "summary") == 0 ||
        rt_strcmp(field, "last") == 0)
    {
        vb_runtime_flow_format_summary(dst, cap);
        return 1;
    }
    if (index < 0)
    {
        rt_snprintf(dst, cap, "--");
        dst[cap - 1] = '\0';
        return 1;
    }

    item = &g_vb_flow.items[index];
    if (rt_strcmp(field, "payload") == 0 || rt_strcmp(field, "text") == 0)
    {
        rt_snprintf(dst, cap, "%s", item->payload[0] ? item->payload : "(empty)");
    }
    else if (rt_strcmp(field, "channel") == 0)
    {
        rt_snprintf(dst, cap, "%s", item->channel);
    }
    else if (rt_strcmp(field, "seq") == 0 || rt_strcmp(field, "sequence") == 0)
    {
        rt_snprintf(dst, cap, "%lu", (unsigned long)item->sequence);
    }
    else if (rt_strcmp(field, "bytes") == 0)
    {
        rt_snprintf(dst, cap, "%lu", (unsigned long)item->bytes);
    }
    else
    {
        return 0;
    }
    dst[cap - 1] = '\0';
    return 1;
}

static void vb_runtime_flow_update_label(void)
{
    char text[VB_FLOW_MAX_PAYLOAD + VB_FLOW_MAX_CHANNEL + 48];
    if (!g_vb_runtime.flow_label) return;
    vb_runtime_flow_format_summary(text, sizeof(text));
    lv_label_set_text(g_vb_runtime.flow_label, text);
    g_vb_runtime.flow_seen_total = g_vb_flow.total_count;
}

static int vb_runtime_flow_save_latest(void)
{
    int index = vb_runtime_flow_latest_index();
    vb_info_flow_item_t *item;
    char channel[VB_FLOW_MAX_CHANNEL * 2];
    char payload[VB_FLOW_MAX_PAYLOAD * 2 + 8];
    char json[VB_FLOW_MAX_PAYLOAD * 2 + VB_FLOW_MAX_CHANNEL * 2 + 128];
    if (index < 0)
    {
        if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
        unlink(VIBEBOARD_FLOW_STATE_FILE);
        return RT_EOK;
    }
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    item = &g_vb_flow.items[index];
    vb_runtime_flow_escape_text(item->channel, channel, sizeof(channel));
    vb_runtime_flow_escape_text(item->payload, payload, sizeof(payload));
    rt_snprintf(json, sizeof(json),
                "{\"api\":\"%s\",\"seq\":%lu,\"channel\":\"%s\",\"bytes\":%lu,\"payload\":\"%s\"}\n",
                VIBEBOARD_RUNTIME_FLOW_API_VERSION,
                (unsigned long)item->sequence,
                channel,
                (unsigned long)item->bytes,
                payload);
    json[sizeof(json) - 1] = '\0';
    return vb_write_text_file(VIBEBOARD_FLOW_STATE_FILE, json);
}

static int vb_runtime_flow_load_state(void)
{
    char json[VB_FLOW_MAX_PAYLOAD * 2 + VB_FLOW_MAX_CHANNEL * 2 + 128];
    char api[64];
    char channel[VB_FLOW_MAX_CHANNEL];
    char payload[VB_FLOW_MAX_PAYLOAD + 1];
    int seq = 0;
    int bytes = 0;
    vb_info_flow_item_t *item;
    if (g_vb_flow.count > 0) return RT_EOK;
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    if (vb_read_text_file(VIBEBOARD_FLOW_STATE_FILE, json, sizeof(json)) <= 0) return -RT_ERROR;
    vb_runtime_flow_copy_json_string(json, "api", api, sizeof(api));
    if (rt_strcmp(api, VIBEBOARD_RUNTIME_FLOW_API_VERSION) != 0) return -RT_EINVAL;
    vb_runtime_flow_copy_json_string(json, "channel", channel, sizeof(channel));
    vb_runtime_flow_copy_json_string(json, "payload", payload, sizeof(payload));
    if (!vb_is_safe_flow_channel(channel)) return -RT_EINVAL;
    if (!vb_json_read_int(json, RT_NULL, "seq", &seq)) return -RT_EINVAL;
    if (!vb_json_read_int(json, RT_NULL, "bytes", &bytes)) bytes = rt_strlen(payload);
    if (seq < 0 || bytes < 0) return -RT_EINVAL;

    rt_memset(&g_vb_flow, 0, sizeof(g_vb_flow));
    item = &g_vb_flow.items[0];
    item->sequence = (uint32_t)seq;
    item->bytes = (uint32_t)bytes;
    item->tick = rt_tick_get();
    vb_safe_copy(item->channel, sizeof(item->channel), channel);
    vb_safe_copy(item->payload, sizeof(item->payload), payload);
    g_vb_flow.write_index = 1 % VB_FLOW_HISTORY;
    g_vb_flow.count = 1;
    g_vb_flow.total_count = 1;
    rt_kprintf("[vb_runtime][flow] restored seq=%lu channel=%s bytes=%lu\n",
               (unsigned long)item->sequence,
               item->channel,
               (unsigned long)item->bytes);
    return RT_EOK;
}

static int vb_runtime_flow_send_hex(const char *channel, uint32_t sequence, const char *hex)
{
    char payload[VB_FLOW_MAX_PAYLOAD + 1];
    uint32_t bytes = 0;
    int result;
    int slot;
    vb_info_flow_item_t *item;

    if (!vb_is_safe_flow_channel(channel))
    {
        rt_kprintf("[vb_runtime][flow] invalid channel\n");
        return -RT_EINVAL;
    }
    result = vb_decode_hex_text(hex, payload, sizeof(payload), &bytes);
    if (result != RT_EOK)
    {
        rt_kprintf("[vb_runtime][flow] invalid payload hex rc=%d\n", result);
        return result;
    }

    slot = g_vb_flow.write_index;
    item = &g_vb_flow.items[slot];
    rt_memset(item, 0, sizeof(*item));
    item->sequence = sequence;
    item->bytes = bytes;
    item->tick = rt_tick_get();
    vb_safe_copy(item->channel, sizeof(item->channel), channel);
    vb_safe_copy(item->payload, sizeof(item->payload), payload);

    g_vb_flow.write_index = (slot + 1) % VB_FLOW_HISTORY;
    if (g_vb_flow.count < VB_FLOW_HISTORY) g_vb_flow.count++;
    g_vb_flow.total_count++;

    rt_kprintf("[vb_runtime][flow] recv total=%lu seq=%lu channel=%s bytes=%lu text=%s\n",
               (unsigned long)g_vb_flow.total_count,
               (unsigned long)item->sequence,
               item->channel,
               (unsigned long)item->bytes,
               item->payload);
    vb_runtime_flow_save_latest();
    return (int)bytes;
}

static void vb_runtime_flow_clear_state(void)
{
    rt_memset(&g_vb_flow, 0, sizeof(g_vb_flow));
    if (vb_prepare_filesystem() == RT_EOK) unlink(VIBEBOARD_FLOW_STATE_FILE);
    rt_kprintf("[vb_runtime][flow] cleared\n");
}

static int vb_runtime_flow_status_command(void)
{
    int i;
    int start;
    rt_kprintf("[vb_runtime][flow] api=%s\n", VIBEBOARD_RUNTIME_FLOW_API_VERSION);
    rt_kprintf("[vb_runtime][flow] total=%lu retained=%d capacity=%d\n",
               (unsigned long)g_vb_flow.total_count, g_vb_flow.count, VB_FLOW_HISTORY);
    if (g_vb_flow.count <= 0) return RT_EOK;
    start = (g_vb_flow.write_index + VB_FLOW_HISTORY - g_vb_flow.count) % VB_FLOW_HISTORY;
    for (i = 0; i < g_vb_flow.count; i++)
    {
        int index = (start + i) % VB_FLOW_HISTORY;
        vb_info_flow_item_t *item = &g_vb_flow.items[index];
        rt_kprintf("[vb_runtime][flow] item=%d seq=%lu channel=%s bytes=%lu tick=%lu payload=%s\n",
                   i,
                   (unsigned long)item->sequence,
                   item->channel,
                   (unsigned long)item->bytes,
                   (unsigned long)item->tick,
                   item->payload);
    }
    return RT_EOK;
}

static const char *vb_json_find_value(const char *begin, const char *end, const char *key)
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
        return hit;
    }
    return RT_NULL;
}

static const char *vb_json_find_string(const char *begin, const char *end, const char *key)
{
    const char *hit = vb_json_find_value(begin, end, key);
    if (hit && *hit == '"') return hit + 1;
    return RT_NULL;
}

static int vb_json_read_int(const char *begin, const char *end, const char *key, int *out)
{
    char *tail;
    const char *src = vb_json_find_value(begin, end, key);
    long value;
    if (!src || !out) return 0;
    value = strtol(src, &tail, 10);
    if (tail == src) return 0;
    *out = (int)value;
    return 1;
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

static void vb_json_copy_string_array_csv(const char *begin, const char *end, const char *key,
                                          char *dst, rt_size_t cap, const char *fallback)
{
    const char *value;
    const char *cursor;
    const char *array_end;
    rt_size_t used = 0;
    int first = 1;
    if (!dst || cap == 0) return;
    vb_safe_copy(dst, cap, fallback ? fallback : "");
    if (!begin || !key) return;
    value = vb_json_find_value(begin, end, key);
    if (!value || *value != '[') return;
    cursor = value + 1;
    array_end = strchr(cursor, ']');
    if (!array_end || (end && array_end > end)) return;
    dst[0] = '\0';
    while (cursor < array_end && used + 1 < cap)
    {
        const char *src;
        while (cursor < array_end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',')) cursor++;
        if (cursor >= array_end) break;
        if (*cursor != '"') break;
        cursor++;
        if (!first)
        {
            if (used + 2 >= cap) break;
            dst[used++] = ',';
            dst[used++] = ' ';
        }
        first = 0;
        src = cursor;
        while (src < array_end && *src && *src != '"' && used + 1 < cap)
        {
            char c;
            if (*src == '\\' && src[1]) src++;
            c = *src++;
            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            dst[used++] = c;
        }
        dst[used] = '\0';
        cursor = src;
        if (cursor < array_end && *cursor == '"') cursor++;
    }
    if (used == 0) vb_safe_copy(dst, cap, fallback ? fallback : "");
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


static uint32_t vb_2048_next_random(void)
{
    uint32_t value = g_vb_runtime.game2048.rng;
    if (value == 0) value = rt_tick_get() ^ 0x9e3779b9u;
    value = value * 1664525u + 1013904223u;
    g_vb_runtime.game2048.rng = value;
    return value;
}

static uint32_t vb_2048_tile_color(int value)
{
    switch (value)
    {
    case 0: return 0x263244;
    case 2: return 0xeee4da;
    case 4: return 0xede0c8;
    case 8: return 0xf2b179;
    case 16: return 0xf59563;
    case 32: return 0xf67c5f;
    case 64: return 0xf65e3b;
    case 128: return 0xedcf72;
    case 256: return 0xedcc61;
    case 512: return 0xedc850;
    case 1024: return 0xedc53f;
    case 2048: return 0xedc22e;
    default: return 0x3b82f6;
    }
}

static uint32_t vb_2048_text_color(int value)
{
    return value <= 4 ? 0x334155 : 0xf8fafc;
}

static int vb_2048_can_move(void)
{
    int r;
    int c;
    for (r = 0; r < VB_2048_SIZE; r++)
    {
        for (c = 0; c < VB_2048_SIZE; c++)
        {
            int value = g_vb_runtime.game2048.board[r][c];
            if (value == 0) return 1;
            if (c + 1 < VB_2048_SIZE && value == g_vb_runtime.game2048.board[r][c + 1]) return 1;
            if (r + 1 < VB_2048_SIZE && value == g_vb_runtime.game2048.board[r + 1][c]) return 1;
        }
    }
    return 0;
}

static void vb_2048_add_tile(void)
{
    int empty[VB_2048_CELLS];
    int count = 0;
    int r;
    int c;
    int slot;
    uint32_t rnd;
    for (r = 0; r < VB_2048_SIZE; r++)
    {
        for (c = 0; c < VB_2048_SIZE; c++)
        {
            if (g_vb_runtime.game2048.board[r][c] == 0) empty[count++] = r * VB_2048_SIZE + c;
        }
    }
    if (count <= 0) return;
    rnd = vb_2048_next_random();
    slot = empty[rnd % count];
    g_vb_runtime.game2048.board[slot / VB_2048_SIZE][slot % VB_2048_SIZE] = ((rnd >> 8) % 10 == 0) ? 4 : 2;
}

static int vb_2048_merge_line(int in[VB_2048_SIZE], int out[VB_2048_SIZE])
{
    int temp[VB_2048_SIZE] = {0, 0, 0, 0};
    int compact[VB_2048_SIZE] = {0, 0, 0, 0};
    int i;
    int count = 0;
    int out_count = 0;
    int changed = 0;
    for (i = 0; i < VB_2048_SIZE; i++)
    {
        if (in[i] != 0) temp[count++] = in[i];
    }
    for (i = 0; i < count; i++)
    {
        if (i + 1 < count && temp[i] == temp[i + 1])
        {
            compact[out_count] = temp[i] * 2;
            g_vb_runtime.game2048.score += compact[out_count];
            if (g_vb_runtime.game2048.score > g_vb_runtime.game2048.best_score)
            {
                g_vb_runtime.game2048.best_score = g_vb_runtime.game2048.score;
            }
            out_count++;
            i++;
        }
        else
        {
            compact[out_count++] = temp[i];
        }
    }
    for (i = 0; i < VB_2048_SIZE; i++)
    {
        out[i] = compact[i];
        if (out[i] != in[i]) changed = 1;
    }
    return changed;
}

static int vb_2048_move(lv_dir_t dir)
{
    int changed = 0;
    int i;
    int j;
    int in[VB_2048_SIZE];
    int out[VB_2048_SIZE];
    if (!g_vb_runtime.game2048.active || g_vb_runtime.game2048.game_over) return 0;
    for (i = 0; i < VB_2048_SIZE; i++)
    {
        for (j = 0; j < VB_2048_SIZE; j++)
        {
            if (dir == LV_DIR_LEFT) in[j] = g_vb_runtime.game2048.board[i][j];
            else if (dir == LV_DIR_RIGHT) in[j] = g_vb_runtime.game2048.board[i][VB_2048_SIZE - 1 - j];
            else if (dir == LV_DIR_TOP) in[j] = g_vb_runtime.game2048.board[j][i];
            else if (dir == LV_DIR_BOTTOM) in[j] = g_vb_runtime.game2048.board[VB_2048_SIZE - 1 - j][i];
            else return 0;
        }
        if (vb_2048_merge_line(in, out)) changed = 1;
        for (j = 0; j < VB_2048_SIZE; j++)
        {
            if (dir == LV_DIR_LEFT) g_vb_runtime.game2048.board[i][j] = out[j];
            else if (dir == LV_DIR_RIGHT) g_vb_runtime.game2048.board[i][VB_2048_SIZE - 1 - j] = out[j];
            else if (dir == LV_DIR_TOP) g_vb_runtime.game2048.board[j][i] = out[j];
            else if (dir == LV_DIR_BOTTOM) g_vb_runtime.game2048.board[VB_2048_SIZE - 1 - j][i] = out[j];
        }
    }
    if (changed)
    {
        g_vb_runtime.game2048.moves++;
        vb_2048_add_tile();
        if (!vb_2048_can_move()) g_vb_runtime.game2048.game_over = 1;
    }
    return changed;
}

static void vb_2048_render(void)
{
    int r;
    int c;
    int tile_size = (VB_2048_BOARD_SIZE - (VB_2048_SIZE + 1) * VB_2048_GAP) / VB_2048_SIZE;
    char text[64];
    if (!g_vb_runtime.game2048.active) return;
    for (r = 0; r < VB_2048_SIZE; r++)
    {
        for (c = 0; c < VB_2048_SIZE; c++)
        {
            int idx = r * VB_2048_SIZE + c;
            int value = g_vb_runtime.game2048.board[r][c];
            lv_obj_t *tile = g_vb_runtime.game2048.tiles[idx];
            lv_obj_t *label = g_vb_runtime.game2048.labels[idx];
            if (!tile || !label) continue;
            lv_obj_set_style_bg_color(tile, lv_color_hex(vb_2048_tile_color(value)), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (value > 0) rt_snprintf(text, sizeof(text), "%d", value);
            else text[0] = '\0';
            lv_label_set_text(label, text);
            lv_obj_set_style_text_color(label, lv_color_hex(vb_2048_text_color(value)), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_size(tile, tile_size, tile_size);
        }
    }
    if (g_vb_runtime.game2048.score_label)
    {
        rt_snprintf(text, sizeof(text), "Score %d   Best %d", g_vb_runtime.game2048.score, g_vb_runtime.game2048.best_score);
        lv_label_set_text(g_vb_runtime.game2048.score_label, text);
    }
    if (g_vb_runtime.game2048.status_label)
    {
        rt_snprintf(text, sizeof(text), g_vb_runtime.game2048.game_over ? "Game over  press K1 home" : "Swipe board  K1 home");
        lv_label_set_text(g_vb_runtime.game2048.status_label, text);
    }
}

static const char *vb_2048_dir_name(lv_dir_t dir)
{
    switch (dir)
    {
    case LV_DIR_LEFT: return "left";
    case LV_DIR_RIGHT: return "right";
    case LV_DIR_TOP: return "up";
    case LV_DIR_BOTTOM: return "down";
    default: return "none";
    }
}

static lv_dir_t vb_2048_dir_from_delta(int dx, int dy)
{
    int adx = abs(dx);
    int ady = abs(dy);
    if (adx < VB_2048_SWIPE_MIN_PRIMARY && ady < VB_2048_SWIPE_MIN_PRIMARY) return LV_DIR_NONE;
    if (adx >= ady) return dx >= 0 ? LV_DIR_RIGHT : LV_DIR_LEFT;
    return dy >= 0 ? LV_DIR_BOTTOM : LV_DIR_TOP;
}

static void vb_2048_handle_swipe(lv_dir_t dir, const char *source)
{
    char text[64];
    if (!g_vb_runtime.game2048.active) return;
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT && dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM) return;
    if (vb_2048_move(dir))
    {
        vb_2048_render();
        if (g_vb_runtime.game2048.status_label)
        {
            rt_snprintf(text, sizeof(text), "Moved %s", vb_2048_dir_name(dir));
            lv_label_set_text(g_vb_runtime.game2048.status_label, text);
        }
        rt_kprintf("[vb_runtime][2048] %s dir=%s move=%d score=%d best=%d dx=%d dy=%d\n",
                   source ? source : "swipe",
                   vb_2048_dir_name(dir),
                   g_vb_runtime.game2048.moves,
                   g_vb_runtime.game2048.score,
                   g_vb_runtime.game2048.best_score,
                   g_vb_runtime.touch.delta.x,
                   g_vb_runtime.touch.delta.y);
    }
    else
    {
        if (g_vb_runtime.game2048.status_label)
        {
            rt_snprintf(text, sizeof(text), "No move %s", vb_2048_dir_name(dir));
            lv_label_set_text(g_vb_runtime.game2048.status_label, text);
        }
        rt_kprintf("[vb_runtime][2048] %s ignored dir=%s dx=%d dy=%d\n",
                   source ? source : "swipe",
                   vb_2048_dir_name(dir),
                   g_vb_runtime.touch.delta.x,
                   g_vb_runtime.touch.delta.y);
    }
}

static void vb_2048_handle_gesture(lv_dir_t dir)
{
    vb_2048_handle_swipe(dir, "gesture");
}

static void vb_2048_reset(void)
{
    rt_memset(g_vb_runtime.game2048.board, 0, sizeof(g_vb_runtime.game2048.board));
    g_vb_runtime.game2048.score = 0;
    g_vb_runtime.game2048.moves = 0;
    g_vb_runtime.game2048.game_over = 0;
    g_vb_runtime.game2048.rng = rt_tick_get() ^ 0x20482048u;
    vb_2048_add_tile();
    vb_2048_add_tile();
}

static void vb_runtime_bind_touch_events(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(obj, vb_runtime_touch_event_cb, LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(obj, vb_runtime_touch_event_cb, LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(obj, vb_runtime_touch_event_cb, LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(obj, vb_runtime_touch_event_cb, LV_EVENT_PRESS_LOST, RT_NULL);
    lv_obj_add_event_cb(obj, vb_runtime_touch_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(obj, vb_runtime_touch_event_cb, LV_EVENT_GESTURE, RT_NULL);
}

static int vb_2048_start(const char *title)
{
    int r;
    int c;
    int tile_size = (VB_2048_BOARD_SIZE - (VB_2048_SIZE + 1) * VB_2048_GAP) / VB_2048_SIZE;
    int board_x = (LV_HOR_RES_MAX - VB_2048_BOARD_SIZE) / 2;
    int board_y = VB_SCREEN_SAFE_TOP + 92;
    lv_obj_t *label;
    if (!g_vb_runtime.root) return -RT_ERROR;
    rt_memset(&g_vb_runtime.game2048, 0, sizeof(g_vb_runtime.game2048));
    g_vb_runtime.game2048.active = 1;

    label = vb_create_label(g_vb_runtime.root, title && title[0] ? title : "2048", FONT_BIGL, lv_color_hex(0xfbbf24));
    lv_obj_set_width(label, VB_SCREEN_SAFE_WIDTH);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, VB_SCREEN_SAFE_TOP - 4);

    g_vb_runtime.game2048.score_label = vb_create_label(g_vb_runtime.root, "Score 0   Best 0", FONT_SMALL, lv_color_hex(0xe2e8f0));
    lv_obj_set_width(g_vb_runtime.game2048.score_label, VB_SCREEN_SAFE_WIDTH);
    lv_obj_set_style_text_align(g_vb_runtime.game2048.score_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.game2048.score_label, LV_ALIGN_TOP_MID, 0, VB_SCREEN_SAFE_TOP + 38);

    g_vb_runtime.game2048.board_panel = lv_obj_create(g_vb_runtime.root);
    lv_obj_set_size(g_vb_runtime.game2048.board_panel, VB_2048_BOARD_SIZE, VB_2048_BOARD_SIZE);
    lv_obj_set_pos(g_vb_runtime.game2048.board_panel, board_x, board_y);
    lv_obj_set_style_bg_color(g_vb_runtime.game2048.board_panel, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_vb_runtime.game2048.board_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_vb_runtime.game2048.board_panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(g_vb_runtime.game2048.board_panel, lv_color_hex(0x475569), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_vb_runtime.game2048.board_panel, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    vb_runtime_bind_touch_events(g_vb_runtime.game2048.board_panel);
    lv_obj_set_ext_click_area(g_vb_runtime.game2048.board_panel, 12);

    for (r = 0; r < VB_2048_SIZE; r++)
    {
        for (c = 0; c < VB_2048_SIZE; c++)
        {
            int idx = r * VB_2048_SIZE + c;
            lv_obj_t *tile = lv_obj_create(g_vb_runtime.game2048.board_panel);
            lv_obj_set_size(tile, tile_size, tile_size);
            lv_obj_set_pos(tile,
                           VB_2048_GAP + c * (tile_size + VB_2048_GAP),
                           VB_2048_GAP + r * (tile_size + VB_2048_GAP));
            lv_obj_set_style_radius(tile, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(tile, lv_color_hex(0x263244), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
            label = vb_create_label(tile, "", FONT_NORMAL, lv_color_hex(0xf8fafc));
            lv_obj_set_width(label, tile_size);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_center(label);
            g_vb_runtime.game2048.tiles[idx] = tile;
            g_vb_runtime.game2048.labels[idx] = label;
        }
    }

    g_vb_runtime.game2048.status_label = vb_create_label(g_vb_runtime.root, "Swipe board  K1 home", FONT_SMALL, lv_color_hex(0xbfdbfe));
    lv_obj_set_width(g_vb_runtime.game2048.status_label, VB_SCREEN_SAFE_WIDTH);
    lv_obj_set_style_text_align(g_vb_runtime.game2048.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.game2048.status_label, LV_ALIGN_BOTTOM_MID, 0, -VB_SCREEN_SAFE_BOTTOM + 8);

    vb_2048_reset();
    vb_2048_render();
    rt_kprintf("[vb_runtime][2048] started\n");
    return RT_EOK;
}

static lv_obj_t *vb_weather_blob(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *vb_weather_layer(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *vb_weather_sticker_blob(lv_obj_t *parent, int x, int y, int w, int h,
                                         uint32_t color, int radius, uint32_t border, int border_w)
{
    lv_obj_t *obj = vb_weather_blob(parent, x, y, w, h, color, radius);
    if (border_w > 0)
    {
        lv_obj_set_style_border_width(obj, border_w, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(obj, lv_color_hex(border), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    return obj;
}

static void vb_weather_soft_shadow(lv_obj_t *obj, uint32_t color, int width, int ofs_y, lv_opa_t opa)
{
    if (!obj) return;
    lv_obj_set_style_shadow_width(obj, width, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(obj, ofs_y, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static const lv_point_t vb_weather_smile_points[] = {
    {0, 0}, {10, 8}, {24, 9}, {36, 0}
};

static const lv_point_t vb_weather_bolt_points[] = {
    {34, 0}, {12, 44}, {34, 44}, {22, 92}, {70, 30}, {46, 30}, {58, 0}
};

static const lv_point_t vb_weather_snow_h[] = {
    {0, 8}, {18, 8}
};

static const lv_point_t vb_weather_snow_v[] = {
    {9, 0}, {9, 18}
};

static const lv_point_t vb_weather_snow_d1[] = {
    {2, 2}, {16, 16}
};

static const lv_point_t vb_weather_snow_d2[] = {
    {16, 2}, {2, 16}
};

static lv_obj_t *vb_weather_line(lv_obj_t *parent, const lv_point_t *points, uint16_t point_count,
                                 int x, int y, uint32_t color, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, point_count);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_color(line, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(line, width, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(line, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

static vb_weather_kind_t vb_weather_kind_from_condition(const char *condition, int code)
{
    if (condition)
    {
        if (rt_strcmp(condition, "sunny") == 0 || rt_strcmp(condition, "clear") == 0) return VB_WEATHER_SUNNY;
        if (rt_strcmp(condition, "cloudy") == 0 || rt_strcmp(condition, "overcast") == 0) return VB_WEATHER_CLOUDY;
        if (rt_strcmp(condition, "rain") == 0 || rt_strcmp(condition, "drizzle") == 0) return VB_WEATHER_RAIN;
        if (rt_strcmp(condition, "snow") == 0) return VB_WEATHER_SNOW;
        if (rt_strcmp(condition, "storm") == 0 || rt_strcmp(condition, "thunder") == 0) return VB_WEATHER_STORM;
        if (rt_strcmp(condition, "fog") == 0 || rt_strcmp(condition, "mist") == 0) return VB_WEATHER_FOG;
    }
    if (code == 0 || code == 1) return VB_WEATHER_SUNNY;
    if (code == 2 || code == 3) return VB_WEATHER_CLOUDY;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return VB_WEATHER_RAIN;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return VB_WEATHER_SNOW;
    if (code >= 95) return VB_WEATHER_STORM;
    if (code == 45 || code == 48) return VB_WEATHER_FOG;
    return VB_WEATHER_CLOUDY;
}

static const char *vb_weather_kind_label(vb_weather_kind_t kind)
{
    switch (kind)
    {
    case VB_WEATHER_SUNNY: return "Sunny";
    case VB_WEATHER_CLOUDY: return "Cloudy";
    case VB_WEATHER_RAIN: return "Rain";
    case VB_WEATHER_SNOW: return "Snow";
    case VB_WEATHER_STORM: return "Storm";
    case VB_WEATHER_FOG: return "Fog";
    default: return "Weather";
    }
}

static const char *vb_weather_kind_asset(vb_weather_kind_t kind)
{
    switch (kind)
    {
    case VB_WEATHER_SUNNY: return "sunny";
    case VB_WEATHER_CLOUDY: return "cloudy";
    case VB_WEATHER_RAIN: return "rain";
    case VB_WEATHER_SNOW: return "snow";
    case VB_WEATHER_STORM: return "storm";
    case VB_WEATHER_FOG: return "fog";
    default: return "cloudy";
    }
}

static void vb_script_to_lvgl_image_path(const char *fs_path, char *dst, rt_size_t cap)
{
    if (!dst || cap == 0) return;
    if (!fs_path || !fs_path[0])
    {
        dst[0] = '\0';
        return;
    }
    if (fs_path[0] == '/')
    {
        rt_snprintf(dst, cap, "/%s", fs_path);
        dst[cap - 1] = '\0';
        return;
    }
    vb_safe_copy(dst, cap, fs_path);
}

static int vb_weather_build_asset_path(vb_weather_kind_t kind, char *fs_dst, rt_size_t fs_cap,
                                       char *lvgl_dst, rt_size_t lvgl_cap)
{
    if (!fs_dst || fs_cap == 0) return -RT_EINVAL;
    rt_snprintf(fs_dst, fs_cap, "%s/%s/assets/weather/%s.png",
                VIBEBOARD_APP_ROOT,
                g_vb_runtime.active_app,
                vb_weather_kind_asset(kind));
    fs_dst[fs_cap - 1] = '\0';
    if (lvgl_dst && lvgl_cap > 0) vb_script_to_lvgl_image_path(fs_dst, lvgl_dst, lvgl_cap);
    return vb_file_exists(fs_dst) ? RT_EOK : -RT_ERROR;
}

static void vb_weather_release_image(void)
{
    if (g_vb_runtime.weather.image_dsc)
    {
        app_cache_img_free(g_vb_runtime.weather.image_dsc);
        g_vb_runtime.weather.image_dsc = RT_NULL;
    }
    g_vb_runtime.weather.image_raw = 0;
}

static void vb_weather_init_image_decoders(void)
{
    static uint8_t initialized = 0;
    if (initialized) return;
    initialized = 1;
#if defined(LV_USE_PNG) && LV_USE_PNG
    lv_png_init();
    rt_kprintf("[vb_runtime][weather] png decoder initialized\n");
#endif
}

static int vb_weather_build_bin_asset_path(vb_weather_kind_t kind, char *fs_dst, rt_size_t fs_cap)
{
    if (!fs_dst || fs_cap == 0) return -RT_EINVAL;
    rt_snprintf(fs_dst, fs_cap, "%s/%s/assets/weather/%s.bin",
                VIBEBOARD_APP_ROOT,
                g_vb_runtime.active_app,
                vb_weather_kind_asset(kind));
    fs_dst[fs_cap - 1] = '\0';
    return vb_file_exists(fs_dst) ? RT_EOK : -RT_ERROR;
}

static uint16_t vb_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t vb_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int vb_read_full(int fd, void *dst, int bytes)
{
    uint8_t *out = (uint8_t *)dst;
    int total = 0;
    while (total < bytes)
    {
        int n = read(fd, out + total, bytes - total);
        if (n <= 0) return total > 0 ? total : n;
        total += n;
    }
    return total;
}

static int vb_weather_load_bin_image(vb_weather_kind_t kind, const char *path)
{
    uint8_t header[16];
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t cf;
    uint32_t data_size;
    struct stat st;
    lv_img_dsc_t *dsc = RT_NULL;
    int fd;
    int read_bytes;

    if (!path || !path[0]) return -RT_EINVAL;
    if (stat(path, &st) != 0)
    {
        rt_kprintf("[vb_runtime][weather] bin stat failed: %s\n", path);
        return -RT_ERROR;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        rt_kprintf("[vb_runtime][weather] bin open failed: %s\n", path);
        return -RT_ERROR;
    }

    read_bytes = vb_read_full(fd, header, sizeof(header));
    if (read_bytes != (int)sizeof(header))
    {
        close(fd);
        rt_kprintf("[vb_runtime][weather] bin header read failed: %s bytes=%d\n", path, read_bytes);
        return -RT_ERROR;
    }

    magic = vb_read_le32(&header[0]);
    version = vb_read_le16(&header[4]);
    width = vb_read_le16(&header[6]);
    height = vb_read_le16(&header[8]);
    cf = vb_read_le16(&header[10]);
    data_size = vb_read_le32(&header[12]);

    if (magic != VB_WEATHER_IMG_MAGIC || version != VB_WEATHER_IMG_VERSION ||
        width == 0 || height == 0 || width > 240 || height > 240 ||
        cf != LV_IMG_CF_TRUE_COLOR_ALPHA ||
        data_size != (uint32_t)(width * height * LV_IMG_PX_SIZE_ALPHA_BYTE) ||
        st.st_size != (off_t)(sizeof(header) + data_size))
    {
        close(fd);
        rt_kprintf("[vb_runtime][weather] bin invalid path=%s magic=%08x ver=%u w=%u h=%u cf=%u data=%lu size=%ld\n",
                   path,
                   (unsigned int)magic,
                   (unsigned int)version,
                   (unsigned int)width,
                   (unsigned int)height,
                   (unsigned int)cf,
                   (unsigned long)data_size,
                   (long)st.st_size);
        return -RT_ERROR;
    }

    dsc = app_cache_img_alloc(width, height, LV_IMG_CF_TRUE_COLOR_ALPHA, data_size, IMAGE_CACHE_PSRAM);
    if (!dsc)
    {
        close(fd);
        rt_kprintf("[vb_runtime][weather] bin image alloc failed path=%s bytes=%lu\n", path, (unsigned long)data_size);
        return -RT_ENOMEM;
    }

    read_bytes = vb_read_full(fd, (void *)dsc->data, (int)data_size);
    close(fd);
    if (read_bytes != (int)data_size)
    {
        app_cache_img_free(dsc);
        rt_kprintf("[vb_runtime][weather] bin data read failed path=%s bytes=%d expected=%lu\n",
                   path,
                   read_bytes,
                   (unsigned long)data_size);
        return -RT_ERROR;
    }

    vb_weather_release_image();
    g_vb_runtime.weather.image_dsc = dsc;
    rt_kprintf("[vb_runtime][weather] bin image loaded path=%s kind=%s w=%u h=%u bytes=%lu\n",
               path,
               vb_weather_kind_asset(kind),
               (unsigned int)width,
               (unsigned int)height,
               (unsigned long)data_size);
    return RT_EOK;
}

static int vb_weather_create_image_pet(vb_weather_kind_t kind)
{
    char bin_path[VB_MAX_PATH];
    char fs_path[VB_MAX_PATH];
    char image_path[VB_MAX_PATH + 2];
    lv_img_header_t header;
    lv_img_src_t src_type;
    lv_res_t info_res;
    lv_obj_t *image;

    if (vb_weather_build_bin_asset_path(kind, bin_path, sizeof(bin_path)) == RT_EOK &&
        vb_weather_load_bin_image(kind, bin_path) == RT_EOK)
    {
        image = lv_img_create(g_vb_runtime.root);
        lv_img_set_src(image, g_vb_runtime.weather.image_dsc);
        lv_img_set_pivot(image,
                         g_vb_runtime.weather.image_dsc->header.w / 2,
                         g_vb_runtime.weather.image_dsc->header.h / 2);
        lv_img_set_zoom(image, 256);
        lv_img_set_antialias(image, true);
        lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 106);
        g_vb_runtime.weather.image = image;
        g_vb_runtime.weather.image_raw = 1;
        rt_kprintf("[vb_runtime][weather] image asset=%s mode=bin\n", bin_path);
        return RT_EOK;
    }

    if (vb_weather_build_asset_path(kind, fs_path, sizeof(fs_path), image_path, sizeof(image_path)) != RT_EOK)
    {
        rt_kprintf("[vb_runtime][weather] image asset missing for %s\n", vb_weather_kind_asset(kind));
        return -RT_ERROR;
    }

    vb_weather_init_image_decoders();
    memset(&header, 0, sizeof(header));
    src_type = lv_img_src_get_type(image_path);
    info_res = lv_img_decoder_get_info(image_path, &header);
    if (info_res != LV_RES_OK || header.w == 0 || header.h == 0)
    {
        rt_kprintf("[vb_runtime][weather] image decode failed fs=%s lvgl=%s type=%d res=%d w=%u h=%u cf=%u\n",
                   fs_path,
                   image_path,
                   (int)src_type,
                   (int)info_res,
                   (unsigned int)header.w,
                   (unsigned int)header.h,
                   (unsigned int)header.cf);
        return -RT_ERROR;
    }

    image = lv_img_create(g_vb_runtime.root);
    lv_img_set_src(image, image_path);
    lv_img_set_pivot(image, header.w / 2, header.h / 2);
    lv_img_set_zoom(image, 256);
    lv_img_set_antialias(image, true);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 106);
    g_vb_runtime.weather.image = image;
    g_vb_runtime.weather.image_raw = 0;
    rt_kprintf("[vb_runtime][weather] image asset=%s lvgl=%s type=%d w=%u h=%u cf=%u\n",
               fs_path,
               image_path,
               (int)src_type,
               (unsigned int)header.w,
               (unsigned int)header.h,
               (unsigned int)header.cf);
    return RT_EOK;
}

static void vb_weather_create_cloud(vb_weather_kind_t kind)
{
    lv_obj_t *pet = g_vb_runtime.weather.pet;
    uint32_t base = kind == VB_WEATHER_STORM ? 0x8aa0b5 : 0xf8fafc;
    uint32_t mid = kind == VB_WEATHER_STORM ? 0x64748b : 0xe2e8f0;
    uint32_t hi = kind == VB_WEATHER_STORM ? 0xcbd5e1 : 0xffffff;
    uint32_t shade = kind == VB_WEATHER_STORM ? 0x475569 : 0xcbd5e1;
    uint32_t outline = kind == VB_WEATHER_STORM ? 0x1e293b : 0xeff6ff;

    if (!pet) return;
    g_vb_runtime.weather.cloud[0] = vb_weather_sticker_blob(pet, 40, 92, 210, 66, outline, 33, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[1] = vb_weather_sticker_blob(pet, 66, 58, 88, 88, outline, 44, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[2] = vb_weather_sticker_blob(pet, 126, 38, 106, 106, outline, 53, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[3] = vb_weather_sticker_blob(pet, 202, 78, 78, 78, outline, 39, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[4] = vb_weather_sticker_blob(pet, 50, 98, 188, 52, base, 26, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[5] = vb_weather_sticker_blob(pet, 76, 66, 72, 72, mid, 36, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[6] = vb_weather_sticker_blob(pet, 134, 48, 88, 88, hi, 44, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[7] = vb_weather_sticker_blob(pet, 204, 86, 62, 62, base, 31, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[8] = vb_weather_sticker_blob(pet, 74, 132, 150, 14, shade, 7, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[9] = vb_weather_sticker_blob(pet, 110, 70, 50, 16, 0xffffff, 8, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[10] = vb_weather_sticker_blob(pet, 166, 58, 42, 14, 0xffffff, 7, 0x0f172a, 0);
    g_vb_runtime.weather.cloud[11] = vb_weather_sticker_blob(pet, 232, 112, 18, 18, 0xffffff, 9, 0x0f172a, 0);
    vb_weather_soft_shadow(g_vb_runtime.weather.cloud[0], 0x020617, 18, 10, LV_OPA_30);
}

static void vb_weather_create_sun_pet(void)
{
    lv_obj_t *pet = g_vb_runtime.weather.pet;
    int i;
    if (!pet) return;
    for (i = 0; i < VB_WEATHER_RAY_COUNT; i++)
    {
        int horiz = (i == 0 || i == 4);
        int diag = (i % 2) != 0;
        int w = horiz ? 46 : (diag ? 16 : 14);
        int h = horiz ? 14 : (diag ? 40 : 48);
        int x = i == 0 ? 236 : i == 4 ? 18 : i == 1 ? 210 : i == 2 ? 136 : i == 3 ? 52 : i == 5 ? 54 : i == 6 ? 136 : 210;
        int y = i == 0 ? 106 : i == 4 ? 106 : i == 1 ? 38 : i == 2 ? 12 : i == 3 ? 38 : i == 5 ? 176 : i == 6 ? 198 : 176;
        g_vb_runtime.weather.rays[i] = vb_weather_sticker_blob(pet, x, y, w, h, 0xffd166, h / 2, 0x0f172a, 0);
        if (diag)
        {
            lv_obj_set_style_transform_pivot_x(g_vb_runtime.weather.rays[i], w / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_pivot_y(g_vb_runtime.weather.rays[i], h / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_angle(g_vb_runtime.weather.rays[i], (i == 1 || i == 5) ? 450 : -450, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    g_vb_runtime.weather.sun = vb_weather_sticker_blob(pet, 74, 48, 154, 154, 0xfbbf24, 77, 0xfff7ad, 5);
    vb_weather_soft_shadow(g_vb_runtime.weather.sun, 0x78350f, 18, 8, LV_OPA_30);
    g_vb_runtime.weather.decor[0] = vb_weather_blob(pet, 112, 76, 76, 28, 0xffe08a, 14);
    g_vb_runtime.weather.decor[1] = vb_weather_blob(pet, 178, 120, 18, 18, 0xffe08a, 9);
}

static void vb_weather_create_drops(vb_weather_kind_t kind)
{
    int i;
    lv_obj_t *parent = g_vb_runtime.weather.pet ? g_vb_runtime.weather.pet : g_vb_runtime.root;
    for (i = 0; i < VB_WEATHER_DROP_COUNT; i++)
    {
        int x = 58 + i * 28;
        int y = 164 + (i % 3) * 20;
        uint32_t color = kind == VB_WEATHER_SNOW ? 0xf8fafc : 0x38bdf8;
        int size = kind == VB_WEATHER_SNOW ? 20 : 12;
        if (kind == VB_WEATHER_SNOW)
        {
            lv_obj_t *flake = vb_weather_layer(parent, x, y, 22, 22);
            vb_weather_line(flake, vb_weather_snow_h, 2, 2, 2, color, 3);
            vb_weather_line(flake, vb_weather_snow_v, 2, 2, 2, color, 3);
            vb_weather_line(flake, vb_weather_snow_d1, 2, 2, 2, color, 2);
            vb_weather_line(flake, vb_weather_snow_d2, 2, 2, 2, color, 2);
            g_vb_runtime.weather.drops[i] = flake;
        }
        else
        {
            g_vb_runtime.weather.drops[i] = vb_weather_sticker_blob(parent, x, y, size, 26, color, size / 2, 0xbae6fd, 2);
            lv_obj_set_style_transform_pivot_x(g_vb_runtime.weather.drops[i], size / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_pivot_y(g_vb_runtime.weather.drops[i], 13, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_angle(g_vb_runtime.weather.drops[i], 180, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

static void vb_weather_create_image_effects(vb_weather_kind_t kind)
{
    lv_obj_t *root = g_vb_runtime.root;
    if (!root) return;

    if (kind == VB_WEATHER_RAIN || kind == VB_WEATHER_SNOW || kind == VB_WEATHER_STORM)
    {
        vb_weather_create_drops(kind);
    }

    if (kind == VB_WEATHER_STORM)
    {
        g_vb_runtime.weather.bolt = vb_weather_line(root, vb_weather_bolt_points, 7, 154, 172, 0xfacc15, 7);
        vb_weather_soft_shadow(g_vb_runtime.weather.bolt, 0xfacc15, 12, 0, LV_OPA_50);
        lv_obj_add_flag(g_vb_runtime.weather.bolt, LV_OBJ_FLAG_HIDDEN);
    }
    else if (kind == VB_WEATHER_FOG)
    {
        int i;
        for (i = 0; i < VB_WEATHER_FOG_COUNT; i++)
        {
            g_vb_runtime.weather.fog[i] = vb_weather_sticker_blob(
                root, 86 + i * 18, 232 + i * 22, 220 - i * 36, 13,
                0xdbeafe, 7, 0xffffff, 1);
            lv_obj_set_style_bg_opa(g_vb_runtime.weather.fog[i], LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    else
    {
        g_vb_runtime.weather.decor[0] = vb_weather_blob(root, 82, 142, 9, 9, kind == VB_WEATHER_SUNNY ? 0xfff7ad : 0xe0f2fe, 5);
        g_vb_runtime.weather.decor[1] = vb_weather_blob(root, 292, 152, 7, 7, kind == VB_WEATHER_SUNNY ? 0xffd166 : 0xffffff, 4);
        g_vb_runtime.weather.decor[2] = vb_weather_blob(root, 96, 266, 6, 6, 0xffffff, 3);
        g_vb_runtime.weather.decor[3] = vb_weather_blob(root, 276, 250, 8, 8, kind == VB_WEATHER_CLOUDY ? 0xbfdbfe : 0xfff7ad, 4);
        g_vb_runtime.weather.decor[4] = vb_weather_blob(root, 118, 112, 5, 5, 0xffffff, 3);
        g_vb_runtime.weather.decor[5] = vb_weather_blob(root, 258, 122, 5, 5, 0xffffff, 3);
    }
}

static int vb_weather_start(const char *city, const char *condition, int temp_c, int humidity, int weather_code)
{
    char text[96];
    int i;
    vb_weather_kind_t kind = vb_weather_kind_from_condition(condition, weather_code);
    uint32_t bg = 0x0f172a;

    if (!g_vb_runtime.root) return -RT_ERROR;
    vb_weather_release_image();
    rt_memset(&g_vb_runtime.weather, 0, sizeof(g_vb_runtime.weather));
    g_vb_runtime.weather.active = 1;
    g_vb_runtime.weather.kind = kind;
    g_vb_runtime.weather.temp_c = temp_c;
    g_vb_runtime.weather.humidity = humidity;
    g_vb_runtime.weather.weather_code = weather_code;
    g_vb_runtime.weather.last_run = rt_tick_get();
    g_vb_runtime.weather.period_ticks = rt_tick_from_millisecond(VB_WEATHER_PERIOD_MS);
    if (g_vb_runtime.weather.period_ticks == 0) g_vb_runtime.weather.period_ticks = 1;
    vb_safe_copy(g_vb_runtime.weather.city, sizeof(g_vb_runtime.weather.city), city && city[0] ? city : "Current Location");
    vb_safe_copy(g_vb_runtime.weather.condition, sizeof(g_vb_runtime.weather.condition), condition && condition[0] ? condition : vb_weather_kind_label(kind));

    if (kind == VB_WEATHER_SUNNY) bg = 0x103749;
    else if (kind == VB_WEATHER_RAIN) bg = 0x111827;
    else if (kind == VB_WEATHER_SNOW) bg = 0x1e3a5f;
    else if (kind == VB_WEATHER_STORM) bg = 0x18181b;
    else if (kind == VB_WEATHER_FOG) bg = 0x334155;
    lv_obj_set_style_bg_color(g_vb_runtime.root, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);

    g_vb_runtime.weather.title_label = vb_create_label(g_vb_runtime.root, "Weather Pet", FONT_BIGL, lv_color_hex(0xf8fafc));
    lv_obj_set_width(g_vb_runtime.weather.title_label, 340);
    lv_obj_set_style_text_align(g_vb_runtime.weather.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.weather.title_label, LV_ALIGN_TOP_MID, 0, 28);

    rt_snprintf(text, sizeof(text), "%s  %dC  %s  RH %d%%",
                g_vb_runtime.weather.city, temp_c, vb_weather_kind_label(kind), humidity);
    g_vb_runtime.weather.summary_label = vb_create_label(g_vb_runtime.root, text, FONT_SMALL, lv_color_hex(0xcbd5e1));
    lv_obj_set_width(g_vb_runtime.weather.summary_label, 350);
    lv_obj_set_style_text_align(g_vb_runtime.weather.summary_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.weather.summary_label, LV_ALIGN_TOP_MID, 0, 70);

    if (vb_weather_create_image_pet(kind) == RT_EOK)
    {
        vb_weather_create_image_effects(kind);
        rt_kprintf("[vb_runtime][weather] started city=%s kind=%s temp=%d humidity=%d code=%d mode=%s\n",
                   g_vb_runtime.weather.city,
                   vb_weather_kind_label(kind),
                   temp_c,
                   humidity,
                   weather_code,
                   g_vb_runtime.weather.image_raw ? "bin" : "png");
        return RT_EOK;
    }

    g_vb_runtime.weather.pet = vb_weather_layer(g_vb_runtime.root, 44, 104, 300, 260);
    if (kind == VB_WEATHER_SUNNY)
    {
        vb_weather_create_sun_pet();
        g_vb_runtime.weather.left_eye = vb_weather_blob(g_vb_runtime.weather.pet, 116, 108, 16, 22, 0x0f172a, 8);
        g_vb_runtime.weather.right_eye = vb_weather_blob(g_vb_runtime.weather.pet, 168, 108, 16, 22, 0x0f172a, 8);
        g_vb_runtime.weather.mouth = vb_weather_line(g_vb_runtime.weather.pet, vb_weather_smile_points, 4, 132, 148, 0x0f172a, 5);
        g_vb_runtime.weather.blush_l = vb_weather_blob(g_vb_runtime.weather.pet, 86, 138, 28, 12, 0xfb7185, 6);
        g_vb_runtime.weather.blush_r = vb_weather_blob(g_vb_runtime.weather.pet, 188, 138, 28, 12, 0xfb7185, 6);
    }
    else
    {
        if (kind == VB_WEATHER_CLOUDY)
        {
            g_vb_runtime.weather.sun = vb_weather_sticker_blob(g_vb_runtime.weather.pet, 28, 36, 86, 86, 0xfbbf24, 43, 0xfff7ad, 4);
            vb_weather_soft_shadow(g_vb_runtime.weather.sun, 0x78350f, 12, 6, LV_OPA_20);
        }
        vb_weather_create_cloud(kind);
        g_vb_runtime.weather.left_eye = vb_weather_blob(g_vb_runtime.weather.pet, 120, 112, 15, 20, 0x0f172a, 8);
        g_vb_runtime.weather.right_eye = vb_weather_blob(g_vb_runtime.weather.pet, 172, 112, 15, 20, 0x0f172a, 8);
        g_vb_runtime.weather.mouth = vb_weather_line(g_vb_runtime.weather.pet, vb_weather_smile_points, 4, 136, 148, 0x0f172a, 5);
        g_vb_runtime.weather.blush_l = vb_weather_blob(g_vb_runtime.weather.pet, 92, 134, 28, 12, 0xfb7185, 6);
        g_vb_runtime.weather.blush_r = vb_weather_blob(g_vb_runtime.weather.pet, 190, 134, 28, 12, 0xfb7185, 6);
        if (kind == VB_WEATHER_RAIN || kind == VB_WEATHER_SNOW || kind == VB_WEATHER_STORM)
        {
            vb_weather_create_drops(kind);
        }
        if (kind == VB_WEATHER_STORM)
        {
            g_vb_runtime.weather.bolt = vb_weather_line(g_vb_runtime.weather.pet, vb_weather_bolt_points, 7, 120, 154, 0xfacc15, 9);
            vb_weather_soft_shadow(g_vb_runtime.weather.bolt, 0xfacc15, 12, 0, LV_OPA_50);
        }
        if (kind == VB_WEATHER_FOG)
        {
            for (i = 0; i < VB_WEATHER_FOG_COUNT; i++)
            {
                g_vb_runtime.weather.fog[i] = vb_weather_sticker_blob(
                    g_vb_runtime.weather.pet, 44 + i * 18, 170 + i * 24,
                    220 - i * 32, 14, 0xdbeafe, 7, 0xffffff, 1);
            }
        }
    }

    rt_kprintf("[vb_runtime][weather] started city=%s kind=%s temp=%d humidity=%d code=%d\n",
               g_vb_runtime.weather.city, vb_weather_kind_label(kind), temp_c, humidity, weather_code);
    return RT_EOK;
}

static void vb_weather_step(void)
{
    int i;
    int bob;
    int blink;
    int sway;
    int slow;
    int breathe;
    int angle;
    int zoom;
    vb_weather_state_t *w = &g_vb_runtime.weather;
    if (!w->active) return;
    w->frame++;
    bob = (w->frame % 12) < 6 ? (w->frame % 6) : (11 - (w->frame % 12));
    sway = (w->frame % 18) < 9 ? (w->frame % 9) : (17 - (w->frame % 18));
    slow = w->frame % 36;
    breathe = slow < 18 ? slow : 35 - slow;
    angle = (w->frame % 28) < 14 ? (w->frame % 14) - 7 : 7 - (w->frame % 14);
    zoom = 252 + (breathe / 3);
    blink = (w->frame % 24) == 0 || (w->frame % 24) == 1;

    if (w->image)
    {
        lv_obj_align(w->image, LV_ALIGN_TOP_MID, (sway / 3) - 1, 106 + bob);
        lv_img_set_zoom(w->image, (uint16_t)zoom);
        lv_img_set_angle(w->image, (int16_t)(angle * 4));
    }

    if (w->pet)
    {
        lv_obj_set_pos(w->pet, 44 + sway / 3, 104 + bob);
    }
    if (w->left_eye)
    {
        lv_obj_set_height(w->left_eye, blink ? 4 : 20);
    }
    if (w->right_eye)
    {
        lv_obj_set_height(w->right_eye, blink ? 4 : 20);
    }
    for (i = 0; i < VB_WEATHER_RAY_COUNT; i++)
    {
        if (w->rays[i])
        {
            lv_obj_set_style_bg_opa(w->rays[i], (w->frame + i) % 8 < 4 ? LV_OPA_COVER : LV_OPA_70,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    for (i = 0; i < VB_WEATHER_DROP_COUNT; i++)
    {
        if (w->drops[i])
        {
            int base_x = w->image ? 70 : 58;
            int base_y = w->image ? 154 : 164;
            int span_y = w->image ? 142 : 82;
            int x = base_x + i * 28;
            int y = base_y + ((w->frame * (w->kind == VB_WEATHER_SNOW ? 3 : 9) + i * 19) % span_y);
            if (w->kind == VB_WEATHER_SNOW)
            {
                x += ((w->frame + i * 2) % 15) - 7;
                lv_obj_set_style_transform_angle(w->drops[i], (w->frame * 18 + i * 140) % 3600,
                                                 LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            else
            {
                lv_obj_set_style_bg_opa(w->drops[i], ((w->frame + i) % 8) < 5 ? LV_OPA_80 : LV_OPA_50,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            lv_obj_set_pos(w->drops[i], x, y);
        }
    }
    if (w->bolt)
    {
        if ((w->frame % 13) == 0 || (w->frame % 13) == 1 || (w->frame % 29) == 2) lv_obj_clear_flag(w->bolt, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(w->bolt, LV_OBJ_FLAG_HIDDEN);
    }
    for (i = 0; i < VB_WEATHER_FOG_COUNT; i++)
    {
        if (w->fog[i])
        {
            int dx = ((w->frame * 3 + i * 11) % 34) - 17;
            int x = w->image ? 86 + i * 18 : 44 + i * 18;
            int y = w->image ? 232 + i * 22 : 170 + i * 24;
            lv_obj_set_pos(w->fog[i], x + dx, y);
            lv_obj_set_style_bg_opa(w->fog[i], ((w->frame + i * 3) % 16) < 8 ? LV_OPA_70 : LV_OPA_40,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    if (w->image)
    {
        for (i = 0; i < VB_WEATHER_DECOR_COUNT; i++)
        {
            if (w->decor[i])
            {
                int phase = (w->frame + i * 5) % 24;
                int dy = phase < 12 ? phase / 3 : (23 - phase) / 3;
                lv_obj_set_style_bg_opa(w->decor[i], phase < 12 ? LV_OPA_90 : LV_OPA_50,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
                if (i == 0) lv_obj_set_pos(w->decor[i], 82, 142 - dy);
                else if (i == 1) lv_obj_set_pos(w->decor[i], 292, 152 + dy);
                else if (i == 2) lv_obj_set_pos(w->decor[i], 96, 266 - dy);
                else if (i == 3) lv_obj_set_pos(w->decor[i], 276, 250 + dy);
                else if (i == 4) lv_obj_set_pos(w->decor[i], 118, 112 - dy);
                else if (i == 5) lv_obj_set_pos(w->decor[i], 258, 122 + dy);
            }
        }
    }
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
                g_vb_runtime.flow_label = RT_NULL;
                g_vb_runtime.script_flow_label = RT_NULL;
                g_vb_runtime.script_flow_field[0] = '\0';
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
        char image_path[VB_MAX_PATH + 2];
        target = vb_script_find_object(args[0]);
        vb_script_resolve_asset_path(args[1], resolved, sizeof(resolved));
        vb_script_to_lvgl_image_path(resolved, image_path, sizeof(image_path));
        if (target && target->obj && image_path[0]) lv_img_set_src(target->obj, image_path);
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
        char image_path[VB_MAX_PATH + 2];
        lv_obj_t *image = lv_img_create(g_vb_runtime.root);
        vb_script_resolve_asset_path(args[0], resolved, sizeof(resolved));
        vb_script_to_lvgl_image_path(resolved, image_path, sizeof(image_path));
        if (image_path[0]) lv_img_set_src(image, image_path);
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
    if (vb_extract_call_args(line, "vibe_touch_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[96];
        if (rt_strcmp(args[1], "count") == 0)
        {
            rt_snprintf(text, sizeof(text), "Touch %lu", (unsigned long)g_vb_runtime.touch.count);
        }
        else if (rt_strcmp(args[1], "event") == 0)
        {
            rt_snprintf(text, sizeof(text), "%s", vb_runtime_touch_event_name(g_vb_runtime.touch.last_event));
        }
        else if (rt_strcmp(args[1], "gesture") == 0)
        {
            rt_snprintf(text, sizeof(text), "%s", vb_runtime_touch_gesture_name(g_vb_runtime.touch.gesture_dir));
        }
        else if (rt_strcmp(args[1], "delta") == 0)
        {
            rt_snprintf(text, sizeof(text), "%d,%d", g_vb_runtime.touch.delta.x, g_vb_runtime.touch.delta.y);
        }
        else if (rt_strcmp(args[1], "duration") == 0)
        {
            rt_snprintf(text, sizeof(text), "%lums", (unsigned long)g_vb_runtime.touch.last_duration_ms);
        }
        else if (rt_strcmp(args[1], "active") == 0)
        {
            rt_snprintf(text, sizeof(text), "%s", g_vb_runtime.touch.active ? "active" : "idle");
        }
        else if (g_vb_runtime.touch.ready)
        {
            rt_snprintf(text, sizeof(text), "%d,%d", g_vb_runtime.touch.point.x, g_vb_runtime.touch.point.y);
        }
        else
        {
            rt_snprintf(text, sizeof(text), "Touch --");
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
        }
        rt_kprintf("[vb_runtime][lua] touch.%s %s\n", args[1], text);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_gpio_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[96];
        if (!vb_runtime_gpio_format_text(args[1], text, sizeof(text)))
        {
            rt_snprintf(text, sizeof(text), "GPIO --");
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
        }
        rt_kprintf("[vb_runtime][lua] gpio.%s %s\n", args[1], text);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_power_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[96];
        if (!vb_runtime_power_format_text(args[1], text, sizeof(text)))
        {
            rt_snprintf(text, sizeof(text), "Power --");
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
        }
        rt_kprintf("[vb_runtime][lua] power.%s %s\n", args[1], text);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_display_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[96];
        if (!vb_runtime_display_format_text(args[1], text, sizeof(text)))
        {
            rt_snprintf(text, sizeof(text), "Display --");
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
        }
        rt_kprintf("[vb_runtime][lua] display.%s %s\n", args[1], text);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_display_brightness", args, &argc) && argc >= 1)
    {
        char json[VB_DISPLAY_JSON_MAX];
        int result = vb_runtime_display_set_brightness_text(args[0], json, sizeof(json));
        rt_kprintf("[vb_runtime][lua] display.brightness %s rc=%d %s\n", args[0], result, json);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_voice_start", args, &argc))
    {
        char json[VB_VOICE_JSON_MAX];
        const char *duration = argc >= 1 ? args[0] : "";
        int result = vb_runtime_voice_start_text(duration, json, sizeof(json));
        rt_kprintf("[vb_runtime][lua] voice.start %s rc=%d %s\n", duration[0] ? duration : "default", result, json);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_voice_clear", args, &argc))
    {
        char json[VB_VOICE_JSON_MAX];
        int result = vb_runtime_voice_clear_json(json, sizeof(json));
        rt_kprintf("[vb_runtime][lua] voice.clear rc=%d %s\n", result, json);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_voice_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[96];
        if (!vb_runtime_voice_format_text(args[1], text, sizeof(text)))
        {
            rt_snprintf(text, sizeof(text), "Voice --");
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
        }
        rt_kprintf("[vb_runtime][lua] voice.%s %s\n", args[1], text);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_flow_label", args, &argc) && argc >= 2)
    {
        vb_script_object_t *target = vb_script_find_object(args[0]);
        char text[VB_FLOW_MAX_PAYLOAD + VB_FLOW_MAX_CHANNEL + 48];
        if (!vb_runtime_flow_format_text(args[1], text, sizeof(text)))
        {
            rt_snprintf(text, sizeof(text), "Flow --");
        }
        if (target && target->obj)
        {
            lv_label_set_text(target->obj, text);
            g_vb_runtime.script_last_label = target->obj;
            g_vb_runtime.script_flow_label = target->obj;
            vb_safe_copy(g_vb_runtime.script_flow_field, sizeof(g_vb_runtime.script_flow_field), args[1]);
            g_vb_runtime.script_flow_seen_total = g_vb_flow.total_count;
        }
        rt_kprintf("[vb_runtime][lua] flow.%s %s\n", args[1], text);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_rgb", args, &argc) && argc >= 1)
    {
        char json[VB_RGB_JSON_MAX];
        int result = vb_runtime_rgb_set_text(args[0], json, sizeof(json));
        rt_kprintf("[vb_runtime][lua] rgb %s rc=%d %s\n", args[0], result, json);
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_snake_autoplay", args, &argc) && argc >= 4)
    {
        vb_snake_start(args[0], atoi(args[1]), atoi(args[2]), atoi(args[3]));
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_2048_game", args, &argc))
    {
        vb_2048_start(argc >= 1 ? args[0] : "2048");
        return 1;
    }
    if (vb_extract_call_args(line, "vibe_weather_pet", args, &argc) && argc >= 5)
    {
        vb_weather_start(args[0], args[1], atoi(args[2]), atoi(args[3]), atoi(args[4]));
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
    g_vb_runtime.script_flow_label = RT_NULL;
    g_vb_runtime.script_runtime_active = 0;
    g_vb_runtime.script_tick_prefix[0] = '\0';
    g_vb_runtime.script_flow_field[0] = '\0';
    g_vb_runtime.script_flow_seen_total = g_vb_flow.total_count;
    rt_memset(&g_vb_runtime.snake, 0, sizeof(g_vb_runtime.snake));
    rt_memset(&g_vb_runtime.weather, 0, sizeof(g_vb_runtime.weather));

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
    g_vb_runtime.script_flow_label = RT_NULL;
    g_vb_runtime.script_object_count = 0;
    g_vb_runtime.script_tick_prefix[0] = '\0';
    g_vb_runtime.script_flow_field[0] = '\0';
    rt_memset(&g_vb_runtime.snake, 0, sizeof(g_vb_runtime.snake));
    rt_memset(&g_vb_runtime.weather, 0, sizeof(g_vb_runtime.weather));
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

static void vb_runtime_request_home_from_navigation(const char *source)
{
    if (g_vb_runtime.pending_stop) return;
    rt_kprintf("[vb_runtime] %s home dx=%d dy=%d\n",
               source ? source : "navigation",
               g_vb_runtime.touch.delta.x,
               g_vb_runtime.touch.delta.y);
    g_vb_runtime.pending_stop = 1;
    if (g_vb_runtime.timer) lv_timer_ready(g_vb_runtime.timer);
}

static void vb_runtime_touch_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point = {0, 0};
    uint32_t now;
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
        code != LV_EVENT_CLICKED && code != LV_EVENT_GESTURE)
    {
        return;
    }
    indev = lv_event_get_indev(event);
    if (!indev) indev = lv_indev_get_act();
    if (indev) lv_indev_get_point(indev, &point);
    now = rt_tick_get();
    g_vb_runtime.touch.available = 1;
    g_vb_runtime.touch.ready = 1;
    g_vb_runtime.touch.active = (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) ? 1 : 0;
    g_vb_runtime.touch.last_event = code;
    g_vb_runtime.touch.point = point;
    g_vb_runtime.touch.last_tick = now;
    if (code == LV_EVENT_PRESSED)
    {
        g_vb_runtime.touch.count++;
        g_vb_runtime.touch.press_tick = now;
        g_vb_runtime.touch.press_point = point;
        g_vb_runtime.touch.release_point = point;
        g_vb_runtime.touch.delta.x = 0;
        g_vb_runtime.touch.delta.y = 0;
        g_vb_runtime.touch.last_duration_ms = 0;
        g_vb_runtime.touch.gesture_dir = LV_DIR_NONE;
        if (g_vb_runtime.game2048.active) g_vb_runtime.game2048.drag_consumed = 0;
    }
    else if (code == LV_EVENT_PRESSING)
    {
        g_vb_runtime.touch.delta.x = point.x - g_vb_runtime.touch.press_point.x;
        g_vb_runtime.touch.delta.y = point.y - g_vb_runtime.touch.press_point.y;
        g_vb_runtime.touch.last_duration_ms = vb_runtime_touch_tick_elapsed_ms(g_vb_runtime.touch.press_tick, now);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED)
    {
        g_vb_runtime.touch.release_point = point;
        g_vb_runtime.touch.delta.x = point.x - g_vb_runtime.touch.press_point.x;
        g_vb_runtime.touch.delta.y = point.y - g_vb_runtime.touch.press_point.y;
        g_vb_runtime.touch.last_duration_ms = vb_runtime_touch_tick_elapsed_ms(g_vb_runtime.touch.press_tick, now);
    }
    else if (code == LV_EVENT_GESTURE)
    {
        g_vb_runtime.touch.gesture_dir = indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
        g_vb_runtime.touch.release_point = point;
        g_vb_runtime.touch.delta.x = point.x - g_vb_runtime.touch.press_point.x;
        g_vb_runtime.touch.delta.y = point.y - g_vb_runtime.touch.press_point.y;
        g_vb_runtime.touch.last_duration_ms = vb_runtime_touch_tick_elapsed_ms(g_vb_runtime.touch.press_tick, now);
    }
    if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED ||
         code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED ||
         code == LV_EVENT_GESTURE) &&
        g_vb_runtime.touch.press_point.x <= VB_SCREEN_EDGE_BACK_X &&
        g_vb_runtime.touch.delta.x >= VB_SCREEN_EDGE_BACK_DX &&
        abs(g_vb_runtime.touch.delta.y) <= VB_SCREEN_EDGE_BACK_MAX_DY &&
        (code != LV_EVENT_GESTURE || g_vb_runtime.touch.gesture_dir == LV_DIR_RIGHT))
    {
        vb_runtime_request_home_from_navigation(code == LV_EVENT_GESTURE ? "edge gesture" : "edge drag");
        return;
    }
    if (g_vb_runtime.game2048.active)
    {
        if (code == LV_EVENT_GESTURE && !g_vb_runtime.game2048.drag_consumed)
        {
            vb_2048_handle_gesture(g_vb_runtime.touch.gesture_dir);
            g_vb_runtime.game2048.drag_consumed = 1;
            return;
        }
        if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED) &&
            !g_vb_runtime.game2048.drag_consumed)
        {
            lv_dir_t dir = vb_2048_dir_from_delta(g_vb_runtime.touch.delta.x, g_vb_runtime.touch.delta.y);
            if (dir != LV_DIR_NONE)
            {
                vb_2048_handle_swipe(dir, "release");
                g_vb_runtime.game2048.drag_consumed = 1;
            }
        }
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
    if (vb_runtime_voice_is_start_action(component->capability))
    {
        char json[VB_VOICE_JSON_MAX];
        int result = vb_runtime_voice_start_text(component->value, json, sizeof(json));
        if (result == RT_EOK) vb_set_status("voice recording requested");
        else if (result == -RT_EBUSY) vb_set_status("voice already recording");
        else vb_set_status("voice recording failed");
        rt_kprintf("[vb_runtime] action %s value=%s rc=%d %s\n",
                   component->capability,
                   component->value[0] ? component->value : "--",
                   result,
                   json);
        return;
    }
    if (vb_runtime_voice_is_clear_action(component->capability))
    {
        char json[VB_VOICE_JSON_MAX];
        int result = vb_runtime_voice_clear_json(json, sizeof(json));
        vb_set_status(result == RT_EOK ? "voice cleared" : "voice clear failed");
        rt_kprintf("[vb_runtime] action %s rc=%d %s\n", component->capability, result, json);
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

static void vb_runtime_clear_overlay_controls(void)
{
    if (g_vb_runtime.overlay_apps_button)
    {
        lv_obj_del(g_vb_runtime.overlay_apps_button);
        g_vb_runtime.overlay_apps_button = RT_NULL;
    }
    if (g_vb_runtime.overlay_nav_zone)
    {
        lv_obj_del(g_vb_runtime.overlay_nav_zone);
        g_vb_runtime.overlay_nav_zone = RT_NULL;
    }
}

static void vb_runtime_show_navigation_controls(void)
{
    vb_runtime_clear_overlay_controls();
    g_vb_runtime.overlay_nav_zone = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_vb_runtime.overlay_nav_zone);
    lv_obj_set_size(g_vb_runtime.overlay_nav_zone, VB_SCREEN_EDGE_BACK_X, LV_VER_RES_MAX);
    lv_obj_align(g_vb_runtime.overlay_nav_zone, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(g_vb_runtime.overlay_nav_zone, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(g_vb_runtime.overlay_nav_zone,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_ext_click_area(g_vb_runtime.overlay_nav_zone, 12);
    lv_obj_set_style_bg_opa(g_vb_runtime.overlay_nav_zone, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_vb_runtime.overlay_nav_zone, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g_vb_runtime.overlay_nav_zone, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(g_vb_runtime.overlay_nav_zone, vb_runtime_touch_event_cb, LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.overlay_nav_zone, vb_runtime_touch_event_cb, LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.overlay_nav_zone, vb_runtime_touch_event_cb, LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.overlay_nav_zone, vb_runtime_touch_event_cb, LV_EVENT_PRESS_LOST, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.overlay_nav_zone, vb_runtime_touch_event_cb, LV_EVENT_GESTURE, RT_NULL);
}

static void vb_launcher_refresh_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) return;
    g_vb_runtime.launcher_page = 0;
    g_vb_runtime.launcher_pending_delete[0] = '\0';
    vb_runtime_request_manager_refresh("refreshed");
}

static void vb_launcher_page_event_cb(lv_event_t *event)
{
    vb_launcher_page_action_t *action;
    int max_page;
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) return;
    action = (vb_launcher_page_action_t *)lv_event_get_user_data(event);
    if (!action) return;
    max_page = g_vb_runtime.launcher_total > 0 ? (g_vb_runtime.launcher_total - 1) / VB_LAUNCHER_MAX_ITEMS : 0;
    g_vb_runtime.launcher_page += action->delta;
    if (g_vb_runtime.launcher_page < 0) g_vb_runtime.launcher_page = 0;
    if (g_vb_runtime.launcher_page > max_page) g_vb_runtime.launcher_page = max_page;
    g_vb_runtime.launcher_pending_delete[0] = '\0';
    vb_runtime_request_manager_refresh("page changed");
}

static void vb_launcher_launch_event_cb(lv_event_t *event)
{
    vb_launcher_item_t *item;
    int result;
    if (LV_EVENT_SHORT_CLICKED != lv_event_get_code(event) &&
        LV_EVENT_CLICKED != lv_event_get_code(event)) return;
    item = (vb_launcher_item_t *)lv_event_get_user_data(event);
    if (!item || !item->id[0]) return;
    result = vb_runtime_app_launch(item->id);
    if (result == RT_EOK)
    {
        char text[VB_MAX_TEXT];
        rt_snprintf(text, sizeof(text), "launching %s", item->id);
        vb_set_status(text);
        if (g_vb_runtime.timer) lv_timer_ready(g_vb_runtime.timer);
    }
    else
    {
        vb_set_status(g_vb_runtime.app_last_error[0] ? g_vb_runtime.app_last_error : "launch failed");
    }
    rt_kprintf("[vb_runtime][launcher] launch app=%s rc=%d\n", item->id, result);
}

static void vb_launcher_delete_event_cb(lv_event_t *event)
{
    vb_launcher_item_t *item;
    int result;
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) return;
    item = (vb_launcher_item_t *)lv_event_get_user_data(event);
    if (!item || !item->id[0]) return;
    if (rt_strcmp(g_vb_runtime.launcher_pending_delete, item->id) != 0)
    {
        char text[VB_MAX_TEXT];
        vb_safe_copy(g_vb_runtime.launcher_pending_delete, sizeof(g_vb_runtime.launcher_pending_delete), item->id);
        rt_snprintf(text, sizeof(text), "confirm delete %s", item->id);
        vb_runtime_request_manager_refresh(text);
        return;
    }
    result = vb_runtime_app_delete(item->id);
    g_vb_runtime.launcher_pending_delete[0] = '\0';
    if (result == RT_EOK)
    {
        char text[VB_MAX_TEXT];
        int max_page;
        rt_snprintf(text, sizeof(text), "deleted %s", item->id);
        if (g_vb_runtime.launcher_total > 0) g_vb_runtime.launcher_total--;
        max_page = g_vb_runtime.launcher_total > 0 ? (g_vb_runtime.launcher_total - 1) / VB_LAUNCHER_MAX_ITEMS : 0;
        if (g_vb_runtime.launcher_page > max_page) g_vb_runtime.launcher_page = max_page;
        vb_runtime_request_manager_refresh(text);
    }
    else
    {
        vb_set_status(g_vb_runtime.app_last_error[0] ? g_vb_runtime.app_last_error : "delete failed");
    }
    rt_kprintf("[vb_runtime][launcher] delete app=%s rc=%d\n", item->id, result);
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

static void vb_runtime_request_manager_refresh(const char *message)
{
    vb_safe_copy(g_vb_runtime.manager_message, sizeof(g_vb_runtime.manager_message), message ? message : "App manager");
    g_vb_runtime.pending_manager_refresh = 1;
}

#if 0
static void vb_render_app_manager_ui(const char *reason)
{
    vb_app_summary_t apps[VB_LAUNCHER_MAX_ITEMS];
    char active[VB_MAX_APP_ID];
    char line[VB_MAX_TEXT + 48];
    const lv_coord_t safe_x = VB_SCREEN_SAFE_LEFT;
    const lv_coord_t safe_y = VB_SCREEN_SAFE_TOP;
    const lv_coord_t safe_w = VB_SCREEN_SAFE_WIDTH;
    const lv_coord_t safe_h = VB_SCREEN_SAFE_HEIGHT;
    const lv_coord_t row_w = safe_w;
    const lv_coord_t row_h = 44;
    const lv_coord_t row_gap = 6;
    const lv_coord_t row_y0 = safe_y + 88;
    int total = 0;
    int count;
    int i;

    vb_runtime_clear_overlay_controls();
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    g_vb_runtime.status_label = RT_NULL;
    g_vb_runtime.clock_label = RT_NULL;
    g_vb_runtime.flow_label = RT_NULL;
    g_vb_runtime.script_flow_label = RT_NULL;
    g_vb_runtime.component_count = 0;
    g_vb_runtime.launcher_count = 0;
    g_vb_runtime.launcher_total = 0;

    g_vb_runtime.touch.available = 1;
    g_vb_runtime.touch.ready = 1;
    g_vb_runtime.touch.active = 0;
    g_vb_runtime.touch.last_event = 0;
    g_vb_runtime.touch.gesture_dir = LV_DIR_NONE;
    g_vb_runtime.home_key_last_pressed = 0;
    g_vb_runtime.home_key_last_tick = rt_tick_get();

    vb_read_active_app(active, sizeof(active));
    count = vb_runtime_collect_apps(apps, VB_LAUNCHER_MAX_ITEMS, g_vb_runtime.launcher_page * VB_LAUNCHER_MAX_ITEMS, &total);
    if (count < 0)
    {
        count = 0;
        total = 0;
    }
    if (total > 0 && g_vb_runtime.launcher_page * VB_LAUNCHER_MAX_ITEMS >= total)
    {
        g_vb_runtime.launcher_page = (total - 1) / VB_LAUNCHER_MAX_ITEMS;
        count = vb_runtime_collect_apps(apps, VB_LAUNCHER_MAX_ITEMS, g_vb_runtime.launcher_page * VB_LAUNCHER_MAX_ITEMS, &total);
        if (count < 0) count = 0;
    }
    if (g_vb_runtime.launcher_page < 0) g_vb_runtime.launcher_page = 0;
    g_vb_runtime.launcher_total = total;

    g_vb_runtime.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_vb_runtime.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(g_vb_runtime.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_vb_runtime.root, LV_OBJ_FLAG_SCROLLABLE);
    vb_set_obj_bg(g_vb_runtime.root, 0x0b1120);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_PRESS_LOST, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_GESTURE, RT_NULL);

    lv_obj_t *back = lv_btn_create(g_vb_runtime.root);
    lv_obj_set_size(back, 64, 30);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, safe_x, safe_y);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back, vb_back_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_t *back_label = vb_create_label(back, "Back", FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_center(back_label);

    lv_obj_t *refresh = lv_btn_create(g_vb_runtime.root);
    lv_obj_set_size(refresh, 70, 30);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -safe_x, safe_y);
    lv_obj_set_style_radius(refresh, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x0f766e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(refresh, vb_launcher_refresh_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_t *refresh_label = vb_create_label(refresh, "Refresh", FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_center(refresh_label);

    lv_obj_t *title = vb_create_label(g_vb_runtime.root, "App Manager", FONT_BIGL, lv_color_hex(0x5eead4));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, safe_y + 1);

    rt_snprintf(line, sizeof(line), "active=%s state=%s apps=%d page=%d/%d", active, vb_runtime_app_state_name(), total, total ? g_vb_runtime.launcher_page + 1 : 0, total ? ((total - 1) / VB_LAUNCHER_MAX_ITEMS) + 1 : 0);
    lv_obj_t *state = vb_create_label(g_vb_runtime.root, line, FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_set_width(state, safe_w);
    lv_label_set_long_mode(state, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(state, LV_ALIGN_TOP_MID, 0, safe_y + 38);

    lv_obj_t *hint = vb_create_label(g_vb_runtime.root,
                                     reason && reason[0] ? reason : "Tap Launch to run an installed app",
                                     FONT_SMALL,
                                     g_vb_runtime.app_failed ? lv_color_hex(0xfca5a5) : lv_color_hex(0xcbd5e1));
    lv_obj_set_width(hint, safe_w);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, safe_y + 60);

    if (count == 0)
    {
        lv_obj_t *panel = lv_obj_create(g_vb_runtime.root);
        lv_obj_set_size(panel, safe_w, 118);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, row_y0 + 16);
        vb_set_obj_bg(panel, 0x172554);
        lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *empty = vb_create_label(panel, "No Runtime apps found\nInstall over serial or BLE", FONT_NORMAL, LV_COLOR_WHITE);
        lv_obj_set_width(empty, 260);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty);
    }
    else
    {
        for (i = 0; i < count; i++)
        {
            lv_coord_t y = row_y0 + i * (row_h + row_gap);
            lv_obj_t *row = lv_obj_create(g_vb_runtime.root);
            lv_obj_set_size(row, row_w, row_h);
            lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
            vb_set_obj_bg(row, apps[i].active ? 0x1e3a8a : 0x111827);
            lv_obj_set_style_radius(row, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(row, apps[i].active ? 1 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(row, lv_color_hex(0x5eead4), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *name = vb_create_label(row, apps[i].name, FONT_SMALL, LV_COLOR_WHITE);
            lv_obj_set_width(name, row_w - 126);
            lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
            lv_obj_align(name, LV_ALIGN_TOP_LEFT, 10, 5);

            rt_snprintf(line, sizeof(line), "%s  %s", apps[i].id, apps[i].compatible ? "ready" : "incompatible");
            lv_obj_t *meta = vb_create_label(row, line, FONT_SMALL, apps[i].compatible ? lv_color_hex(0x93c5fd) : lv_color_hex(0xfca5a5));
            lv_obj_set_width(meta, row_w - 126);
            lv_label_set_long_mode(meta, LV_LABEL_LONG_CLIP);
            lv_obj_align(meta, LV_ALIGN_TOP_LEFT, 10, 25);

            vb_safe_copy(g_vb_runtime.launcher_items[i].id, sizeof(g_vb_runtime.launcher_items[i].id), apps[i].id);
            g_vb_runtime.launcher_count++;

            lv_obj_t *launch = lv_btn_create(row);
            lv_obj_set_size(launch, 64, 28);
            lv_obj_align(launch, LV_ALIGN_RIGHT_MID, -50, 0);
            lv_obj_set_style_radius(launch, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(launch, lv_color_hex(apps[i].compatible ? 0x2dd4bf : 0x475569), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (apps[i].compatible)
            {
                lv_obj_add_event_cb(launch, vb_launcher_launch_event_cb, LV_EVENT_SHORT_CLICKED, &g_vb_runtime.launcher_items[i]);
            }
            lv_obj_t *launch_label = vb_create_label(launch, apps[i].active ? "Open" : "Launch", FONT_SMALL,
                                                     apps[i].compatible ? lv_color_hex(0x0f172a) : lv_color_hex(0xcbd5e1));
            lv_obj_center(launch_label);

            lv_obj_t *del = lv_btn_create(row);
            int confirming = rt_strcmp(g_vb_runtime.launcher_pending_delete, apps[i].id) == 0;
            lv_obj_set_size(del, 42, 28);
            lv_obj_align(del, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_set_style_radius(del, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(del, lv_color_hex(confirming ? 0xf97316 : 0x7f1d1d), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_event_cb(del, vb_launcher_delete_event_cb, LV_EVENT_CLICKED, &g_vb_runtime.launcher_items[i]);
            lv_obj_t *del_label = vb_create_label(del, confirming ? "OK" : "Del", FONT_SMALL, LV_COLOR_WHITE);
            lv_obj_center(del_label);
        }
    }

    if (total > VB_LAUNCHER_MAX_ITEMS)
    {
        lv_obj_t *prev;
        lv_obj_t *next;
        lv_obj_t *prev_label;
        lv_obj_t *next_label;
        int max_page = (total - 1) / VB_LAUNCHER_MAX_ITEMS;
        g_vb_runtime.launcher_page_actions[0].delta = -1;
        g_vb_runtime.launcher_page_actions[1].delta = 1;
        prev = lv_btn_create(g_vb_runtime.root);
        lv_obj_set_size(prev, 68, 30);
        lv_obj_align(prev, LV_ALIGN_TOP_LEFT, safe_x, safe_y + safe_h - 36);
        lv_obj_set_style_radius(prev, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(prev, lv_color_hex(g_vb_runtime.launcher_page > 0 ? 0x334155 : 0x1f2937), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (g_vb_runtime.launcher_page > 0) lv_obj_add_event_cb(prev, vb_launcher_page_event_cb, LV_EVENT_CLICKED, &g_vb_runtime.launcher_page_actions[0]);
        prev_label = vb_create_label(prev, "Prev", FONT_SMALL, g_vb_runtime.launcher_page > 0 ? LV_COLOR_WHITE : lv_color_hex(0x64748b));
        lv_obj_center(prev_label);

        next = lv_btn_create(g_vb_runtime.root);
        lv_obj_set_size(next, 68, 30);
        lv_obj_align(next, LV_ALIGN_TOP_RIGHT, -safe_x, safe_y + safe_h - 36);
        lv_obj_set_style_radius(next, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(next, lv_color_hex(g_vb_runtime.launcher_page < max_page ? 0x334155 : 0x1f2937), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (g_vb_runtime.launcher_page < max_page) lv_obj_add_event_cb(next, vb_launcher_page_event_cb, LV_EVENT_CLICKED, &g_vb_runtime.launcher_page_actions[1]);
        next_label = vb_create_label(next, "Next", FONT_SMALL, g_vb_runtime.launcher_page < max_page ? LV_COLOR_WHITE : lv_color_hex(0x64748b));
        lv_obj_center(next_label);
        rt_snprintf(line, sizeof(line), "%d-%d/%d",
                    g_vb_runtime.launcher_page * VB_LAUNCHER_MAX_ITEMS + (count ? 1 : 0),
                    g_vb_runtime.launcher_page * VB_LAUNCHER_MAX_ITEMS + count,
                    total);
    }
    else
    {
        rt_snprintf(line, sizeof(line), "Tap Apps from any app to return here");
    }
    lv_obj_t *foot = vb_create_label(g_vb_runtime.root, line, FONT_SMALL, lv_color_hex(0x94a3b8));
    lv_obj_set_width(foot, 170);
    lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(foot, LV_ALIGN_TOP_MID, 0, safe_y + safe_h - 32);

    g_vb_runtime.status_label = vb_create_label(g_vb_runtime.root, "launcher ready", FONT_SMALL, lv_color_hex(0xa7f3d0));
    lv_obj_set_width(g_vb_runtime.status_label, safe_w);
    lv_label_set_long_mode(g_vb_runtime.status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(g_vb_runtime.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.status_label, LV_ALIGN_TOP_MID, 0, safe_y + safe_h - 12);
}

#endif

static void vb_render_runtime_ui(int manifest_loaded, int main_lua_present)
{
    int i;
    char line[VB_MAX_TEXT + 32];

    g_vb_runtime.touch.available = 1;
    g_vb_runtime.touch.ready = 1;
    g_vb_runtime.touch.active = 0;
    g_vb_runtime.touch.last_event = 0;
    g_vb_runtime.touch.gesture_dir = LV_DIR_NONE;
    g_vb_runtime.home_key_last_pressed = 0;
    g_vb_runtime.home_key_last_tick = rt_tick_get();

    g_vb_runtime.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_vb_runtime.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(g_vb_runtime.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_vb_runtime.root, LV_OBJ_FLAG_SCROLLABLE);
    vb_set_obj_bg(g_vb_runtime.root, 0x0f172a);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_PRESS_LOST, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(g_vb_runtime.root, vb_runtime_touch_event_cb, LV_EVENT_GESTURE, RT_NULL);

    vb_runtime_show_navigation_controls();

    lv_obj_t *title = vb_create_label(g_vb_runtime.root, "VibeBoard Runtime", FONT_BIGL, lv_color_hex(0x5eead4));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, VB_SCREEN_SAFE_TOP + 2);

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
    lv_obj_align(g_vb_runtime.clock_label, LV_ALIGN_BOTTOM_MID, 0, -64);

    g_vb_runtime.flow_label = vb_create_label(g_vb_runtime.root, "flow ready: waiting for phone", FONT_SMALL, lv_color_hex(0xbfdbfe));
    lv_obj_set_width(g_vb_runtime.flow_label, 350);
    lv_obj_set_style_text_align(g_vb_runtime.flow_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.flow_label, LV_ALIGN_BOTTOM_MID, 0, -38);
    vb_runtime_flow_update_label();

    g_vb_runtime.status_label = vb_create_label(g_vb_runtime.root, "serial bridge ready", FONT_SMALL, lv_color_hex(0xa7f3d0));
    lv_obj_set_width(g_vb_runtime.status_label, 350);
    lv_obj_set_style_text_align(g_vb_runtime.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_vb_runtime.status_label, LV_ALIGN_BOTTOM_MID, 0, -14);
}

static int vb_load_active_package(void)
{
    char manifest_path[VB_MAX_PATH];
    char lua_path[VB_MAX_PATH];
    char *json;
    int manifest_loaded = 0;
    int main_lua_present = 0;
    int start_result = RT_EOK;

    vibeboard_lua_stop_app();
    rt_memset(&g_vb_runtime.game2048, 0, sizeof(g_vb_runtime.game2048));
    rt_memset(g_vb_runtime.components, 0, sizeof(g_vb_runtime.components));
    g_vb_runtime.component_count = 0;
    g_vb_runtime.app_running = 0;
    g_vb_runtime.app_failed = 0;
    vb_read_active_app(g_vb_runtime.active_app, sizeof(g_vb_runtime.active_app));
    vb_safe_copy(g_vb_runtime.app_name, sizeof(g_vb_runtime.app_name), g_vb_runtime.active_app);
    vb_safe_copy(g_vb_runtime.description, sizeof(g_vb_runtime.description), "Install app packages over USB serial or BLE. No firmware flash needed.");

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
    vb_runtime_flow_load_state();
    rt_kprintf("[vb_runtime] active app: %s\n", g_vb_runtime.active_app);
    if (!manifest_loaded && !main_lua_present)
    {
        if (rt_strcmp(g_vb_runtime.active_app, VIBEBOARD_DEFAULT_APP_ID) == 0)
        {
            vb_runtime_set_app_result(RT_EOK, "home idle");
            vb_runtime_return_home();
            return RT_EOK;
        }
        g_vb_runtime.app_failed = 1;
        vb_runtime_set_app_result(-RT_ERROR, "active package missing manifest and main.lua");
        vb_runtime_return_home();
        return -RT_ERROR;
    }
    vb_render_runtime_ui(manifest_loaded, main_lua_present);

    if (vibeboard_lua_runtime_available() && main_lua_present)
    {
        start_result = vibeboard_lua_start_script(lua_path, manifest_path);
        if (start_result == RT_EOK)
        {
            g_vb_runtime.app_running = 1;
            g_vb_runtime.app_failed = 0;
            vb_runtime_set_app_result(RT_EOK, "lua app running");
            rt_kprintf("[vb_runtime] lua app started: %s engine=%s\n", lua_path, vibeboard_lua_runtime_name());
        }
        else
        {
            g_vb_runtime.app_failed = 1;
            vb_runtime_set_app_result(start_result, "lua adapter failed, manifest fallback shown");
            vb_set_status("lua failed; returning home");
            rt_kprintf("[vb_runtime] lua adapter failed, using manifest fallback\n");
        }
    }
    else if (manifest_loaded)
    {
        g_vb_runtime.app_running = 1;
        vb_runtime_set_app_result(RT_EOK, "manifest app running");
        if (main_lua_present)
        {
            rt_kprintf("[vb_runtime] main.lua present, lua adapter unavailable: %s\n", lua_path);
        }
    }
    else if (rt_strcmp(g_vb_runtime.active_app, VIBEBOARD_DEFAULT_APP_ID) == 0)
    {
        vb_runtime_set_app_result(RT_EOK, "home idle");
    }
    else
    {
        g_vb_runtime.app_failed = 1;
        vb_runtime_set_app_result(-RT_ERROR, "active package missing manifest and main.lua");
    }
    return start_result;
}

static void vb_runtime_stop_current_app(void)
{
    vb_runtime_clear_overlay_controls();
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    vibeboard_lua_stop_app();
    vb_weather_release_image();
    rt_memset(&g_vb_runtime.game2048, 0, sizeof(g_vb_runtime.game2048));
    rt_memset(g_vb_runtime.components, 0, sizeof(g_vb_runtime.components));
    g_vb_runtime.component_count = 0;
    g_vb_runtime.status_label = RT_NULL;
    g_vb_runtime.clock_label = RT_NULL;
    g_vb_runtime.flow_label = RT_NULL;
    g_vb_runtime.script_flow_label = RT_NULL;
    g_vb_runtime.app_running = 0;
    g_vb_runtime.app_failed = 0;
    g_vb_runtime.app_stop_count++;
    (void)vb_write_active_app(VIBEBOARD_DEFAULT_APP_ID);
    vb_safe_copy(g_vb_runtime.active_app, sizeof(g_vb_runtime.active_app), VIBEBOARD_DEFAULT_APP_ID);
    vb_runtime_set_app_result(RT_EOK, "stopped");
    vb_runtime_return_home();
    rt_kprintf("[vb_runtime] app stopped active=%s\n", g_vb_runtime.active_app);
}

static int vb_runtime_reload_current(void)
{
    if (!g_vb_runtime.running)
    {
        return gui_app_run(APP_ID);
    }
    vb_runtime_clear_overlay_controls();
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    vb_weather_release_image();
    g_vb_runtime.status_label = RT_NULL;
    g_vb_runtime.clock_label = RT_NULL;
    g_vb_runtime.flow_label = RT_NULL;
    return vb_load_active_package();
}

static void vb_runtime_request_reload(void)
{
    if (g_vb_runtime.running)
    {
        g_vb_runtime.pending_reload = 1;
        if (g_vb_runtime.timer) lv_timer_ready(g_vb_runtime.timer);
    }
    else
    {
        (void)gui_app_run(APP_ID);
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

static void vb_runtime_install_close_file(void)
{
    if (g_vb_runtime.install_fd >= 0)
    {
        if (fsync(g_vb_runtime.install_fd) != 0)
        {
            rt_kprintf("[vb_runtime] install file sync failed: %s\n",
                       g_vb_runtime.install_path[0] ? g_vb_runtime.install_path : "--");
        }
        close(g_vb_runtime.install_fd);
        g_vb_runtime.install_fd = -1;
    }
    g_vb_runtime.install_path[0] = '\0';
    g_vb_runtime.install_offset = 0;
}

static int vb_runtime_install_session_matches(const char *app_id)
{
    return app_id && g_vb_runtime.install_app[0] &&
           rt_strcmp(g_vb_runtime.install_app, app_id) == 0;
}

static int vb_runtime_install_begin_app(const char *app_id)
{
    char staging_dir[VB_MAX_PATH];
    char marker[VB_MAX_PATH];
    int cleanup;
    if (!vb_is_safe_app_id(app_id))
    {
        rt_kprintf("usage: vb_runtime_install_begin <app_id>\n");
        return -RT_EINVAL;
    }
    if (vb_prepare_filesystem() != RT_EOK) return -RT_ERROR;
    if (g_vb_runtime.install_app[0])
    {
        cleanup = vb_runtime_install_abort_app(g_vb_runtime.install_app);
        if (cleanup != RT_EOK) return cleanup;
    }
    vb_runtime_install_close_file();
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
    vb_safe_copy(g_vb_runtime.install_app, sizeof(g_vb_runtime.install_app), app_id);
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
    int result;
    int written = 0;
    int flags;

    if (!vb_is_safe_app_id(app_id) || !vb_is_runtime_package_path(path) ||
        !offset_text || !hex || rt_strlen(hex) > VB_MAX_HEX_CHARS)
    {
        rt_kprintf("[vb_runtime] install file failed: invalid args\n");
        return -RT_EINVAL;
    }
    if (!vb_runtime_install_session_matches(app_id))
    {
        rt_kprintf("[vb_runtime] install file failed: session=%s app=%s\n",
                   g_vb_runtime.install_app[0] ? g_vb_runtime.install_app : "--", app_id);
        return -RT_EBUSY;
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

    if (g_vb_runtime.install_fd < 0 || rt_strcmp(g_vb_runtime.install_path, file_path) != 0)
    {
        vb_runtime_install_close_file();
        flags = O_WRONLY | O_CREAT;
        if (offset == 0) flags |= O_TRUNC;
        g_vb_runtime.install_fd = open(file_path, flags, 0);
        if (g_vb_runtime.install_fd < 0)
        {
            rt_kprintf("[vb_runtime] install file open failed: %s\n", file_path);
            return -RT_ERROR;
        }
        vb_safe_copy(g_vb_runtime.install_path, sizeof(g_vb_runtime.install_path), file_path);
        g_vb_runtime.install_offset = -1;
    }

    if (g_vb_runtime.install_offset != offset)
    {
        if (lseek(g_vb_runtime.install_fd, offset, SEEK_SET) < 0)
        {
            vb_runtime_install_close_file();
            rt_kprintf("[vb_runtime] install file seek failed: %s\n", file_path);
            return -RT_ERROR;
        }
    }
    result = vb_write_hex_chunk(g_vb_runtime.install_fd, hex, &written);
    if (result != RT_EOK)
    {
        vb_runtime_install_close_file();
        rt_kprintf("[vb_runtime] install file write failed: %s (%d)\n", path, result);
        return result;
    }
    if (fsync(g_vb_runtime.install_fd) != 0)
    {
        vb_runtime_install_close_file();
        rt_kprintf("[vb_runtime] install file sync failed: %s\n", path);
        return -RT_ERROR;
    }
    g_vb_runtime.install_offset = offset + written;
    vb_runtime_install_close_file();
    if (!g_vb_runtime.quiet_logs)
    {
        rt_kprintf("[vb_runtime] install chunk: %s %ld %u\n", path, offset,
                   (unsigned int)(rt_strcmp(hex, "-") == 0 ? 0 : rt_strlen(hex) / 2));
    }
    return RT_EOK;
}

static int vb_runtime_install_end_app(const char *app_id)
{
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
    if (!vb_runtime_install_session_matches(app_id))
    {
        rt_kprintf("[vb_runtime] install end failed: session=%s app=%s\n",
                   g_vb_runtime.install_app[0] ? g_vb_runtime.install_app : "--", app_id);
        return -RT_EBUSY;
    }
    vb_runtime_install_close_file();
    vb_build_install_dir(staging_dir, sizeof(staging_dir), VIBEBOARD_STAGING_PREFIX, app_id);
    rt_snprintf(app_dir, sizeof(app_dir), "%s/%s", VIBEBOARD_APP_ROOT, app_id);
    app_dir[sizeof(app_dir) - 1] = '\0';
    vb_build_install_dir(backup_dir, sizeof(backup_dir), VIBEBOARD_BACKUP_PREFIX, app_id);
    {
        int validate = vb_runtime_package_validate_stage(app_id, staging_dir);
        if (validate != RT_EOK)
        {
            rt_kprintf("[vb_runtime] install end failed: package invalid %s rc=%d\n", app_id, validate);
            return validate;
        }
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
    vb_runtime_install_clear_session();
    rt_kprintf("[vb_runtime] install complete: %s\n", app_id);
    if (g_vb_runtime.running)
    {
        vb_runtime_request_reload();
    }
    return RT_EOK;
}

#if VB_RUNTIME_HAS_HTTP_APP_OTA
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

    fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
static int vb_runtime_install_url_app(const char *app_id, const char *base_url)
{
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
}
#endif

static int vb_runtime_status_command(void)
{
    char active[VB_MAX_APP_ID];
    int fs = vb_prepare_filesystem();
    vb_read_active_app(active, sizeof(active));
    vb_runtime_gpio_refresh();
    rt_kprintf("[vb_runtime] api=%s\n", VIBEBOARD_RUNTIME_API_VERSION);
    rt_kprintf("[vb_runtime] root=%s\n", VIBEBOARD_APP_ROOT);
    rt_kprintf("[vb_runtime] active=%s\n", active);
    rt_kprintf("[vb_runtime] fs=%s\n", fs == RT_EOK ? "ready" : "unavailable");
    rt_kprintf("[vb_runtime] capabilities=%s\n", VIBEBOARD_RUNTIME_CAPABILITY_API_VERSION);
    rt_kprintf("[vb_runtime] power_api=%s battery=%d charger=%d\n",
               VIBEBOARD_RUNTIME_POWER_API_VERSION, VB_RUNTIME_HAS_POWER_BATTERY,
               VB_RUNTIME_HAS_POWER_CHARGER);
    rt_kprintf("[vb_runtime] display_api=%s display=%d brightness=%d ready=%d\n",
               VIBEBOARD_RUNTIME_DISPLAY_API_VERSION, VB_RUNTIME_HAS_DISPLAY,
               g_vb_runtime.display_brightness, g_vb_runtime.display_last_ready);
    rt_kprintf("[vb_runtime] rgb_api=%s rgb=%d color=%06lx\n",
               VIBEBOARD_RUNTIME_RGB_API_VERSION, VB_RUNTIME_HAS_RGB,
               (unsigned long)(g_vb_runtime.rgb_color & 0xffffffu));
    rt_kprintf("[vb_runtime] touch_api=%s ready=%d active=%d count=%lu event=%s gesture=%s x=%d y=%d dx=%d dy=%d dur=%lums\n",
               VIBEBOARD_RUNTIME_TOUCH_API_VERSION,
               g_vb_runtime.touch.ready,
               g_vb_runtime.touch.active,
               (unsigned long)g_vb_runtime.touch.count,
               vb_runtime_touch_event_name(g_vb_runtime.touch.last_event),
               vb_runtime_touch_gesture_name(g_vb_runtime.touch.gesture_dir),
               g_vb_runtime.touch.point.x,
               g_vb_runtime.touch.point.y,
               g_vb_runtime.touch.delta.x,
               g_vb_runtime.touch.delta.y,
               (unsigned long)g_vb_runtime.touch.last_duration_ms);
    rt_kprintf("[vb_runtime] gpio_api=%s ready=%d key1=%s(%d) key2=%s(%d)\n",
               VIBEBOARD_RUNTIME_GPIO_API_VERSION,
               g_vb_runtime.gpio.ready,
               g_vb_runtime.gpio.key1_ok ? (g_vb_runtime.gpio.key1_pressed ? "pressed" : "released") : "--",
               g_vb_runtime.gpio.key1_level,
               g_vb_runtime.gpio.key2_ok ? (g_vb_runtime.gpio.key2_pressed ? "pressed" : "released") : "--",
               g_vb_runtime.gpio.key2_level);
    rt_kprintf("[vb_runtime] lua=%s\n", vibeboard_lua_runtime_available() ? vibeboard_lua_runtime_name() : "manifest-fallback");
    rt_kprintf("[vb_runtime] app_manager=%s state=%s launches=%lu stops=%lu last=%d error=%s\n",
               VIBEBOARD_RUNTIME_APP_MANAGER_API_VERSION,
               vb_runtime_app_state_name(),
               (unsigned long)g_vb_runtime.app_launch_count,
               (unsigned long)g_vb_runtime.app_stop_count,
               g_vb_runtime.app_last_status,
               g_vb_runtime.app_last_error[0] ? g_vb_runtime.app_last_error : "--");
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

static int vb_runtime_app_status_command(void)
{
    char json[VB_APP_JSON_MAX];
    int result = vb_runtime_app_status_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_apps_status_command(void)
{
    char json[VB_APP_JSON_MAX];
    int result = vb_runtime_app_list_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

#if VB_RUNTIME_HAS_BT_PAN
static int vb_runtime_pan_status_command(void)
{
    rt_kprintf("[vb_runtime][pan] api=diagnostic/v1\n");
    rt_kprintf("[vb_runtime][pan] product_transport=ble-gatt-or-serial\n");
    rt_kprintf("[vb_runtime][pan] available=1\n");
    rt_kprintf("[vb_runtime][pan] built=1\n");
    rt_kprintf("[vb_runtime][pan] initialized=%d\n", g_vb_pan.initialized);
    rt_kprintf("[vb_runtime][pan] bt_opened=%d\n", g_vb_pan.bt_opened);
    rt_kprintf("[vb_runtime][pan] stack_ready=%d\n", g_vb_pan.stack_ready);
    rt_kprintf("[vb_runtime][pan] name_requested=%d\n", g_vb_pan.name_requested);
    rt_kprintf("[vb_runtime][pan] name_confirmed=%d\n", g_vb_pan.name_confirmed);
    rt_kprintf("[vb_runtime][pan] final_name_requested=%d\n", g_vb_pan.final_name_requested);
    rt_kprintf("[vb_runtime][pan] scan_mode=%d\n", g_vb_pan.scan_mode);
    rt_kprintf("[vb_runtime][pan] target_scan_mode=%d\n", g_vb_pan.target_scan_mode);
    rt_kprintf("[vb_runtime][pan] scan_mode_fsm=%d\n", g_vb_pan.scan_mode_fsm);
    rt_kprintf("[vb_runtime][pan] scan_confirmed=%d\n", g_vb_pan.scan_confirmed);
    rt_kprintf("[vb_runtime][pan] bt_connected=%d\n", g_vb_pan.bt_connected);
    rt_kprintf("[vb_runtime][pan] pan_connected=%d\n", g_vb_pan.pan_connected);
    rt_kprintf("[vb_runtime][pan] connecting=%d\n", g_vb_pan.connecting);
    rt_kprintf("[vb_runtime][pan] pairing_pending=%d\n", g_vb_pan.pairing_pending);
    rt_kprintf("[vb_runtime][pan] open_retries=%d\n", g_vb_pan.open_retries);
    rt_kprintf("[vb_runtime][pan] last_error=%d\n", g_vb_pan.last_error);
    rt_kprintf("[vb_runtime][pan] local_name=%s\n",
               g_vb_pan.local_name[0] ? g_vb_pan.local_name : VIBEBOARD_BT_PAN_NAME);
    rt_kprintf("[vb_runtime][pan] lwip=1\n");
    rt_kprintf("[vb_runtime][pan] app_ota_http=%d\n", VB_RUNTIME_HAS_HTTP_APP_OTA);
    return RT_EOK;
}
#endif

static int vb_runtime_rgb_status_command(void)
{
    char json[VB_RGB_JSON_MAX];
    int result = vb_runtime_rgb_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}
static int vb_runtime_sensors_status_command(void)
{
    char json[VB_SENSOR_JSON_MAX];
    int result = vb_runtime_sensors_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_power_status_command(void)
{
    char json[VB_POWER_JSON_MAX];
    int result = vb_runtime_power_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_display_status_command(void)
{
    char json[VB_DISPLAY_JSON_MAX];
    int result = vb_runtime_display_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_gpio_status_command(void)
{
    char json[VB_GPIO_JSON_MAX];
    int result = vb_runtime_gpio_read_json(json, sizeof(json));
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
    if (rt_strcmp(capability, "display.brightness") == 0 ||
        rt_strcmp(capability, "display.size") == 0 ||
        rt_strcmp(capability, "display.resolution") == 0 ||
        rt_strcmp(capability, "display.state") == 0 ||
        rt_strcmp(capability, "display.bpp") == 0 ||
        rt_strcmp(capability, "screen.brightness") == 0 ||
        rt_strcmp(capability, "screen.size") == 0 ||
        rt_strcmp(capability, "vibeboard.display.brightness") == 0 ||
        rt_strcmp(capability, "vibeboard.display.size") == 0 ||
        rt_strcmp(capability, "vibeboard.display.state") == 0 ||
        rt_strcmp(capability, "vibeboard.display.bpp") == 0)
    {
        return vb_runtime_display_format_text(capability, dst, cap);
    }
    if (rt_strncmp(capability, "flow.", 5) == 0 ||
        rt_strncmp(capability, "vibeboard.flow.", 15) == 0)
    {
        return vb_runtime_flow_format_text(capability, dst, cap);
    }
    if (rt_strcmp(capability, "voice.ready") == 0 ||
        rt_strcmp(capability, "voice.recording") == 0 ||
        rt_strcmp(capability, "voice.state") == 0 ||
        rt_strcmp(capability, "voice.seq") == 0 ||
        rt_strcmp(capability, "voice.bytes") == 0 ||
        rt_strcmp(capability, "voice.duration") == 0 ||
        rt_strcmp(capability, "voice.dropped") == 0 ||
        rt_strcmp(capability, "voice.error") == 0 ||
        rt_strcmp(capability, "voice.rate") == 0 ||
        rt_strcmp(capability, "voice.built") == 0 ||
        rt_strcmp(capability, "voice.available") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.ready") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.recording") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.state") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.seq") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.bytes") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.duration") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.dropped") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.error") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.rate") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.built") == 0 ||
        rt_strcmp(capability, "vibeboard.voice.available") == 0)
    {
        return vb_runtime_voice_format_text(capability, dst, cap);
    }
    if (rt_strcmp(capability, "power.battery") == 0 ||
        rt_strcmp(capability, "battery") == 0 ||
        rt_strcmp(capability, "vibeboard.power.battery") == 0)
    {
        return vb_runtime_power_format_text(capability, dst, cap);
    }
    if (rt_strcmp(capability, "charger") == 0 ||
        rt_strcmp(capability, "power.charger") == 0 ||
        rt_strcmp(capability, "power.charger.status") == 0 ||
        rt_strcmp(capability, "power.charger.state") == 0 ||
        rt_strcmp(capability, "power.charger.det") == 0 ||
        rt_strcmp(capability, "power.charger.en") == 0 ||
        rt_strcmp(capability, "power.charger.fault") == 0 ||
        rt_strcmp(capability, "vibeboard.power.charger") == 0 ||
        rt_strcmp(capability, "vibeboard.power.charger.status") == 0 ||
        rt_strcmp(capability, "vibeboard.power.charger.state") == 0 ||
        rt_strcmp(capability, "vibeboard.power.charger.det") == 0 ||
        rt_strcmp(capability, "vibeboard.power.charger.en") == 0 ||
        rt_strcmp(capability, "vibeboard.power.charger.fault") == 0)
    {
        return vb_runtime_power_format_text(capability, dst, cap);
    }
    if (rt_strcmp(capability, "gpio.key1") == 0 ||
        rt_strcmp(capability, "vibeboard.gpio.key1") == 0)
    {
        return vb_runtime_gpio_format_text("key1", dst, cap);
    }
    if (rt_strcmp(capability, "gpio.key1.level") == 0 ||
        rt_strcmp(capability, "vibeboard.gpio.key1.level") == 0)
    {
        return vb_runtime_gpio_format_text("key1.level", dst, cap);
    }
    if (rt_strcmp(capability, "gpio.key2") == 0 ||
        rt_strcmp(capability, "vibeboard.gpio.key2") == 0)
    {
        return vb_runtime_gpio_format_text("key2", dst, cap);
    }
    if (rt_strcmp(capability, "gpio.key2.level") == 0 ||
        rt_strcmp(capability, "vibeboard.gpio.key2.level") == 0)
    {
        return vb_runtime_gpio_format_text("key2.level", dst, cap);
    }
    if (rt_strcmp(capability, "touch.last") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.last") == 0)
    {
        if (g_vb_runtime.touch.ready)
        {
            rt_snprintf(dst, cap, "%d,%d", g_vb_runtime.touch.point.x, g_vb_runtime.touch.point.y);
        }
        else
        {
            rt_snprintf(dst, cap, "--");
        }
        return 1;
    }
    if (rt_strcmp(capability, "touch.count") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.count") == 0)
    {
        rt_snprintf(dst, cap, "%lu", (unsigned long)g_vb_runtime.touch.count);
        return 1;
    }
    if (rt_strcmp(capability, "touch.event") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.event") == 0)
    {
        rt_snprintf(dst, cap, "%s", vb_runtime_touch_event_name(g_vb_runtime.touch.last_event));
        return 1;
    }
    if (rt_strcmp(capability, "touch.gesture") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.gesture") == 0)
    {
        rt_snprintf(dst, cap, "%s", vb_runtime_touch_gesture_name(g_vb_runtime.touch.gesture_dir));
        return 1;
    }
    if (rt_strcmp(capability, "touch.delta") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.delta") == 0)
    {
        rt_snprintf(dst, cap, "%d,%d", g_vb_runtime.touch.delta.x, g_vb_runtime.touch.delta.y);
        return 1;
    }
    if (rt_strcmp(capability, "touch.duration") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.duration") == 0)
    {
        rt_snprintf(dst, cap, "%lums", (unsigned long)g_vb_runtime.touch.last_duration_ms);
        return 1;
    }
    if (rt_strcmp(capability, "touch.active") == 0 ||
        rt_strcmp(capability, "vibeboard.touch.active") == 0)
    {
        rt_snprintf(dst, cap, "%s", g_vb_runtime.touch.active ? "active" : "idle");
        return 1;
    }
    return 0;
}

static void vb_runtime_poll_home_key(uint32_t now)
{
#if VB_RUNTIME_HAS_GPIO
    int pressed;
    if (!g_vb_runtime.root) return;
    (void)vb_runtime_gpio_refresh();
    pressed = (g_vb_runtime.gpio.key1_ok && g_vb_runtime.gpio.key1_pressed) ? 1 : 0;
    if (pressed && !g_vb_runtime.home_key_last_pressed &&
        now - g_vb_runtime.home_key_last_tick >= rt_tick_from_millisecond(VB_KEY_HOME_DEBOUNCE_MS))
    {
        g_vb_runtime.home_key_last_pressed = 1;
        g_vb_runtime.home_key_last_tick = now;
        rt_kprintf("[vb_runtime] hardware key home key1=%d\n",
                   g_vb_runtime.gpio.key1_pressed);
        vb_runtime_return_home();
        return;
    }
    g_vb_runtime.home_key_last_pressed = pressed;
#else
    (void)now;
#endif
}

static void vb_timer_cb(lv_timer_t *timer)
{
    int i;
    char text[VB_FLOW_MAX_PAYLOAD + VB_FLOW_MAX_CHANNEL + 48];
    uint32_t now = rt_tick_get();
    int sensor_refreshed = 0;
    (void)timer;
    if (g_vb_runtime.pending_stop)
    {
        g_vb_runtime.pending_stop = 0;
        vb_runtime_stop_current_app();
        return;
    }
    if (g_vb_runtime.pending_manager_refresh)
    {
        g_vb_runtime.pending_manager_refresh = 0;
        vb_runtime_return_home();
        return;
    }
    if (g_vb_runtime.pending_reload)
    {
        g_vb_runtime.pending_reload = 0;
        vb_runtime_reload_current();
        return;
    }
    vb_runtime_poll_home_key(now);
    if (!g_vb_runtime.root) return;
    g_vb_runtime.tick_count++;
    if (g_vb_runtime.clock_label &&
        now - g_vb_runtime.last_clock_update >= rt_tick_from_millisecond(VB_STATUS_TICK_REFRESH_MS))
    {
        rt_snprintf(text, sizeof(text), "tick=%lu", (unsigned long)g_vb_runtime.tick_count);
        lv_label_set_text(g_vb_runtime.clock_label, text);
        g_vb_runtime.last_clock_update = now;
    }
    if (g_vb_runtime.flow_label && g_vb_runtime.flow_seen_total != g_vb_flow.total_count)
    {
        vb_runtime_flow_update_label();
    }
    if (g_vb_runtime.script_flow_label && g_vb_runtime.script_flow_seen_total != g_vb_flow.total_count)
    {
        if (vb_runtime_flow_format_text(g_vb_runtime.script_flow_field, text, sizeof(text)))
        {
            lv_label_set_text(g_vb_runtime.script_flow_label, text);
        }
        g_vb_runtime.script_flow_seen_total = g_vb_flow.total_count;
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
                 rt_strncmp(component->capability, "vibeboard.sensor.", 17) == 0 ||
                 rt_strcmp(component->capability, "battery") == 0 ||
                 rt_strcmp(component->capability, "charger") == 0 ||
                 rt_strncmp(component->capability, "power.", 6) == 0 ||
                 rt_strncmp(component->capability, "display.", 8) == 0 ||
                 rt_strncmp(component->capability, "screen.", 7) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.display.", 18) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.power.", 16) == 0 ||
                 rt_strncmp(component->capability, "voice.", 6) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.voice.", 16) == 0 ||
                 rt_strncmp(component->capability, "flow.", 5) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.flow.", 15) == 0 ||
                 rt_strncmp(component->capability, "touch.", 6) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.touch.", 17) == 0 ||
                 rt_strncmp(component->capability, "gpio.", 5) == 0 ||
                 rt_strncmp(component->capability, "vibeboard.gpio.", 16) == 0)
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
    if (g_vb_runtime.weather.active && now - g_vb_runtime.weather.last_run >= g_vb_runtime.weather.period_ticks)
    {
        g_vb_runtime.weather.last_run = now;
        vb_weather_step();
    }
}

static void on_start(void)
{
    rt_memset(&g_vb_runtime, 0, sizeof(g_vb_runtime));
    vb_runtime_state_defaults();
    g_vb_runtime_state_initialized = 1;
    g_vb_runtime.running = 1;
    vb_prepare_filesystem();
    (void)vb_runtime_display_load_state();
    (void)vb_runtime_rgb_load_state();
    vb_runtime_recover_install_state();
    vb_load_active_package();
    g_vb_runtime.timer = lv_timer_create(vb_timer_cb, VB_TIMER_PERIOD_MS, RT_NULL);
    rt_kprintf("[vb_runtime] start api=%s root=%s\n", VIBEBOARD_RUNTIME_API_VERSION, VIBEBOARD_APP_ROOT);
}

static void on_stop(void)
{
    g_vb_runtime.running = 0;
    vb_runtime_install_close_file();
    if (g_vb_runtime.timer)
    {
        lv_timer_del(g_vb_runtime.timer);
        g_vb_runtime.timer = RT_NULL;
    }
    vibeboard_lua_stop_app();
    vb_runtime_clear_overlay_controls();
    if (g_vb_runtime.root)
    {
        lv_obj_del(g_vb_runtime.root);
        g_vb_runtime.root = RT_NULL;
    }
    vb_weather_release_image();
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

#if VB_RUNTIME_AUTORUN_UI
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
#endif

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

static int vb_runtime_app(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_app_status_command();
}
MSH_CMD_EXPORT(vb_runtime_app, show VibeBoard runtime app status as JSON);

static int vb_runtime_apps(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_apps_status_command();
}
MSH_CMD_EXPORT(vb_runtime_apps, list VibeBoard runtime apps as JSON);

static int vb_runtime_apps_page(int argc, char **argv)
{
    char json[VB_BLE_STATUS_MAX];
    int offset = argc >= 2 ? (int)strtol(argv[1], RT_NULL, 10) : 0;
    int limit = argc >= 3 ? (int)strtol(argv[2], RT_NULL, 10) : VB_LAUNCHER_MAX_ITEMS;
    int result = vb_runtime_app_list_page_json(json, sizeof(json), offset, limit);
    vb_runtime_print_json_line(json);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_apps_page, list one VibeBoard runtime app page as JSON);

static int vb_runtime_launch(int argc, char **argv)
{
    int result;
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_launch <app_id>\n");
        return -RT_EINVAL;
    }
    result = vb_runtime_app_launch(argv[1]);
    rt_kprintf("%s launch app=%s rc=%d\n", result == RT_EOK ? "ok" : "err", argv[1], result);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_launch, launch VibeBoard runtime app);

static int vb_runtime_stop_app_msh(int argc, char **argv)
{
    int result;
    (void)argc;
    (void)argv;
    result = vb_runtime_app_stop();
    rt_kprintf("%s stop rc=%d\n", result == RT_EOK ? "ok" : "err", result);
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_stop_app_msh, vb_runtime_stop, stop active VibeBoard runtime app);

static int vb_runtime_delete(int argc, char **argv)
{
    int result;
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_delete <app_id>\n");
        return -RT_EINVAL;
    }
    result = vb_runtime_app_delete(argv[1]);
    rt_kprintf("%s delete app=%s rc=%d\n", result == RT_EOK ? "ok" : "err", argv[1], result);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_delete, delete stopped VibeBoard runtime app);

static int vb_runtime_staging_clear(int argc, char **argv)
{
    int removed = 0;
    int result;
    (void)argc;
    (void)argv;
    result = vb_runtime_staging_clear_all(&removed);
    rt_kprintf("%s staging_clear removed=%d rc=%d\n",
               result == RT_EOK ? "ok" : "err",
               removed,
               result);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_staging_clear, clear all VibeBoard runtime staging installs);

static int vb_runtime_capabilities(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_capabilities_status_command();
}
MSH_CMD_EXPORT(vb_runtime_capabilities, show VibeBoard runtime capabilities as JSON);

#if VB_RUNTIME_HAS_BT_PAN
static int vb_runtime_pan_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_pan_status_command();
}
MSH_CMD_EXPORT(vb_runtime_pan_status, show VibeBoard PAN experiment status);
#endif

static int vb_runtime_sensors(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_sensors_status_command();
}
MSH_CMD_EXPORT(vb_runtime_sensors, read VibeBoard built-in sensors as JSON);

static int vb_runtime_power(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_power_status_command();
}
MSH_CMD_EXPORT(vb_runtime_power, read VibeBoard power status as JSON);

static int vb_runtime_display(int argc, char **argv)
{
    char json[VB_DISPLAY_JSON_MAX];
    int result;
    if (argc >= 2)
    {
        result = vb_runtime_display_set_brightness_text(argv[1], json, sizeof(json));
        vb_runtime_print_json_line(json);
        return result;
    }
    return vb_runtime_display_status_command();
}
MSH_CMD_EXPORT(vb_runtime_display, read or set VibeBoard display brightness as JSON);

static int vb_runtime_gpio(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_gpio_status_command();
}
MSH_CMD_EXPORT(vb_runtime_gpio, read VibeBoard GPIO whitelist status as JSON);

static int vb_runtime_touch_status_command(void)
{
    char json[VB_TOUCH_JSON_MAX];
    int result = vb_runtime_touch_read_json(json, sizeof(json));
    vb_runtime_print_json_line(json);
    return result;
}

static int vb_runtime_touch(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_touch_status_command();
}
MSH_CMD_EXPORT(vb_runtime_touch, read VibeBoard touch status as JSON);

static int vb_runtime_rgb(int argc, char **argv)
{
    char json[VB_RGB_JSON_MAX];
    int result;
    if (argc >= 2)
    {
        result = vb_runtime_rgb_set_text(argv[1], json, sizeof(json));
        vb_runtime_print_json_line(json);
        return result;
    }
    return vb_runtime_rgb_status_command();
}
MSH_CMD_EXPORT(vb_runtime_rgb, read or set VibeBoard RGB LED color);
static int vb_runtime_json_read_msh(int argc, char **argv)
{
    char text[VB_BLE_STATUS_MAX];
    uint32_t offset;
    uint32_t max_bytes;
    int result;
    if (argc < 4)
    {
        rt_kprintf("usage: json_read <kind> <offset> <max_bytes>\n");
        return -RT_EINVAL;
    }
    offset = (uint32_t)strtoul(argv[2], RT_NULL, 10);
    max_bytes = (uint32_t)strtoul(argv[3], RT_NULL, 10);
    result = vb_runtime_ble_json_read(argv[1], offset, max_bytes, text, sizeof(text));
    rt_kprintf("%s\n", text);
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_json_read_msh, json_read, read VibeBoard Runtime JSON as hex chunks);
MSH_CMD_EXPORT_ALIAS(vb_runtime_json_read_msh, vb_runtime_json_read, read VibeBoard Runtime JSON as hex chunks);

static int vb_runtime_flow_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_flow_status_command();
}
MSH_CMD_EXPORT(vb_runtime_flow_status, show VibeBoard BLE info flow status);

static int vb_runtime_flow_send(int argc, char **argv)
{
    uint32_t sequence;
    int result;
    if (argc < 4)
    {
        rt_kprintf("usage: vb_runtime_flow_send <channel> <seq> <hex_utf8_payload>\n");
        return -RT_EINVAL;
    }
    sequence = (uint32_t)strtoul(argv[2], RT_NULL, 10);
    result = vb_runtime_flow_send_hex(argv[1], sequence, argv[3]);
    rt_kprintf("%s flow_send channel=%s seq=%lu bytes=%d total=%lu\n",
               result >= 0 ? "ok" : "err",
               argv[1],
               (unsigned long)sequence,
               result >= 0 ? result : 0,
               (unsigned long)g_vb_flow.total_count);
    return result >= 0 ? RT_EOK : result;
}
MSH_CMD_EXPORT(vb_runtime_flow_send, append one VibeBoard BLE info flow frame);

static int vb_runtime_flow_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vb_runtime_flow_clear_state();
    rt_kprintf("ok flow_clear total=0\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(vb_runtime_flow_clear, clear VibeBoard BLE info flow history);

static int vb_runtime_voice(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_voice_json_status_command();
}
MSH_CMD_EXPORT(vb_runtime_voice, read VibeBoard voice bridge status as JSON);

static int vb_runtime_voice_status_msh(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return vb_runtime_voice_status_command();
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_voice_status_msh, vb_runtime_voice_status, show VibeBoard voice bridge status);

static int vb_runtime_voice_start_msh(int argc, char **argv)
{
    uint32_t duration_ms = argc >= 2 ? (uint32_t)strtoul(argv[1], RT_NULL, 10) : VB_VOICE_DEFAULT_MS;
    int result = vb_runtime_voice_start(duration_ms);
    vb_runtime_voice_status_command();
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_voice_start_msh, vb_runtime_voice_start, capture VibeBoard microphone audio);

static int vb_runtime_voice_read_msh(int argc, char **argv)
{
    char text[VB_BLE_STATUS_MAX];
    uint32_t offset = argc >= 2 ? (uint32_t)strtoul(argv[1], RT_NULL, 10) : 0;
    uint32_t max_bytes = argc >= 3 ? (uint32_t)strtoul(argv[2], RT_NULL, 10) : VB_VOICE_CHUNK_BYTES;
    int result = vb_runtime_voice_read_hex(offset, max_bytes, text, sizeof(text));
    rt_kprintf("[vb_runtime][voice] %s\n", result == RT_EOK ? text : "err voice_read");
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_voice_read_msh, vb_runtime_voice_read, read VibeBoard microphone PCM as hex);

static int vb_runtime_voice_clear_msh(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vb_runtime_voice_clear();
    rt_kprintf("[vb_runtime][voice] cleared\n");
    return RT_EOK;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_voice_clear_msh, vb_runtime_voice_clear, clear VibeBoard voice bridge capture);

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
    rt_kprintf("[vb_runtime][ble] ok ble_core api=%s name=%s init=%d power=%d service=%d\n",
               VIBEBOARD_RUNTIME_BLE_API_VERSION, VIBEBOARD_BLE_NAME,
               g_vb_ble.initialized, g_vb_ble.power_on, g_vb_ble.service_ready);
    rt_kprintf("[vb_runtime][ble] ok ble_conn adv=%d conn=%d notify=%d mtu=%d state=%d\n",
               g_vb_ble.advertising, g_vb_ble.connected, g_vb_ble.notify_cccd ? 1 : 0,
               g_vb_ble.mtu, (int)vb_ble_adv_context_state());
    rt_kprintf("[vb_runtime][ble] ok ble_adv idx=%d trans=%d start_rc=%d stop_rc=%d reason=%d starts=%lu stops=%lu restarts=%lu\n",
               (int)vb_ble_adv_context_index(),
               (int)vb_ble_adv_context_transist(),
               (int)g_vb_ble.last_adv_start_rc,
               (int)g_vb_ble.last_adv_stop_rc,
               (int)g_vb_ble.last_adv_stop_reason,
               (unsigned long)g_vb_ble.adv_start_events,
               (unsigned long)g_vb_ble.adv_stop_events,
               (unsigned long)g_vb_ble.adv_restart_requests);
    return RT_EOK;
#else
    rt_kprintf("[vb_runtime][ble] unavailable\n");
    return -RT_ENOSYS;
#endif
}
MSH_CMD_EXPORT(vb_runtime_ble_status, show VibeBoard BLE install status);

static int vb_runtime_ble_restart(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if VB_RUNTIME_HAS_BLE_INSTALL
    uint8_t result = vb_ble_advertising_force_restart();
    rt_kprintf("[vb_runtime][ble] restart rc=%d\n", (int)result);
    return result == SIBLES_ADV_NO_ERR ? RT_EOK : -RT_ERROR;
#else
    rt_kprintf("[vb_runtime][ble] unavailable\n");
    return -RT_ENOSYS;
#endif
}
MSH_CMD_EXPORT(vb_runtime_ble_restart, force restart VibeBoard BLE advertising);

#if VB_RUNTIME_HAS_BT_PAN
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
#endif

static int vb_runtime_install_begin(int argc, char **argv)
{
    int result;
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_install_begin <app_id>\n");
        return -RT_EINVAL;
    }
    result = vb_runtime_install_begin_app(argv[1]);
    rt_kprintf("%s install_begin app=%s rc=%d\n",
               result == RT_EOK ? "ok" : "err",
               argv[1],
               result);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_install_begin, begin VibeBoard runtime app serial install);

static int vb_runtime_install_file(int argc, char **argv)
{
    int result;
    if (argc < 5)
    {
        rt_kprintf("usage: vb_runtime_install_file <app_id> <path> <offset> <hex>\n");
        return -RT_EINVAL;
    }
    result = vb_runtime_install_file_chunk(argv[1], argv[2], argv[3], argv[4]);
    rt_kprintf("%s install_file app=%s path=%s offset=%s rc=%d\n",
               result == RT_EOK ? "ok" : "err",
               argv[1],
               argv[2],
               argv[3],
               result);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_install_file, write hex chunk to VibeBoard runtime app file);

static int vb_runtime_install_abort(int argc, char **argv)
{
    int result;
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_install_abort <app_id>\n");
        return -RT_EINVAL;
    }
    result = vb_runtime_install_abort_app(argv[1]);
    rt_kprintf("%s install_abort app=%s rc=%d\n",
               result == RT_EOK ? "ok" : "err",
               argv[1],
               result);
    return result;
}
MSH_CMD_EXPORT(vb_runtime_install_abort, abort VibeBoard runtime app serial install);

static int vb_runtime_install_end(int argc, char **argv)
{
    int result;
    if (argc < 2)
    {
        rt_kprintf("usage: vb_runtime_install_end <app_id>\n");
        return -RT_EINVAL;
    }
    result = vb_runtime_install_end_app(argv[1]);
    if (result == RT_EOK)
    {
        rt_kprintf("ok install_end app=%s active=%s rc=%d\n", argv[1], argv[1], result);
    }
    else
    {
        rt_kprintf("err install_end app=%s rc=%d\n", argv[1], result);
    }
    return result;
}
MSH_CMD_EXPORT(vb_runtime_install_end, finish VibeBoard runtime app serial install);

#if VB_RUNTIME_HAS_HTTP_APP_OTA
static int vb_runtime_install_url(int argc, char **argv)
{
    if (argc < 3)
    {
        rt_kprintf("usage: vb_runtime_install_url <app_id> <http_base_url>\n");
        return -RT_EINVAL;
    }
    return vb_runtime_install_url_app(argv[1], argv[2]);
}
MSH_CMD_EXPORT(vb_runtime_install_url, install VibeBoard runtime app from experimental HTTP base URL);
#endif

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
