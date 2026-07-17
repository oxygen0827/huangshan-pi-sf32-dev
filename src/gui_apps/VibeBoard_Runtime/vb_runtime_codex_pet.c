#include "vb_runtime_codex_pet.h"
#include "app_mem.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dfs_posix.h>

#define VB_PET_HOLD_CONTEXT_NEW "pet.new"
#define VB_PET_HOLD_CONTEXT_CONTINUE "pet.continue"
#define VB_PET_ASR_TIMEOUT_MS 120000
#define VB_PET_MIN_VOICE_MS 700
#define VB_PET_STARTUP_GRACE_MS 1500
#define VB_PET_CANCEL_Y 320
#define VB_PET_CANCEL_DY 60
#define VB_PET_HEARTBEAT_TTL_MS 30000
#define VB_PET_TEXT_MAX 193
#define VB_PET_PROJECT_MAX 64
#define VB_PET_QUOTA_MAX 96
#define VB_PET_APPROVAL_ID_MAX 25
#define VB_PET_APPROVAL_SUMMARY_MAX 49
#define VB_PET_LISTENING_CUE_MS 100
#define VB_PET_VOICE_UI_ENABLED 0
#define VB_PET_ROCKY_DIR "/sdcard/apps/codex_pet/assets/rocky"
#define VB_PET_ROCKY_RLE_MAGIC 0x454c5256u
#define VB_PET_ROCKY_RLE_VERSION 1u
#define VB_PET_ROCKY_RLE_HEADER_SIZE 20u

static const char *const g_vb_pet_rocky_paths[5][2] = {
    {VB_PET_ROCKY_DIR "/idle0.rle", VB_PET_ROCKY_DIR "/idle1.rle"},
    {VB_PET_ROCKY_DIR "/running0.rle", VB_PET_ROCKY_DIR "/running1.rle"},
    {VB_PET_ROCKY_DIR "/needs0.rle", VB_PET_ROCKY_DIR "/needs1.rle"},
    {VB_PET_ROCKY_DIR "/ready0.rle", VB_PET_ROCKY_DIR "/ready1.rle"},
    {VB_PET_ROCKY_DIR "/blocked0.rle", VB_PET_ROCKY_DIR "/blocked1.rle"},
};

typedef enum
{
    VB_PET_IDLE = 0,
    VB_PET_RECORDING,
    VB_PET_TRANSCRIBING,
    VB_PET_RUNNING,
    VB_PET_NEEDS_INPUT,
    VB_PET_READY,
    VB_PET_ERROR,
    VB_PET_DISCONNECTED
} vb_pet_state_t;

typedef struct
{
    int active;
    int continue_mode;
    int have_thread;
    int key2_last;
    int release_pending;
    int press_y;
    int dirty;
    vb_pet_state_t state;
    uint32_t voice_sequence;
    uint32_t voice_started_at;
    uint32_t voice_stop_deadline;
    uint32_t asr_deadline;
    uint32_t host_deadline;
    uint32_t host_sequence;
    uint32_t quota_sequence;
    uint32_t rgb_phase;
    int quota_live;
    int approval_pending;
    uint32_t approval_sequence;
    uint32_t task_sequence;
    uint32_t animation_phase;
    int task_index;
    int task_count;
    int rocky_available;
    int rocky_frame_key;
    char rgb_color[8];
    char project[VB_PET_PROJECT_MAX];
    char quota[VB_PET_QUOTA_MAX];
    char transcript[VB_PET_TEXT_MAX];
    char task[VB_PET_TEXT_MAX];
    char error[97];
    char approval_id[VB_PET_APPROVAL_ID_MAX];
    char approval_summary[VB_PET_APPROVAL_SUMMARY_MAX];
    char task_detail[VB_PET_TEXT_MAX];
    lv_obj_t *root;
    lv_obj_t *project_label;
    lv_obj_t *connection_label;
    lv_obj_t *pet_face;
    lv_obj_t *pet_body;
    lv_obj_t *pet_tail;
    lv_obj_t *left_ear;
    lv_obj_t *right_ear;
    lv_obj_t *pet_image;
    lv_img_dsc_t *rocky_frames[5][2];
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;
    lv_obj_t *mouth;
    lv_obj_t *status_label;
    lv_obj_t *transcript_label;
    lv_obj_t *task_label;
    lv_obj_t *quota_label;
    lv_obj_t *new_button;
    lv_obj_t *new_label;
    lv_obj_t *continue_button;
    lv_obj_t *continue_label;
    lv_obj_t *talk_button;
    lv_obj_t *talk_label;
    lv_obj_t *cancel_target;
    vb_codex_pet_ops_t ops;
} vb_codex_pet_state_t;

static vb_codex_pet_state_t g_pet;

static uint16_t vb_pet_read_le16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t vb_pet_read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
        ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int vb_pet_read_full(int fd, void *dst, uint32_t size)
{
    uint8_t *cursor = (uint8_t *)dst;
    uint32_t used = 0;
    while (used < size)
    {
        int count = read(fd, cursor + used, size - used);
        if (count <= 0) return -RT_ERROR;
        used += (uint32_t)count;
    }
    return RT_EOK;
}

static void vb_pet_release_rocky_frames(void)
{
    int row;
    int frame;
    for (row = 0; row < 5; row++)
    {
        for (frame = 0; frame < 2; frame++)
        {
            if (g_pet.rocky_frames[row][frame])
            {
                app_cache_img_free(g_pet.rocky_frames[row][frame]);
                g_pet.rocky_frames[row][frame] = RT_NULL;
            }
        }
    }
}

static int vb_pet_load_rocky_frame(const char *path, lv_img_dsc_t **out)
{
    uint8_t header[VB_PET_ROCKY_RLE_HEADER_SIZE];
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t cf;
    uint32_t raw_size;
    uint32_t run_count;
    uint32_t encoded_size;
    uint32_t pixel_count;
    uint32_t written = 0;
    uint32_t index;
    struct stat st;
    lv_img_dsc_t *image = RT_NULL;
    uint8_t *encoded = RT_NULL;
    uint8_t *pixels;
    int fd = -1;

    if (!path || !out) return -RT_EINVAL;
    *out = RT_NULL;
    if (stat(path, &st) != 0 || st.st_size < (off_t)sizeof(header)) return -RT_ERROR;
    fd = open(path, O_RDONLY);
    if (fd < 0 || vb_pet_read_full(fd, header, sizeof(header)) != RT_EOK) goto fail;
    magic = vb_pet_read_le32(&header[0]);
    version = vb_pet_read_le16(&header[4]);
    width = vb_pet_read_le16(&header[6]);
    height = vb_pet_read_le16(&header[8]);
    cf = vb_pet_read_le16(&header[10]);
    raw_size = vb_pet_read_le32(&header[12]);
    run_count = vb_pet_read_le32(&header[16]);
    encoded_size = run_count * 5u;
    pixel_count = (uint32_t)width * (uint32_t)height;
    if (magic != VB_PET_ROCKY_RLE_MAGIC || version != VB_PET_ROCKY_RLE_VERSION ||
        width == 0 || height == 0 || width > 240 || height > 240 ||
        cf != LV_IMG_CF_TRUE_COLOR_ALPHA || raw_size != pixel_count * 3u ||
        run_count == 0 || run_count > pixel_count ||
        encoded_size / 5u != run_count ||
        st.st_size != (off_t)(sizeof(header) + encoded_size)) goto fail;
    image = app_cache_img_alloc(width, height, LV_IMG_CF_TRUE_COLOR_ALPHA,
                                raw_size, IMAGE_CACHE_PSRAM);
    encoded = (uint8_t *)app_cache_alloc(encoded_size, IMAGE_CACHE_PSRAM);
    if (!image || !encoded || vb_pet_read_full(fd, encoded, encoded_size) != RT_EOK) goto fail;
    close(fd);
    fd = -1;
    pixels = (uint8_t *)image->data;
    for (index = 0; index < run_count; index++)
    {
        const uint8_t *record = &encoded[index * 5u];
        uint32_t count = vb_pet_read_le16(record);
        uint32_t run;
        if (count == 0 || written + count > pixel_count) goto fail;
        for (run = 0; run < count; run++)
        {
            pixels[written * 3u] = record[2];
            pixels[written * 3u + 1u] = record[3];
            pixels[written * 3u + 2u] = record[4];
            written++;
        }
    }
    if (written != pixel_count) goto fail;
    app_cache_free(encoded);
    *out = image;
    return RT_EOK;

fail:
    if (fd >= 0) close(fd);
    if (encoded) app_cache_free(encoded);
    if (image) app_cache_img_free(image);
    rt_kprintf("[vb_runtime][codex_pet] Rocky frame load failed path=%s\n", path);
    return -RT_ERROR;
}

static int vb_pet_load_rocky_frames(void)
{
    int row;
    int frame;
    for (row = 0; row < 5; row++)
    {
        for (frame = 0; frame < 2; frame++)
        {
            if (vb_pet_load_rocky_frame(g_vb_pet_rocky_paths[row][frame],
                                        &g_pet.rocky_frames[row][frame]) != RT_EOK)
            {
                vb_pet_release_rocky_frames();
                return 0;
            }
        }
    }
    rt_kprintf("[vb_runtime][codex_pet] Rocky loaded: 10 RLE frames in PSRAM\n");
    return 1;
}

static int vb_pet_sequence_newer(uint32_t candidate, uint32_t current)
{
    if (candidate == 0) return 0;
    if (current == 0) return 1;
    return (int32_t)(candidate - current) > 0;
}

static void vb_pet_copy(char *dst, rt_size_t cap, const char *src)
{
    rt_size_t length;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    length = rt_strlen(src);
    if (length >= cap) length = cap - 1;
    while (length > 0 && (((uint8_t)src[length] & 0xc0u) == 0x80u)) length--;
    rt_memcpy(dst, src, length);
    dst[length] = '\0';
}

static lv_obj_t *vb_pet_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

static lv_obj_t *vb_pet_button(lv_obj_t *parent, const char *text,
                               int x, int y, int width, int height,
                               uint32_t color, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, RT_NULL);
    {
        lv_obj_t *label = vb_pet_label(button, text, 0xffffff);
        lv_obj_center(label);
    }
    return button;
}

static const char *vb_pet_status_text(void)
{
    switch (g_pet.state)
    {
    case VB_PET_RECORDING: return "Listening";
    case VB_PET_TRANSCRIBING: return "Transcribing";
    case VB_PET_RUNNING: return "Running";
    case VB_PET_NEEDS_INPUT: return "Needs input";
    case VB_PET_READY: return "Ready";
    case VB_PET_ERROR: return "Blocked";
    case VB_PET_DISCONNECTED: return "Disconnected";
    default: return "Ready";
    }
}

static uint32_t vb_pet_state_color(void)
{
    switch (g_pet.state)
    {
    case VB_PET_RECORDING:
    case VB_PET_TRANSCRIBING: return 0x22d3ee;
    case VB_PET_RUNNING: return 0x3b82f6;
    case VB_PET_NEEDS_INPUT: return 0xfbbf24;
    case VB_PET_READY: return 0x34d399;
    case VB_PET_ERROR: return 0xfb7185;
    case VB_PET_DISCONNECTED: return 0x94a3b8;
    default: return 0x5eead4;
    }
}

static int vb_pet_rocky_state_row(void)
{
    switch (g_pet.state)
    {
    case VB_PET_RUNNING: return 1;
    case VB_PET_NEEDS_INPUT: return 2;
    case VB_PET_READY: return 3;
    case VB_PET_ERROR: return 4;
    default: return 0;
    }
}

static void vb_pet_update_rocky(uint32_t phase)
{
    int row;
    int frame;
    int key;
    if (!g_pet.rocky_available || !g_pet.pet_image) return;
    row = vb_pet_rocky_state_row();
    frame = phase ? 1 : 0;
    key = row * 2 + frame;
    if (key == g_pet.rocky_frame_key) return;
    g_pet.rocky_frame_key = key;
    lv_img_set_src(g_pet.pet_image, g_pet.rocky_frames[row][frame]);
}

static void vb_pet_rgb_apply(const char *color)
{
    if (!g_pet.ops.rgb_set || !color || rt_strcmp(g_pet.rgb_color, color) == 0) return;
    vb_pet_copy(g_pet.rgb_color, sizeof(g_pet.rgb_color), color);
    (void)g_pet.ops.rgb_set(color);
}

static void vb_pet_rgb_tick(uint32_t now)
{
    const char *color = "off";
    int on = 1;
    uint32_t phase;
    switch (g_pet.state)
    {
    case VB_PET_RECORDING:
    case VB_PET_TRANSCRIBING:
        color = "cyan";
        break;
    case VB_PET_RUNNING:
        color = "blue";
        phase = now % rt_tick_from_millisecond(1800);
        on = phase < rt_tick_from_millisecond(900);
        break;
    case VB_PET_NEEDS_INPUT:
        color = "yellow";
        phase = now % rt_tick_from_millisecond(500);
        on = phase < rt_tick_from_millisecond(250);
        break;
    case VB_PET_READY:
        color = "green";
        phase = now % rt_tick_from_millisecond(1800);
        on = phase < rt_tick_from_millisecond(120) ||
             (phase >= rt_tick_from_millisecond(250) && phase < rt_tick_from_millisecond(370));
        break;
    case VB_PET_ERROR:
        color = "red";
        phase = now % rt_tick_from_millisecond(400);
        on = phase < rt_tick_from_millisecond(200);
        break;
    default:
        on = 0;
        break;
    }
    g_pet.rgb_phase = now;
    vb_pet_rgb_apply(on ? color : "off");
}

static void vb_pet_play_cue(const char *cue)
{
    if (g_pet.ops.cue_play && cue) (void)g_pet.ops.cue_play(cue);
}

static int vb_pet_json_int(const char *payload, const char *key, int fallback)
{
    const char *value;
    if (!payload || !key) return fallback;
    value = strstr(payload, key);
    if (!value) return fallback;
    value = strchr(value, ':');
    return value ? atoi(value + 1) : fallback;
}

static int vb_pet_json_string(const char *payload, const char *key,
                              char *dst, rt_size_t cap)
{
    char marker[32];
    const char *value;
    rt_size_t used = 0;
    if (!payload || !key || !dst || cap == 0) return 0;
    dst[0] = '\0';
    rt_snprintf(marker, sizeof(marker), "\"%s\":\"", key);
    value = strstr(payload, marker);
    if (!value) return 0;
    value += rt_strlen(marker);
    while (*value && *value != '"' && used + 1 < cap)
    {
        char current = *value++;
        if (current == '\\')
        {
            current = *value++;
            if (!current) break;
            if (current == 'n' || current == 'r' || current == 't') current = ' ';
            else if (current == 'u')
            {
                int skip;
                for (skip = 0; skip < 4 && *value; skip++) value++;
                current = '?';
            }
        }
        dst[used++] = current;
    }
    dst[used] = '\0';
    return used > 0;
}

static void vb_pet_clear_approval(void)
{
    g_pet.approval_pending = 0;
    g_pet.approval_id[0] = '\0';
    g_pet.approval_summary[0] = '\0';
}

static void vb_pet_receive_quota(uint32_t sequence, const char *payload)
{
    int primary;
    int secondary;
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.quota_sequence)) return;
    g_pet.quota_sequence = sequence;
    g_pet.quota_live = strstr(payload, "\"status\":\"live\"") != RT_NULL;
    primary = vb_pet_json_int(payload, "\"pU\"", -1);
    secondary = vb_pet_json_int(payload, "\"sU\"", -1);
    if (g_pet.quota_live && primary >= 0)
    {
        if (secondary >= 0)
            rt_snprintf(g_pet.quota, sizeof(g_pet.quota), "Quota 5h %d%% / week %d%%", primary, secondary);
        else
            rt_snprintf(g_pet.quota, sizeof(g_pet.quota), "Quota 5h %d%%", primary);
    }
    else
    {
        vb_pet_copy(g_pet.quota, sizeof(g_pet.quota), "Quota unavailable / stale");
    }
    g_pet.dirty = 1;
}

static void vb_pet_apply_mode_style(void)
{
    if (!g_pet.new_button || !g_pet.continue_button) return;
    if (g_pet.approval_pending)
    {
        lv_obj_set_style_bg_color(g_pet.new_button, lv_color_hex(0x15803d),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_pet.continue_button, lv_color_hex(0xbe123c),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }
    lv_obj_set_style_bg_color(g_pet.new_button, lv_color_hex(0x243244),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.continue_button, lv_color_hex(0x243244),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void vb_pet_render(void)
{
    uint32_t color;
    const char *task_text;
    if (!g_pet.active || !g_pet.root) return;
    color = vb_pet_state_color();
    lv_label_set_text(g_pet.project_label, g_pet.project[0] ? g_pet.project : "Project unavailable");
    lv_label_set_text(g_pet.connection_label,
                      g_pet.state == VB_PET_DISCONNECTED ? "Bridge offline" : "Bridge connected");
    lv_obj_set_style_text_color(g_pet.connection_label, lv_color_hex(color),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (g_pet.rocky_available)
    {
        vb_pet_update_rocky(g_pet.animation_phase);
    }
    else
    {
        lv_obj_set_style_bg_color(g_pet.pet_body, lv_color_hex(0xdbe7ed),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_pet.pet_face, lv_color_hex(0xe8f1f4),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_pet.pet_tail, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text(g_pet.status_label, vb_pet_status_text());
    lv_obj_set_style_text_color(g_pet.status_label, lv_color_hex(color),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (g_pet.error[0]) task_text = g_pet.error;
    else if (g_pet.approval_pending)
        task_text = g_pet.approval_summary[0] ? g_pet.approval_summary : "Approval requested";
    else task_text = g_pet.task_detail[0] ? g_pet.task_detail : "No active Codex tasks";
    lv_label_set_text(g_pet.transcript_label, task_text);
    if (g_pet.task_count > 0)
    {
        lv_label_set_text_fmt(g_pet.task_label, "Task %d of %d", g_pet.task_index, g_pet.task_count);
        lv_obj_clear_flag(g_pet.task_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
        lv_obj_add_flag(g_pet.task_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(g_pet.task_label, lv_color_hex(0x94a3b8),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    vb_pet_apply_mode_style();
    lv_label_set_text(g_pet.new_label, g_pet.approval_pending ? "Allow" : "<");
    lv_label_set_text(g_pet.continue_label, g_pet.approval_pending ? "Deny" : ">");
    lv_obj_clear_flag(g_pet.new_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_pet.continue_button, LV_OBJ_FLAG_HIDDEN);

    if (!g_pet.rocky_available)
    {
        if (g_pet.state == VB_PET_READY)
            lv_label_set_text(g_pet.mouth, "u");
        else if (g_pet.state == VB_PET_NEEDS_INPUT)
            lv_label_set_text(g_pet.mouth, "!");
        else if (g_pet.state == VB_PET_ERROR)
            lv_label_set_text(g_pet.mouth, "x");
        else
            lv_label_set_text(g_pet.mouth, "-");
        if (g_pet.state == VB_PET_RUNNING)
        {
            lv_obj_set_height(g_pet.left_eye, 24);
            lv_obj_set_height(g_pet.right_eye, 24);
        }
        else
        {
            lv_obj_set_height(g_pet.left_eye, 18);
            lv_obj_set_height(g_pet.right_eye, 18);
        }
    }
    g_pet.dirty = 0;
}

static void vb_pet_voice_snapshot(vb_codex_pet_voice_snapshot_t *snapshot)
{
    rt_memset(snapshot, 0, sizeof(*snapshot));
    if (g_pet.ops.voice_snapshot) g_pet.ops.voice_snapshot(snapshot);
}

static int vb_pet_local_voice_active(void)
{
    return g_pet.state == VB_PET_RECORDING || g_pet.state == VB_PET_TRANSCRIBING;
}

static void vb_pet_reset_voice_capture(void)
{
    if (g_pet.ops.voice_clear) g_pet.ops.voice_clear();
}

static void vb_pet_begin_voice(void)
{
    vb_codex_pet_voice_snapshot_t snapshot;
    const char *context;
    int result;
    if (!g_pet.active || !g_pet.ops.voice_start || g_pet.approval_pending) return;
    if (g_pet.state == VB_PET_RECORDING || g_pet.state == VB_PET_TRANSCRIBING) return;
    if (g_pet.continue_mode && !g_pet.have_thread)
    {
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "No task available to continue");
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
        g_pet.dirty = 1;
        return;
    }
    context = g_pet.continue_mode ? VB_PET_HOLD_CONTEXT_CONTINUE : VB_PET_HOLD_CONTEXT_NEW;
    g_pet.error[0] = '\0';
    g_pet.transcript[0] = '\0';
    vb_pet_reset_voice_capture();
    if (g_pet.ops.cue_play && g_pet.ops.cue_play("listening") == RT_EOK)
    {
        rt_thread_mdelay(VB_PET_LISTENING_CUE_MS);
        if (g_pet.ops.cue_stop) g_pet.ops.cue_stop();
    }
    result = g_pet.ops.voice_start(context);
    if (result != RT_EOK)
    {
        rt_snprintf(g_pet.error, sizeof(g_pet.error), "Microphone error (%d)", result);
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
        g_pet.dirty = 1;
        return;
    }
    vb_pet_voice_snapshot(&snapshot);
    g_pet.voice_sequence = snapshot.sequence;
    g_pet.voice_started_at = rt_tick_get();
    g_pet.voice_stop_deadline = 0;
    g_pet.release_pending = 0;
    g_pet.state = VB_PET_RECORDING;
    g_pet.dirty = 1;
}

static void vb_pet_cancel_voice(void)
{
    if (g_pet.state != VB_PET_RECORDING) return;
    if (g_pet.ops.voice_clear) g_pet.ops.voice_clear();
    g_pet.release_pending = 0;
    g_pet.voice_stop_deadline = 0;
    g_pet.asr_deadline = 0;
    g_pet.state = VB_PET_IDLE;
    g_pet.transcript[0] = '\0';
    g_pet.error[0] = '\0';
    g_pet.dirty = 1;
    rt_kprintf("[vb_runtime][codex_pet] voice capture cancelled\n");
}

static void vb_pet_finish_voice(uint32_t now)
{
    int result;
    if (!g_pet.ops.voice_stop) return;
    result = g_pet.ops.voice_stop();
    g_pet.release_pending = 0;
    g_pet.voice_stop_deadline = 0;
    if (result == RT_EOK)
    {
        g_pet.state = VB_PET_TRANSCRIBING;
        g_pet.asr_deadline = now + rt_tick_from_millisecond(VB_PET_ASR_TIMEOUT_MS);
    }
    else
    {
        rt_snprintf(g_pet.error, sizeof(g_pet.error), "Voice stop error (%d)", result);
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
    }
    g_pet.dirty = 1;
}

static void vb_pet_release_voice(uint32_t now)
{
    if (g_pet.state != VB_PET_RECORDING || g_pet.release_pending) return;
    g_pet.voice_stop_deadline = g_pet.voice_started_at +
        rt_tick_from_millisecond(VB_PET_MIN_VOICE_MS);
    if ((int32_t)(now - g_pet.voice_stop_deadline) < 0)
    {
        g_pet.release_pending = 1;
        g_pet.dirty = 1;
        return;
    }
    vb_pet_finish_voice(now);
}

static void vb_pet_talk_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point = {0, 0};
    if (code == LV_EVENT_PRESSED)
    {
        indev = lv_event_get_indev(event);
        if (!indev) indev = lv_indev_get_act();
        if (indev) lv_indev_get_point(indev, &point);
        g_pet.press_y = point.y;
        vb_pet_begin_voice();
    }
    else if (code == LV_EVENT_PRESSING && g_pet.state == VB_PET_RECORDING)
    {
        indev = lv_event_get_indev(event);
        if (!indev) indev = lv_indev_get_act();
        if (!indev) return;
        lv_indev_get_point(indev, &point);
        if (point.y <= VB_PET_CANCEL_Y && g_pet.press_y - point.y >= VB_PET_CANCEL_DY)
            vb_pet_cancel_voice();
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        vb_pet_release_voice(rt_tick_get());
    }
}

static void vb_pet_new_event(lv_event_t *event)
{
    int result;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (g_pet.approval_pending)
    {
        result = g_pet.ops.send_action ?
            g_pet.ops.send_action("approve", g_pet.approval_id) : -RT_ENOSYS;
        if (result == RT_EOK)
        {
            g_pet.error[0] = '\0';
            vb_pet_copy(g_pet.approval_summary, sizeof(g_pet.approval_summary), "Sending approval...");
        }
        else
        {
            vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Bridge unavailable; approval not sent");
        }
        g_pet.dirty = 1;
        return;
    }
    result = g_pet.ops.send_action ? g_pet.ops.send_action("prev", "tasks") : -RT_ENOSYS;
    if (result != RT_EOK)
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Bridge unavailable");
    g_pet.dirty = 1;
}

static void vb_pet_continue_event(lv_event_t *event)
{
    int result;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (g_pet.approval_pending)
    {
        result = g_pet.ops.send_action ?
            g_pet.ops.send_action("deny", g_pet.approval_id) : -RT_ENOSYS;
        if (result == RT_EOK)
        {
            g_pet.error[0] = '\0';
            vb_pet_copy(g_pet.approval_summary, sizeof(g_pet.approval_summary), "Sending denial...");
        }
        else
        {
            vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Bridge unavailable; denial not sent");
        }
        g_pet.dirty = 1;
        return;
    }
    result = g_pet.ops.send_action ? g_pet.ops.send_action("next", "tasks") : -RT_ENOSYS;
    if (result != RT_EOK)
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Bridge unavailable");
    g_pet.dirty = 1;
}

static int vb_pet_json_ttl(const char *payload)
{
    const char *ttl = payload ? strstr(payload, "\"l\":") : RT_NULL;
    long value;
    if (!ttl) return VB_PET_HEARTBEAT_TTL_MS;
    value = strtol(ttl + 4, RT_NULL, 10);
    if (value < 1) value = 1;
    if (value > VB_PET_HEARTBEAT_TTL_MS) value = VB_PET_HEARTBEAT_TTL_MS;
    return (int)value;
}

static void vb_pet_receive_state(uint32_t sequence, const char *payload)
{
    uint32_t now = rt_tick_get();
    vb_pet_state_t previous;
    if (!payload || !strstr(payload, "\"v\":\"pet/v1\"")) return;
    if (!vb_pet_sequence_newer(sequence, g_pet.host_sequence)) return;
    g_pet.host_sequence = sequence;
    g_pet.host_deadline = now + rt_tick_from_millisecond(vb_pet_json_ttl(payload));
    previous = g_pet.state;
    if (vb_pet_local_voice_active())
    {
        g_pet.dirty = 1;
        return;
    }
    if (strstr(payload, "\"q\":\"")) g_pet.have_thread = 1;
    if (strstr(payload, "\"st\":\"u\"")) g_pet.state = VB_PET_RUNNING;
    else if (strstr(payload, "\"st\":\"n\"")) g_pet.state = VB_PET_NEEDS_INPUT;
    else if (strstr(payload, "\"st\":\"b\"")) g_pet.state = VB_PET_ERROR;
    else if (strstr(payload, "\"st\":\"y\"")) g_pet.state = VB_PET_READY;
    else if (strstr(payload, "\"st\":\"x\"")) g_pet.state = VB_PET_DISCONNECTED;
    else if (strstr(payload, "\"st\":\"c\"") &&
             g_pet.state == VB_PET_DISCONNECTED) g_pet.state = VB_PET_IDLE;
    if (g_pet.state != previous)
    {
        if (g_pet.state == VB_PET_NEEDS_INPUT) vb_pet_play_cue("needs_input");
        else if (g_pet.state == VB_PET_READY) vb_pet_play_cue("done");
        else if (g_pet.state == VB_PET_ERROR) vb_pet_play_cue("error");
    }
    g_pet.dirty = 1;
}

static void vb_pet_receive_approval(uint32_t sequence, const char *payload)
{
    char request_id[VB_PET_APPROVAL_ID_MAX];
    char status[16];
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.approval_sequence)) return;
    g_pet.approval_sequence = sequence;
    if (!vb_pet_json_string(payload, "id", request_id, sizeof(request_id))) return;
    if (vb_pet_json_string(payload, "status", status, sizeof(status)))
    {
        if (rt_strcmp(status, "failed") == 0)
        {
            vb_pet_copy(g_pet.approval_summary, sizeof(g_pet.approval_summary),
                        "Use computer to approve");
            g_pet.state = VB_PET_NEEDS_INPUT;
        }
        else if (!g_pet.approval_pending || rt_strcmp(request_id, g_pet.approval_id) == 0)
        {
            vb_pet_clear_approval();
        }
        g_pet.dirty = 1;
        return;
    }
    vb_pet_copy(g_pet.approval_id, sizeof(g_pet.approval_id), request_id);
    if (!vb_pet_json_string(payload, "summary", g_pet.approval_summary,
                            sizeof(g_pet.approval_summary)))
        vb_pet_copy(g_pet.approval_summary, sizeof(g_pet.approval_summary), "Approval requested");
    g_pet.approval_pending = 1;
    g_pet.error[0] = '\0';
    g_pet.state = VB_PET_NEEDS_INPUT;
    g_pet.dirty = 1;
}

static void vb_pet_receive_tasks(uint32_t sequence, const char *payload)
{
    char status[20];
    char request_id[VB_PET_APPROVAL_ID_MAX];
    vb_pet_state_t previous;
    int approval;
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.task_sequence)) return;
    if (vb_pet_json_int(payload, "\"v\"", 0) != 1) return;
    g_pet.task_sequence = sequence;
    previous = g_pet.state;
    (void)vb_pet_json_string(payload, "p", g_pet.project, sizeof(g_pet.project));
    if (!vb_pet_json_string(payload, "d", g_pet.task_detail, sizeof(g_pet.task_detail)))
        vb_pet_copy(g_pet.task_detail, sizeof(g_pet.task_detail), "Codex task updated");
    g_pet.task_index = vb_pet_json_int(payload, "\"i\"", 0);
    g_pet.task_count = vb_pet_json_int(payload, "\"n\"", 0);
    approval = vb_pet_json_int(payload, "\"a\"", 0);
    if (!approval)
    {
        vb_pet_clear_approval();
    }
    else if (vb_pet_json_string(payload, "r", request_id, sizeof(request_id)))
    {
        vb_pet_copy(g_pet.approval_id, sizeof(g_pet.approval_id), request_id);
        vb_pet_copy(g_pet.approval_summary, sizeof(g_pet.approval_summary),
                    "Codex needs approval");
        g_pet.approval_pending = 1;
    }
    if (vb_pet_json_string(payload, "st", status, sizeof(status)))
    {
        if (rt_strcmp(status, "running") == 0) g_pet.state = VB_PET_RUNNING;
        else if (rt_strcmp(status, "needs_input") == 0) g_pet.state = VB_PET_NEEDS_INPUT;
        else if (rt_strcmp(status, "blocked") == 0) g_pet.state = VB_PET_ERROR;
        else if (rt_strcmp(status, "ready") == 0) g_pet.state = VB_PET_READY;
        else if (rt_strcmp(status, "connected") == 0) g_pet.state = VB_PET_IDLE;
    }
    g_pet.error[0] = '\0';
    if (g_pet.state != previous)
    {
        if (g_pet.state == VB_PET_NEEDS_INPUT) vb_pet_play_cue("needs_input");
        else if (g_pet.state == VB_PET_READY) vb_pet_play_cue("done");
        else if (g_pet.state == VB_PET_ERROR) vb_pet_play_cue("error");
    }
    g_pet.dirty = 1;
}

int vb_codex_pet_start(lv_obj_t *root, const vb_codex_pet_ops_t *ops,
                       const char *project)
{
    lv_obj_t *label;
    if (!root || !ops || !ops->send_action) return -RT_EINVAL;
    vb_codex_pet_stop();
    rt_memset(&g_pet, 0, sizeof(g_pet));
    g_pet.root = root;
    g_pet.ops = *ops;
    g_pet.active = 1;
    g_pet.state = VB_PET_DISCONNECTED;
    g_pet.rocky_frame_key = -1;
    vb_pet_copy(g_pet.project, sizeof(g_pet.project), project);

    lv_obj_clean(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0b1118), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(root, lv_color_hex(0xf9fafb), LV_PART_MAIN | LV_STATE_DEFAULT);

    label = vb_pet_label(root, "Codex Companion", 0xf9fafb);
    lv_obj_set_pos(label, 30, 24);
    g_pet.project_label = vb_pet_label(root, "Project unavailable", 0xd1d5db);
    lv_obj_set_width(g_pet.project_label, 205);
    lv_obj_set_pos(g_pet.project_label, 30, 52);
    g_pet.connection_label = vb_pet_label(root, "Bridge offline", 0x94a3b8);
    lv_obj_set_width(g_pet.connection_label, 130);
    lv_obj_set_style_text_align(g_pet.connection_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_pet.connection_label, 240, 52);

    g_pet.pet_tail = lv_obj_create(root);
    lv_obj_set_size(g_pet.pet_tail, 58, 22);
    lv_obj_set_pos(g_pet.pet_tail, 232, 178);
    lv_obj_set_style_radius(g_pet.pet_tail, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.pet_tail, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.pet_tail, LV_OBJ_FLAG_SCROLLABLE);

    g_pet.pet_body = lv_obj_create(root);
    lv_obj_set_size(g_pet.pet_body, 104, 80);
    lv_obj_set_pos(g_pet.pet_body, 143, 158);
    lv_obj_set_style_radius(g_pet.pet_body, 38, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.pet_body, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.pet_body, LV_OBJ_FLAG_SCROLLABLE);

    g_pet.left_ear = lv_obj_create(root);
    lv_obj_set_size(g_pet.left_ear, 41, 48);
    lv_obj_set_pos(g_pet.left_ear, 132, 95);
    lv_obj_set_style_radius(g_pet.left_ear, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.left_ear, lv_color_hex(0xb8cbd3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.left_ear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.left_ear, LV_OBJ_FLAG_SCROLLABLE);
    g_pet.right_ear = lv_obj_create(root);
    lv_obj_set_size(g_pet.right_ear, 41, 48);
    lv_obj_set_pos(g_pet.right_ear, 217, 95);
    lv_obj_set_style_radius(g_pet.right_ear, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.right_ear, lv_color_hex(0xb8cbd3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.right_ear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.right_ear, LV_OBJ_FLAG_SCROLLABLE);

    g_pet.pet_face = lv_obj_create(root);
    lv_obj_set_size(g_pet.pet_face, 120, 110);
    lv_obj_set_pos(g_pet.pet_face, 135, 103);
    lv_obj_set_style_radius(g_pet.pet_face, 42, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.pet_face, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.pet_face, LV_OBJ_FLAG_SCROLLABLE);
    g_pet.left_eye = lv_obj_create(g_pet.pet_face);
    lv_obj_set_size(g_pet.left_eye, 11, 18);
    lv_obj_set_pos(g_pet.left_eye, 32, 38);
    lv_obj_set_style_radius(g_pet.left_eye, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.left_eye, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.left_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    g_pet.right_eye = lv_obj_create(g_pet.pet_face);
    lv_obj_set_size(g_pet.right_eye, 11, 18);
    lv_obj_set_pos(g_pet.right_eye, 76, 38);
    lv_obj_set_style_radius(g_pet.right_eye, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.right_eye, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.right_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    g_pet.mouth = vb_pet_label(g_pet.pet_face, "-", 0x111827);
    lv_obj_set_width(g_pet.mouth, 28);
    lv_obj_set_style_text_align(g_pet.mouth, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_pet.mouth, 46, 70);

    g_pet.rocky_available = vb_pet_load_rocky_frames();
    if (g_pet.rocky_available)
    {
        g_pet.pet_image = lv_img_create(root);
        lv_obj_set_pos(g_pet.pet_image, 105, 72);
        lv_img_set_pivot(g_pet.pet_image, 80, 86);
        lv_img_set_zoom(g_pet.pet_image, 288);
        lv_img_set_antialias(g_pet.pet_image, false);
        lv_obj_clear_flag(g_pet.pet_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_pet.pet_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.pet_tail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.left_ear, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.right_ear, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.pet_face, LV_OBJ_FLAG_HIDDEN);
        vb_pet_update_rocky(0);
    }

    g_pet.status_label = vb_pet_label(root, "Disconnected", 0x94a3b8);
    lv_obj_set_width(g_pet.status_label, 330);
    lv_obj_set_style_text_align(g_pet.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_pet.status_label, 30, 251);
    g_pet.transcript_label = vb_pet_label(root, "No active Codex tasks", 0xf9fafb);
    lv_obj_set_size(g_pet.transcript_label, 330, 58);
    lv_obj_set_pos(g_pet.transcript_label, 30, 280);
    lv_obj_set_style_text_align(g_pet.transcript_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_pet.transcript_label, LV_LABEL_LONG_WRAP);
    g_pet.task_label = vb_pet_label(root, "No active tasks", 0x94a3b8);
    lv_obj_set_width(g_pet.task_label, 330);
    lv_obj_set_style_text_align(g_pet.task_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_pet.task_label, 30, 338);

    g_pet.new_button = vb_pet_button(root, "<", 30, 376, 160, 54,
                                     0x243244, vb_pet_new_event);
    g_pet.new_label = lv_obj_get_child(g_pet.new_button, 0);
    g_pet.continue_button = vb_pet_button(root, ">", 200, 376, 160, 54,
                                          0x243244, vb_pet_continue_event);
    g_pet.continue_label = lv_obj_get_child(g_pet.continue_button, 0);
    vb_pet_render();
    vb_pet_rgb_tick(rt_tick_get());
    return RT_EOK;
}

void vb_codex_pet_stop(void)
{
    if (!g_pet.active) return;
    if ((g_pet.state == VB_PET_RECORDING || g_pet.state == VB_PET_TRANSCRIBING) &&
        g_pet.ops.voice_clear) g_pet.ops.voice_clear();
    if (g_pet.ops.rgb_set) (void)g_pet.ops.rgb_set("off");
    if (g_pet.pet_image) lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_HIDDEN);
    vb_pet_release_rocky_frames();
    g_pet.active = 0;
    rt_memset(&g_pet, 0, sizeof(g_pet));
}

void vb_codex_pet_receive_flow(const char *channel, uint32_t sequence,
                               const char *payload)
{
    if (!g_pet.active || !channel) return;
    if (rt_strcmp(channel, "pet.project") == 0)
    {
        vb_pet_copy(g_pet.project, sizeof(g_pet.project), payload);
        g_pet.dirty = 1;
    }
    else if (rt_strcmp(channel, "pet.resume") == 0)
    {
        g_pet.have_thread = 1;
        g_pet.dirty = 1;
    }
    else if (rt_strcmp(channel, "pet.transcript") == 0 && sequence == g_pet.voice_sequence)
    {
        vb_pet_copy(g_pet.transcript, sizeof(g_pet.transcript), payload);
        g_pet.state = VB_PET_RUNNING;
        g_pet.dirty = 1;
    }
    else if (rt_strcmp(channel, "pet.task.ack") == 0 && sequence == g_pet.voice_sequence)
    {
        vb_pet_copy(g_pet.task, sizeof(g_pet.task), payload);
        g_pet.have_thread = 1;
        g_pet.state = VB_PET_RUNNING;
        g_pet.error[0] = '\0';
        vb_pet_play_cue("submitted");
        g_pet.dirty = 1;
    }
    else if (rt_strcmp(channel, "pet.quota") == 0)
    {
        vb_pet_receive_quota(sequence, payload);
    }
    else if (rt_strcmp(channel, "pet.approval") == 0)
    {
        vb_pet_receive_approval(sequence, payload);
    }
    else if (rt_strcmp(channel, "pet.tasks") == 0)
    {
        vb_pet_receive_tasks(sequence, payload);
    }
    else if (rt_strcmp(channel, "codex.mcp") == 0)
    {
        vb_pet_copy(g_pet.task, sizeof(g_pet.task), payload);
        g_pet.error[0] = '\0';
        g_pet.dirty = 1;
    }
    else if ((rt_strcmp(channel, "pet.asr.error") == 0 ||
              rt_strcmp(channel, "pet.task.error") == 0) &&
             sequence == g_pet.voice_sequence)
    {
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), payload);
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
        g_pet.dirty = 1;
    }
    else if (rt_strcmp(channel, "pet.state") == 0 ||
             rt_strcmp(channel, "pet.heartbeat") == 0)
    {
        vb_pet_receive_state(sequence, payload);
    }
}

void vb_codex_pet_tick(uint32_t now)
{
#if VB_PET_VOICE_UI_ENABLED
    vb_codex_pet_voice_snapshot_t snapshot;
    int key2 = 0;
#endif
    uint32_t animation_phase;
    if (!g_pet.active) return;
#if VB_PET_VOICE_UI_ENABLED
    vb_pet_voice_snapshot(&snapshot);
    if (g_pet.ops.key2_pressed) key2 = g_pet.ops.key2_pressed();
    if (key2 && !g_pet.key2_last) vb_pet_begin_voice();
    else if (!key2 && g_pet.key2_last) vb_pet_release_voice(now);
    g_pet.key2_last = key2;

    if (g_pet.state == VB_PET_RECORDING && g_pet.release_pending &&
        snapshot.recording && (int32_t)(now - g_pet.voice_stop_deadline) >= 0 &&
        (snapshot.bytes > 0 ||
         (int32_t)(now - (g_pet.voice_started_at +
                          rt_tick_from_millisecond(VB_PET_STARTUP_GRACE_MS))) >= 0))
        vb_pet_finish_voice(now);
    if (g_pet.state == VB_PET_RECORDING && !snapshot.recording)
    {
        if (snapshot.ready && snapshot.bytes > 0)
        {
            g_pet.state = VB_PET_TRANSCRIBING;
            g_pet.asr_deadline = now + rt_tick_from_millisecond(VB_PET_ASR_TIMEOUT_MS);
        }
        else
        {
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "No audio captured");
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
        }
        g_pet.dirty = 1;
    }
    if (g_pet.state == VB_PET_TRANSCRIBING && snapshot.error < 0 &&
        !snapshot.recording && !snapshot.ready)
    {
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Voice capture failed");
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
        g_pet.dirty = 1;
    }
    if (g_pet.state == VB_PET_TRANSCRIBING && g_pet.asr_deadline &&
        (int32_t)(now - g_pet.asr_deadline) >= 0)
    {
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "ASR bridge timeout");
        g_pet.state = VB_PET_ERROR;
        vb_pet_play_cue("error");
        g_pet.asr_deadline = 0;
        g_pet.dirty = 1;
    }
#endif
    if (g_pet.host_deadline && (int32_t)(now - g_pet.host_deadline) >= 0)
    {
        if ((g_pet.state == VB_PET_RECORDING || g_pet.state == VB_PET_TRANSCRIBING) &&
            g_pet.ops.voice_clear) g_pet.ops.voice_clear();
        g_pet.host_deadline = 0;
        g_pet.host_sequence = 0;
        vb_pet_clear_approval();
        g_pet.state = VB_PET_DISCONNECTED;
        g_pet.dirty = 1;
    }
    animation_phase = (now / rt_tick_from_millisecond(
        g_pet.state == VB_PET_RUNNING ? 260 : 720)) & 1u;
    if (animation_phase != g_pet.animation_phase)
    {
        g_pet.animation_phase = animation_phase;
        if (g_pet.rocky_available)
        {
            vb_pet_update_rocky(animation_phase);
        }
        else
        {
            if (g_pet.pet_face)
                lv_obj_set_y(g_pet.pet_face, animation_phase ? 96 : 99);
            if (g_pet.pet_tail)
                lv_obj_set_pos(g_pet.pet_tail, animation_phase ? 235 : 230,
                               animation_phase ? 164 : 169);
        }
    }
    vb_pet_rgb_tick(now);
    if (g_pet.dirty) vb_pet_render();
}

int vb_codex_pet_active(void)
{
    return g_pet.active;
}
