#include "vb_runtime_codex_pet.h"
#include "vb_runtime_storage.h"
#include "app_mem.h"
#include "lv_ext_resource_manager.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dfs_posix.h>
#include <zlib.h>

#define VB_PET_HOLD_CONTEXT_NEW "pet.new"
#define VB_PET_HOLD_CONTEXT_CONTINUE "pet.continue"
#define VB_PET_ASR_TIMEOUT_MS 120000
#define VB_PET_MIN_VOICE_MS 700
#define VB_PET_STARTUP_GRACE_MS 1500
#define VB_PET_CANCEL_Y 320
#define VB_PET_CANCEL_DY 60
#define VB_PET_HEARTBEAT_TTL_MS 30000
#define VB_PET_RECONNECT_AFTER_MS 12000
#define VB_PET_TEXT_MAX 193
#define VB_PET_PROJECT_MAX 64
#define VB_PET_QUOTA_MAX 96
#define VB_PET_APPROVAL_ID_MAX 25
#define VB_PET_APPROVAL_SUMMARY_MAX 49
#define VB_PET_LISTENING_CUE_MS 100
#define VB_PET_VOICE_UI_ENABLED 0
#define VB_PET_ROCKY_DIR "/sdcard/apps/codex_pet/assets/rocky"
#define VB_PET_ASSET_ROOT "/sdcard/apps/codex_pet/assets/pets"
#define VB_PET_CATALOG_PATH VB_PET_ASSET_ROOT "/catalog.txt"
#define VB_PET_MAX_ASSETS 1
#define VB_PET_MAX_ASSET_FRAMES 8
#define VB_PET_ASSET_SLUG_MAX 33
#define VB_PET_ASSET_NAME_MAX 33
#define VB_PET_ASSET_CATALOG_MAX 1024
#define VB_PET_ASSET_STATE_COUNT 9
#define VB_PET_ROCKY_RLE_MAGIC 0x454c5256u
#define VB_PET_ROCKY_RLE_VERSION 1u
#define VB_PET_ROCKY_RLE_HEADER_SIZE 20u
#define VB_PET_RLE_RECORD_BATCH 800u
#define VB_PET_PRELOAD_PATH VB_PET_ASSET_ROOT "/preload.bin"
#define VB_PET_PRELOAD_STATE_PATH_FORMAT VB_PET_ASSET_ROOT "/state%d.bin"
#define VB_PET_PRELOAD_MAGIC 0x43504256u
#define VB_PET_PRELOAD_LEGACY_VERSION 1u
#define VB_PET_PRELOAD_VERSION 2u
#define VB_PET_PRELOAD_HEADER_SIZE 16u
#define VB_PET_PRELOAD_STATE_ENTRY_SIZE 12u
#define VB_PET_TASK_RESIDENT_STATE_COUNT 5
#define VB_PET_PRELOAD_LEGACY_STATE_COUNT 5u
#define VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE 2u
#define VB_PET_STATUS_API "vibeboard-huangshan-codex-pet/v1"
#define VB_PET_PRELOAD_STORAGE_WAIT_MS 3000
#define VB_PET_PRELOAD_MAX_BYTES (1400000u)
#define VB_PET_PRELOAD_MAX_COMPRESSED_BYTES (1024u * 1024u)
#define VB_PET_PRELOAD_MAX_RESIDENT_COMPRESSED_BYTES (768u * 1024u)
#define VB_PET_PRELOAD_CACHE_BANKS 2
#define VB_PET_PRELOAD_THREAD_STACK 4096
#define VB_PET_STARTUP_ANIMATION_DELAY_MS 2500
#define VB_PET_FLOW_QUEUE_SIZE 16
#define VB_PET_FLOW_CHANNEL_MAX 25
#define VB_PET_FLOW_PAYLOAD_MAX 193
#define VB_PET_RUNTIME_FRAME_LIMIT VB_PET_MAX_ASSET_FRAMES
#define VB_PET_PRELOAD_IO_CHUNK_BYTES (8u * 1024u)
#define VB_PET_NATIVE_FRAME_MS 120
#define VB_PET_LEGACY_FRAME_MS 180
#define VB_PET_IMAGE_X 115
#define VB_PET_IMAGE_Y 115
#define VB_PET_IMAGE_ZOOM 360
#define VB_PET_ACTION_Y 369
#define VB_PET_ACTION_WIDTH 150
#define VB_PET_ACTION_HEIGHT 44
#define VB_PET_ACTION_LEFT_X 35
#define VB_PET_ACTION_RIGHT_X 205
#define VB_PET_STATUS_Y 324
#define VB_PET_TRANSCRIPT_Y 346
#define VB_PET_TRANSCRIPT_HEIGHT 40
#define VB_PET_TASK_LABEL_FULL_Y 392
#define VB_PET_TASK_LABEL_COMPACT_Y 346
#define VB_PET_SWIPE_ZONE_TOP 80
#define VB_PET_SWIPE_ZONE_BOTTOM 324
#define VB_PET_SWIPE_MIN_DX 28
#define VB_PET_SWIPE_MAX_DY 96
#define VB_PET_SWIPE_MIN_DY 64
#define VB_PET_SWIPE_MAX_DX 72
#define VB_PET_TOP_EDGE_MAX_Y 72
#define VB_PET_EDGE_BACK_X 72
#define VB_PET_IDLE_MIN_MS 45000
#define VB_PET_IDLE_RANGE_MS 75001
#define VB_PET_NOTICE_MS 8000
#define VB_PET_USAGE_CLOCK_REFRESH_MS 60000
#define VB_PET_USAGE_LEFT 48
#define VB_PET_USAGE_WIDTH 294
#define VB_PET_USAGE_TITLE_WIDTH 205
#define VB_PET_USAGE_STATUS_X 269
#define VB_PET_USAGE_STATUS_Y 44
#define VB_PET_USAGE_STATUS_WIDTH 72
#define VB_PET_USAGE_TITLE_Y 40
#define VB_PET_USAGE_HERO_Y 78
#define VB_PET_USAGE_UNIT_Y 116
#define VB_PET_USAGE_META_Y 140
#define VB_PET_USAGE_CONTEXT_Y 180
#define VB_PET_USAGE_CONTEXT_BAR_Y 208
#define VB_PET_USAGE_CONTEXT_BAR_HEIGHT 14
#define VB_PET_USAGE_METRIC_LABEL_Y 248
#define VB_PET_USAGE_METRIC_VALUE_Y 272
#define VB_PET_USAGE_METRIC_COLUMN_WIDTH 86
#define VB_PET_USAGE_METRIC_NEW_X 48
#define VB_PET_USAGE_METRIC_CACHED_X 152
#define VB_PET_USAGE_METRIC_OUTPUT_X 255
#define VB_PET_USAGE_FOOTER_Y 382
#define VB_PET_SUMMARY_BAR_Y 190
#define VB_PET_SUMMARY_BAR_HEIGHT 76
#define VB_PET_SUMMARY_BAR_WIDTH 20
#define VB_PET_SUMMARY_BAR_GAP 22
#define VB_PET_SUMMARY_DAY_Y 272

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

typedef enum
{
    VB_PET_ASSET_IDLE = 0,
    VB_PET_ASSET_RUN_RIGHT,
    VB_PET_ASSET_RUN_LEFT,
    VB_PET_ASSET_WAVING,
    VB_PET_ASSET_JUMPING,
    VB_PET_ASSET_FAILED,
    VB_PET_ASSET_WAITING,
    VB_PET_ASSET_RUNNING,
    VB_PET_ASSET_REVIEW
} vb_pet_asset_state_t;

static const uint8_t g_vb_pet_task_resident_states[VB_PET_TASK_RESIDENT_STATE_COUNT] = {
    VB_PET_ASSET_IDLE,
    VB_PET_ASSET_WAVING,
    VB_PET_ASSET_FAILED,
    VB_PET_ASSET_WAITING,
    VB_PET_ASSET_RUNNING,
};

typedef enum
{
    VB_PET_PAGE_HOME = 0,
    VB_PET_PAGE_USAGE_CURRENT,
    VB_PET_PAGE_USAGE_SUMMARY
} vb_pet_page_t;

typedef struct
{
    int active;
    vb_pet_page_t page;
    int continue_mode;
    int have_thread;
    int key2_last;
    int release_pending;
    int press_y;
    int dirty;
    vb_pet_state_t state;
    vb_pet_state_t task_state;
    uint32_t voice_sequence;
    uint32_t voice_started_at;
    uint32_t voice_stop_deadline;
    uint32_t asr_deadline;
    uint32_t host_deadline;
    uint32_t host_seen_at;
    uint32_t sync_label_updated_at;
    uint32_t host_sequence;
    uint32_t quota_sequence;
    uint32_t usage_sequence;
    uint32_t usage_summary_sequence;
    uint32_t progress_sequence;
    uint32_t achievement_sequence;
    uint32_t cue_sequence;
    uint32_t rgb_phase;
    int quota_live;
    int quota_auth_required;
    int quota_primary_used;
    int quota_secondary_used;
    int quota_primary_window_minutes;
    int quota_secondary_window_minutes;
    int quota_primary_reset_seconds;
    int quota_secondary_reset_seconds;
    uint32_t quota_received_at;
    uint32_t quota_rendered_at;
    int usage_live;
    int usage_cost_valid;
    uint64_t usage_total_tokens;
    uint64_t usage_context_tokens;
    uint64_t usage_context_window;
    uint64_t usage_uncached_input_tokens;
    uint64_t usage_cached_input_tokens;
    uint64_t usage_output_tokens;
    uint64_t usage_turn_tokens;
    uint64_t usage_cost_microusd;
    uint64_t usage_turn_cost_microusd;
    char usage_model[25];
    int usage_summary_live;
    int usage_summary_cost_complete;
    int usage_summary_cost_trend;
    uint64_t usage_summary_today_tokens;
    uint64_t usage_summary_today_cost;
    uint64_t usage_summary_trend[7];
    int progress_live;
    int progress_level;
    uint64_t progress_xp;
    uint64_t progress_next_xp;
    int progress_today_tasks;
    int progress_today_active_seconds;
    int progress_streak;
    char progress_mood[16];
    char progress_notice[65];
    uint32_t progress_notice_until;
    uint32_t ready_idle_at;
    char last_cue_id[17];
    uint32_t idle_next_at;
    int idle_last_asset;
    int idle_transient;
    int approval_pending;
    uint32_t approval_sequence;
    uint32_t task_sequence;
    uint32_t animation_phase;
    int task_index;
    int task_count;
    int active_task_count;
    int rocky_available;
    int custom_available;
    int rocky_frame_key;
    int custom_state;
    int custom_frame_count;
    int custom_frame_index;
    int custom_displayed_frame;
    int custom_frame_ms;
    uint8_t *preloaded_data;
    uint8_t *preload_resident_compressed;
    uint32_t preloaded_data_size;
    uint32_t preload_raw_frame_size;
    uint32_t preload_resident_compressed_bytes;
    uint32_t preload_resident_offsets[VB_PET_ASSET_STATE_COUNT];
    uint32_t preload_resident_lengths[VB_PET_ASSET_STATE_COUNT];
    uint32_t preload_state_offsets[VB_PET_ASSET_STATE_COUNT];
    uint32_t preload_state_lengths[VB_PET_ASSET_STATE_COUNT];
    uint32_t preload_legacy_offsets[VB_PET_PRELOAD_LEGACY_STATE_COUNT]
                                      [VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE];
    uint32_t preload_legacy_lengths[VB_PET_PRELOAD_LEGACY_STATE_COUNT]
                                      [VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE];
    uint8_t preload_state_frame_counts[VB_PET_ASSET_STATE_COUNT];
    uint8_t preload_version;
    uint8_t preload_max_frames;
    uint8_t preload_split;
    int cache_state[VB_PET_PRELOAD_CACHE_BANKS];
    uint8_t cache_frame_counts[VB_PET_PRELOAD_CACHE_BANKS];
    int active_cache_bank;
    int requested_asset_state;
    volatile uint32_t loader_request_sequence;
    volatile uint32_t loader_completed_sequence;
    uint32_t loader_applied_sequence;
    volatile int loader_request_state;
    volatile int loader_request_bank;
    volatile int loader_completed_state;
    volatile int loader_completed_bank;
    volatile int loader_completed_result;
    volatile int loader_stop;
    rt_thread_t loader_thread;
    rt_sem_t loader_sem;
    int preview_asset_state;
    int transient_asset_state;
    int transient_started;
    uint32_t ui_tick_count;
    int pet_index;
    int pet_count;
    volatile int pending_pet_selection;
    int pending_pet_attempts;
    uint32_t custom_next_frame_at;
    uint32_t startup_transient_at;
    uint32_t pending_pet_retry_at;
    int touch_press_x;
    int touch_press_y;
    int touch_swipe_consumed;
    char rgb_color[8];
    char project[VB_PET_PROJECT_MAX];
    char quota[VB_PET_QUOTA_MAX];
    char transcript[VB_PET_TEXT_MAX];
    char task[VB_PET_TEXT_MAX];
    char error[97];
    char approval_id[VB_PET_APPROVAL_ID_MAX];
    char approval_summary[VB_PET_APPROVAL_SUMMARY_MAX];
    char task_detail[VB_PET_TEXT_MAX];
    char pet_slug[VB_PET_ASSET_SLUG_MAX];
    char pet_name[VB_PET_ASSET_NAME_MAX];
    char pending_pet_slug[VB_PET_ASSET_SLUG_MAX];
    char pet_slugs[VB_PET_MAX_ASSETS][VB_PET_ASSET_SLUG_MAX];
    char pet_names[VB_PET_MAX_ASSETS][VB_PET_ASSET_NAME_MAX];
    lv_obj_t *root;
    lv_obj_t *title_label;
    lv_obj_t *connection_label;
    lv_obj_t *pet_face;
    lv_obj_t *pet_body;
    lv_obj_t *pet_tail;
    lv_obj_t *left_ear;
    lv_obj_t *right_ear;
    lv_obj_t *pet_image;
    lv_img_dsc_t *rocky_frames[5][2];
    lv_img_dsc_t preloaded_frames[VB_PET_PRELOAD_CACHE_BANKS][VB_PET_MAX_ASSET_FRAMES];
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;
    lv_obj_t *mouth;
    lv_obj_t *status_label;
    lv_obj_t *transcript_label;
    lv_obj_t *task_label;
    lv_obj_t *quota_label;
    lv_obj_t *quota_title_label;
    lv_obj_t *quota_status_label;
    lv_obj_t *quota_primary_label;
    lv_obj_t *quota_primary_value_label;
    lv_obj_t *quota_primary_bar;
    lv_obj_t *quota_primary_fill;
    lv_obj_t *quota_primary_reset_label;
    lv_obj_t *quota_secondary_label;
    lv_obj_t *quota_footer_label;
    lv_obj_t *usage_new_label;
    lv_obj_t *usage_new_value;
    lv_obj_t *usage_cached_label;
    lv_obj_t *usage_cached_value;
    lv_obj_t *usage_output_label;
    lv_obj_t *usage_output_value;
    lv_obj_t *summary_bars[7];
    lv_obj_t *summary_day_labels[7];
    lv_obj_t *new_button;
    lv_obj_t *new_label;
    lv_obj_t *continue_button;
    lv_obj_t *continue_label;
    lv_obj_t *talk_button;
    lv_obj_t *talk_label;
    lv_obj_t *cancel_target;
    vb_codex_pet_ops_t ops;
} vb_codex_pet_state_t;

typedef struct
{
    uint32_t sequence;
    char channel[VB_PET_FLOW_CHANNEL_MAX];
    char payload[VB_PET_FLOW_PAYLOAD_MAX];
} vb_pet_flow_message_t;

typedef struct
{
    int active;
    vb_pet_state_t state;
    uint32_t host_seen_at;
    int task_index;
    int task_count;
    int active_task_count;
    int approval_pending;
    int pet_index;
    int pet_count;
    int custom_available;
    int custom_frame_count;
    int custom_frame_index;
    int custom_frame_ms;
    int custom_state;
    int requested_asset_state;
    int preload_version;
    int asset_state_count;
    uint32_t preloaded_data_size;
    uint32_t preload_resident_compressed_bytes;
    uint32_t ui_tick_count;
    uint32_t queued_flows;
    uint32_t dropped_flows;
    char pet_slug[VB_PET_ASSET_SLUG_MAX];
    char rgb_color[8];
} vb_pet_status_snapshot_t;

static vb_codex_pet_state_t g_pet;
static uint8_t g_vb_pet_rle_records[VB_PET_RLE_RECORD_BATCH * 5u];
static uint8_t g_vb_pet_preload_io[VB_PET_PRELOAD_IO_CHUNK_BYTES];
static vb_pet_flow_message_t g_vb_pet_flow_queue[VB_PET_FLOW_QUEUE_SIZE];
static volatile uint32_t g_vb_pet_flow_read;
static volatile uint32_t g_vb_pet_flow_write;
static volatile uint32_t g_vb_pet_flow_drops;
static volatile int g_vb_pet_flow_active;
static volatile int g_vb_pet_loader_phase;
static vb_pet_status_snapshot_t g_vb_pet_status;

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
        uint32_t remaining = size - used;
        uint32_t wanted = remaining > VB_PET_PRELOAD_IO_CHUNK_BYTES ?
                          VB_PET_PRELOAD_IO_CHUNK_BYTES : remaining;
        int count = read(fd, cursor + used, wanted);
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

static void vb_pet_detach_custom_image(void)
{
    if (!g_pet.pet_image) return;
    lv_img_set_src(g_pet.pet_image, RT_NULL);
    lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_pet.pet_image);
}

static void vb_pet_clear_custom_state(void)
{
    vb_pet_detach_custom_image();
    g_pet.custom_frame_count = 0;
    g_pet.custom_frame_index = 0;
    g_pet.custom_displayed_frame = -1;
    g_pet.custom_state = -1;
}

static int vb_pet_load_rle_frame_segment(const char *path, uint32_t segment_offset,
                                         uint32_t segment_size, lv_img_dsc_t **out,
                                         uint32_t reusable_capacity)
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
    uint8_t *pixels;
    int allocated = 0;
    int fd = -1;

    if (!path || !out) return -RT_EINVAL;
    if (stat(path, &st) != 0 || segment_offset > (uint32_t)st.st_size) return -RT_ERROR;
    if (segment_size == 0) segment_size = (uint32_t)st.st_size - segment_offset;
    if (segment_size < sizeof(header) || segment_size > (uint32_t)st.st_size - segment_offset)
        return -RT_ERROR;
    fd = open(path, O_RDONLY);
    if (fd < 0 || lseek(fd, (off_t)segment_offset, SEEK_SET) < 0 ||
        vb_pet_read_full(fd, header, sizeof(header)) != RT_EOK) goto fail;
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
        segment_size != sizeof(header) + encoded_size) goto fail;
    image = *out;
    if (image)
    {
        if (!image->data || reusable_capacity < raw_size) goto fail;
        lv_img_cache_invalidate_src(image);
        image->header.always_zero = 0;
        image->header.w = width;
        image->header.h = height;
        image->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        image->data_size = raw_size;
    }
    else
    {
        image = app_cache_img_alloc(width, height, LV_IMG_CF_TRUE_COLOR_ALPHA,
                                    raw_size, IMAGE_CACHE_PSRAM);
        if (!image) goto fail;
        allocated = 1;
    }
    pixels = (uint8_t *)image->data;
    index = 0;
    while (index < run_count)
    {
        uint32_t chunk = run_count - index;
        uint32_t record_index;
        if (chunk > VB_PET_RLE_RECORD_BATCH) chunk = VB_PET_RLE_RECORD_BATCH;
        if (vb_pet_read_full(fd, g_vb_pet_rle_records, chunk * 5u) != RT_EOK) goto fail;
        for (record_index = 0; record_index < chunk; record_index++)
        {
            const uint8_t *record = &g_vb_pet_rle_records[record_index * 5u];
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
        index += chunk;
    }
    if (written != pixel_count) goto fail;
    close(fd);
    *out = image;
    return RT_EOK;

fail:
    if (fd >= 0) close(fd);
    if (allocated && image)
    {
        app_cache_img_free(image);
        *out = RT_NULL;
    }
    rt_kprintf("[vb_runtime][codex_pet] VRLE frame load failed path=%s\n", path);
    return -RT_ERROR;
}

static int vb_pet_load_rle_frame(const char *path, lv_img_dsc_t **out)
{
    return vb_pet_load_rle_frame_segment(path, 0, 0, out, 0);
}

static int vb_pet_load_rocky_frames(void)
{
    int row;
    int frame;
    for (row = 0; row < 5; row++)
    {
        for (frame = 0; frame < 2; frame++)
        {
            if (vb_pet_load_rle_frame(g_vb_pet_rocky_paths[row][frame],
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

static void vb_pet_copy_span(char *dst, rt_size_t cap, const char *src, rt_size_t length)
{
    if (!dst || cap == 0) return;
    if (!src) length = 0;
    if (length >= cap) length = cap - 1;
    if (length > 0) rt_memcpy(dst, src, length);
    dst[length] = '\0';
}

static void vb_pet_reset_flow_queue(int active)
{
    rt_base_t level = rt_hw_interrupt_disable();
    g_vb_pet_flow_read = 0;
    g_vb_pet_flow_write = 0;
    g_vb_pet_flow_drops = 0;
    g_vb_pet_flow_active = active ? 1 : 0;
    rt_hw_interrupt_enable(level);
}

static void vb_pet_enqueue_flow(const char *channel, uint32_t sequence,
                                const char *payload)
{
    uint32_t next;
    vb_pet_flow_message_t *message;
    rt_base_t level;
    if (!channel) return;
    level = rt_hw_interrupt_disable();
    if (!g_vb_pet_flow_active)
    {
        rt_hw_interrupt_enable(level);
        return;
    }
    next = (g_vb_pet_flow_write + 1u) % VB_PET_FLOW_QUEUE_SIZE;
    if (next == g_vb_pet_flow_read)
    {
        g_vb_pet_flow_read = (g_vb_pet_flow_read + 1u) % VB_PET_FLOW_QUEUE_SIZE;
        g_vb_pet_flow_drops++;
    }
    message = &g_vb_pet_flow_queue[g_vb_pet_flow_write];
    message->sequence = sequence;
    vb_pet_copy(message->channel, sizeof(message->channel), channel);
    vb_pet_copy(message->payload, sizeof(message->payload), payload);
    g_vb_pet_flow_write = next;
    rt_hw_interrupt_enable(level);
}

static int vb_pet_pop_flow(vb_pet_flow_message_t *message)
{
    rt_base_t level;
    if (!message) return 0;
    level = rt_hw_interrupt_disable();
    if (g_vb_pet_flow_read == g_vb_pet_flow_write)
    {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    *message = g_vb_pet_flow_queue[g_vb_pet_flow_read];
    g_vb_pet_flow_read = (g_vb_pet_flow_read + 1u) % VB_PET_FLOW_QUEUE_SIZE;
    rt_hw_interrupt_enable(level);
    return 1;
}

static int vb_pet_read_text(const char *path, char *dst, rt_size_t cap)
{
    struct stat st;
    int fd;
    int count;
    if (!path || !dst || cap < 2) return -RT_EINVAL;
    dst[0] = '\0';
    if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size >= (off_t)cap) return -RT_ERROR;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -RT_ERROR;
    count = read(fd, dst, (rt_size_t)st.st_size);
    close(fd);
    if (count != st.st_size) return -RT_ERROR;
    dst[count] = '\0';
    return RT_EOK;
}

static const char *vb_pet_asset_state_name(int state)
{
    static const char *const names[VB_PET_ASSET_STATE_COUNT] = {
        "idle", "runRight", "runLeft", "waving", "jumping",
        "failed", "waiting", "running", "review"
    };
    return state >= 0 && state < VB_PET_ASSET_STATE_COUNT ? names[state] : "none";
}

static int vb_pet_asset_state_index(void)
{
    if (g_pet.approval_pending) return VB_PET_ASSET_REVIEW;
    switch (g_pet.state)
    {
    case VB_PET_RECORDING: return VB_PET_ASSET_WAVING;
    case VB_PET_TRANSCRIBING:
    case VB_PET_RUNNING: return VB_PET_ASSET_RUNNING;
    case VB_PET_NEEDS_INPUT: return VB_PET_ASSET_WAITING;
    case VB_PET_READY: return VB_PET_ASSET_IDLE;
    case VB_PET_ERROR: return VB_PET_ASSET_FAILED;
    default: return VB_PET_ASSET_IDLE;
    }
}

static int vb_pet_desired_asset_state(void)
{
    if (g_pet.preview_asset_state >= 0) return g_pet.preview_asset_state;
    return g_pet.transient_asset_state >= 0 ?
           g_pet.transient_asset_state : vb_pet_asset_state_index();
}

static int vb_pet_legacy_state_index(int asset_state)
{
    switch (asset_state)
    {
    case VB_PET_ASSET_IDLE: return 0;
    case VB_PET_ASSET_WAVING:
    case VB_PET_ASSET_JUMPING: return 1;
    case VB_PET_ASSET_FAILED: return 2;
    case VB_PET_ASSET_WAITING:
    case VB_PET_ASSET_REVIEW: return 3;
    case VB_PET_ASSET_RUN_RIGHT:
    case VB_PET_ASSET_RUN_LEFT:
    case VB_PET_ASSET_RUNNING: return 4;
    default: return -1;
    }
}

static int vb_pet_state_is_task_resident(int state)
{
    int index;
    for (index = 0; index < VB_PET_TASK_RESIDENT_STATE_COUNT; index++)
    {
        if (state == g_vb_pet_task_resident_states[index]) return 1;
    }
    return 0;
}

static int vb_pet_stop_preload_worker(void)
{
    if (g_pet.loader_thread)
    {
        g_pet.loader_stop = 1;
        if (g_pet.loader_sem) rt_sem_release(g_pet.loader_sem);
        if (g_pet.loader_thread)
        {
            /* The loader can be inside SD/zlib work.  Runtime polls this
             * acknowledgement from its GUI timer instead of blocking LVGL. */
            return -RT_EBUSY;
        }
    }
    if (g_pet.loader_sem)
    {
        rt_sem_delete(g_pet.loader_sem);
        g_pet.loader_sem = RT_NULL;
    }
    return RT_EOK;
}

static int vb_pet_release_preloaded_assets(void)
{
    int bank;
    int frame;
    if (vb_pet_stop_preload_worker() != RT_EOK) return -RT_EBUSY;
    vb_pet_clear_custom_state();
    for (bank = 0; bank < VB_PET_PRELOAD_CACHE_BANKS; bank++)
    {
        for (frame = 0; frame < VB_PET_MAX_ASSET_FRAMES; frame++)
        {
            lv_img_dsc_t *image = &g_pet.preloaded_frames[bank][frame];
            if (image->data) lv_img_cache_invalidate_src(image);
        }
    }
    if (g_pet.preload_resident_compressed)
        app_cache_free(g_pet.preload_resident_compressed);
    if (g_pet.preloaded_data) app_cache_free(g_pet.preloaded_data);
    g_pet.preload_resident_compressed = RT_NULL;
    g_pet.preloaded_data = RT_NULL;
    g_pet.preloaded_data_size = 0;
    g_pet.preload_resident_compressed_bytes = 0;
    rt_memset(g_pet.preloaded_frames, 0, sizeof(g_pet.preloaded_frames));
    return RT_EOK;
}

static int vb_pet_preload_segment_valid(uint32_t offset, uint32_t length,
                                        uint32_t minimum_offset, off_t file_size)
{
    return offset >= minimum_offset && length > 0 &&
           length <= VB_PET_PRELOAD_MAX_COMPRESSED_BYTES &&
           (off_t)offset <= file_size &&
           (off_t)length <= file_size - (off_t)offset;
}

static int vb_pet_parse_preload_unlocked(void)
{
    uint8_t pack[VB_PET_PRELOAD_HEADER_SIZE +
                 VB_PET_ASSET_STATE_COUNT * VB_PET_PRELOAD_STATE_ENTRY_SIZE];
    uint32_t raw_size;
    uint32_t raw_resident_bytes;
    uint32_t compressed_resident_bytes = 0;
    uint32_t expected_first = 0;
    uint32_t index_bytes;
    uint16_t width;
    uint16_t height;
    uint16_t version;
    struct stat st;
    int fd = -1;
    int resident_fd = -1;
    int state;
    int bank;
    int frame;
    int result = 0;

    for (state = 0; state < VB_PET_ASSET_STATE_COUNT; state++)
    {
        g_pet.preload_resident_offsets[state] = 0xffffffffu;
        g_pet.preload_resident_lengths[state] = 0;
    }

    fd = open(VB_PET_PRELOAD_PATH, O_RDONLY);
    if (fd < 0 || vb_pet_read_full(fd, pack, VB_PET_PRELOAD_HEADER_SIZE) != RT_EOK ||
        fstat(fd, &st) != 0) goto finish;
    version = vb_pet_read_le16(&pack[4]);
    width = vb_pet_read_le16(&pack[8]);
    height = vb_pet_read_le16(&pack[10]);
    if (vb_pet_read_le32(&pack[0]) != VB_PET_PRELOAD_MAGIC ||
        vb_pet_read_le16(&pack[6]) != g_pet.pet_count ||
        width == 0 || width > 240 || height == 0 || height > 240) goto finish;
    raw_size = (uint32_t)width * (uint32_t)height * 3u;
    g_pet.preload_max_frames = 0;
    if (version == VB_PET_PRELOAD_VERSION)
    {
        uint16_t total_frames = vb_pet_read_le16(&pack[14]);
        if (vb_pet_read_le16(&pack[12]) != VB_PET_ASSET_STATE_COUNT ||
            total_frames < VB_PET_ASSET_STATE_COUNT * 2u ||
            total_frames > VB_PET_ASSET_STATE_COUNT * VB_PET_MAX_ASSET_FRAMES)
            goto finish;
        index_bytes = VB_PET_ASSET_STATE_COUNT * VB_PET_PRELOAD_STATE_ENTRY_SIZE;
        if (vb_pet_read_full(fd, &pack[VB_PET_PRELOAD_HEADER_SIZE], index_bytes) != RT_EOK)
            goto finish;
        g_pet.preload_split = st.st_size ==
            (off_t)(VB_PET_PRELOAD_HEADER_SIZE + index_bytes);
        for (state = 0; state < VB_PET_ASSET_STATE_COUNT; state++)
        {
            const uint8_t *entry = &pack[VB_PET_PRELOAD_HEADER_SIZE +
                                         state * VB_PET_PRELOAD_STATE_ENTRY_SIZE];
            uint32_t first = vb_pet_read_le16(entry);
            uint32_t count = entry[2];
            uint32_t offset = vb_pet_read_le32(entry + 4);
            uint32_t length = vb_pet_read_le32(entry + 8);
            if (entry[3] != 0 || first != expected_first || count < 2 ||
                count > VB_PET_MAX_ASSET_FRAMES) goto finish;
            if (g_pet.preload_split)
            {
                char state_path[96];
                struct stat state_st;
                rt_snprintf(state_path, sizeof(state_path),
                            VB_PET_PRELOAD_STATE_PATH_FORMAT, state);
                if (stat(state_path, &state_st) != 0 || state_st.st_size != (off_t)length)
                    goto finish;
            }
            else if (!vb_pet_preload_segment_valid(offset, length,
                         VB_PET_PRELOAD_HEADER_SIZE + index_bytes, st.st_size)) goto finish;
            g_pet.preload_state_frame_counts[state] = (uint8_t)count;
            g_pet.preload_state_offsets[state] = offset;
            g_pet.preload_state_lengths[state] = length;
            if (count > g_pet.preload_max_frames) g_pet.preload_max_frames = (uint8_t)count;
            expected_first += count;
        }
        if (expected_first != total_frames) goto finish;
    }
    else if (version == VB_PET_PRELOAD_LEGACY_VERSION)
    {
        uint32_t entry;
        uint32_t minimum_offset;
        if (vb_pet_read_le16(&pack[12]) != VB_PET_PRELOAD_LEGACY_STATE_COUNT ||
            vb_pet_read_le16(&pack[14]) != VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE)
            goto finish;
        index_bytes = VB_PET_PRELOAD_LEGACY_STATE_COUNT *
                      VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE * 8u;
        minimum_offset = VB_PET_PRELOAD_HEADER_SIZE + index_bytes;
        if (vb_pet_read_full(fd, &pack[VB_PET_PRELOAD_HEADER_SIZE], index_bytes) != RT_EOK)
            goto finish;
        for (entry = 0; entry < VB_PET_PRELOAD_LEGACY_STATE_COUNT *
                                   VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE; entry++)
        {
            uint32_t offset = vb_pet_read_le32(
                &pack[VB_PET_PRELOAD_HEADER_SIZE + entry * 8u]);
            uint32_t length = vb_pet_read_le32(
                &pack[VB_PET_PRELOAD_HEADER_SIZE + entry * 8u + 4u]);
            int legacy_state = (int)(entry / VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE);
            int legacy_frame = (int)(entry % VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE);
            if (!vb_pet_preload_segment_valid(offset, length, minimum_offset, st.st_size))
                goto finish;
            g_pet.preload_legacy_offsets[legacy_state][legacy_frame] = offset;
            g_pet.preload_legacy_lengths[legacy_state][legacy_frame] = length;
        }
        for (state = 0; state < VB_PET_ASSET_STATE_COUNT; state++)
            g_pet.preload_state_frame_counts[state] = VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE;
        g_pet.preload_max_frames = VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE;
    }
    else goto finish;

    for (index_bytes = 0; index_bytes < VB_PET_TASK_RESIDENT_STATE_COUNT; index_bytes++)
    {
        uint32_t length = 0;
        state = g_vb_pet_task_resident_states[index_bytes];
        if (version == VB_PET_PRELOAD_VERSION)
            length = g_pet.preload_state_lengths[state];
        else
        {
            int legacy_state = vb_pet_legacy_state_index(state);
            for (frame = 0; frame < VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE; frame++)
                length += g_pet.preload_legacy_lengths[legacy_state][frame];
        }
        if (length == 0 || length > VB_PET_PRELOAD_MAX_RESIDENT_COMPRESSED_BYTES -
            compressed_resident_bytes) goto finish;
        g_pet.preload_resident_offsets[state] = compressed_resident_bytes;
        g_pet.preload_resident_lengths[state] = length;
        compressed_resident_bytes += length;
    }
    raw_resident_bytes = raw_size * (uint32_t)g_pet.preload_max_frames *
                         VB_PET_PRELOAD_CACHE_BANKS;
    if (raw_resident_bytes == 0 || raw_resident_bytes > VB_PET_PRELOAD_MAX_BYTES ||
        compressed_resident_bytes == 0) goto finish;
    g_pet.preloaded_data = (uint8_t *)app_cache_alloc(raw_resident_bytes, IMAGE_CACHE_PSRAM);
    g_pet.preload_resident_compressed = (uint8_t *)app_cache_alloc(
        compressed_resident_bytes, IMAGE_CACHE_PSRAM);
    if (!g_pet.preloaded_data || !g_pet.preload_resident_compressed) goto finish;
    g_pet.preloaded_data_size = raw_resident_bytes;
    g_pet.preload_raw_frame_size = raw_size;
    g_pet.preload_resident_compressed_bytes = compressed_resident_bytes;
    g_pet.preload_version = (uint8_t)version;

    for (index_bytes = 0; index_bytes < VB_PET_TASK_RESIDENT_STATE_COUNT; index_bytes++)
    {
        uint8_t *cursor;
        state = g_vb_pet_task_resident_states[index_bytes];
        cursor = g_pet.preload_resident_compressed +
                 g_pet.preload_resident_offsets[state];
        if (version == VB_PET_PRELOAD_VERSION)
        {
            uint32_t length = g_pet.preload_state_lengths[state];
            if (g_pet.preload_split)
            {
                char state_path[96];
                rt_snprintf(state_path, sizeof(state_path),
                            VB_PET_PRELOAD_STATE_PATH_FORMAT, state);
                resident_fd = open(state_path, O_RDONLY);
                if (resident_fd < 0 || vb_pet_read_full(resident_fd, cursor, length) != RT_EOK)
                    goto finish;
                close(resident_fd);
                resident_fd = -1;
            }
            else if (lseek(fd, (off_t)g_pet.preload_state_offsets[state], SEEK_SET) < 0 ||
                     vb_pet_read_full(fd, cursor, length) != RT_EOK) goto finish;
        }
        else
        {
            int legacy_state = vb_pet_legacy_state_index(state);
            for (frame = 0; frame < VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE; frame++)
            {
                uint32_t offset = g_pet.preload_legacy_offsets[legacy_state][frame];
                uint32_t length = g_pet.preload_legacy_lengths[legacy_state][frame];
                if (lseek(fd, (off_t)offset, SEEK_SET) < 0 ||
                    vb_pet_read_full(fd, cursor, length) != RT_EOK) goto finish;
                cursor += length;
            }
        }
    }
    for (bank = 0; bank < VB_PET_PRELOAD_CACHE_BANKS; bank++)
    {
        g_pet.cache_state[bank] = -1;
        for (frame = 0; frame < g_pet.preload_max_frames; frame++)
        {
            lv_img_dsc_t *image = &g_pet.preloaded_frames[bank][frame];
            rt_memset(image, 0, sizeof(*image));
            image->header.always_zero = 0;
            image->header.w = width;
            image->header.h = height;
            image->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
            image->data_size = raw_size;
            image->data = g_pet.preloaded_data +
                ((uint32_t)bank * g_pet.preload_max_frames + (uint32_t)frame) * raw_size;
        }
    }
    result = 1;
finish:
    if (resident_fd >= 0) close(resident_fd);
    if (fd >= 0) close(fd);
    return result;
}

static int vb_pet_load_resident_state(int state, int bank)
{
    const uint8_t *source;
    uint8_t *target;
    uint32_t frame_count;
    if (!g_pet.preloaded_data || !g_pet.preload_resident_compressed ||
        !vb_pet_state_is_task_resident(state) ||
        state < 0 || state >= VB_PET_ASSET_STATE_COUNT ||
        bank < 0 || bank >= VB_PET_PRELOAD_CACHE_BANKS) return 0;
    frame_count = g_pet.preload_state_frame_counts[state];
    target = g_pet.preloaded_data +
        (uint32_t)bank * g_pet.preload_max_frames * g_pet.preload_raw_frame_size;
    source = g_pet.preload_resident_compressed + g_pet.preload_resident_offsets[state];
    if (g_pet.preload_version == VB_PET_PRELOAD_VERSION)
    {
        uLongf decoded_size = g_pet.preload_raw_frame_size * frame_count;
        if (uncompress(target, &decoded_size, source,
                       g_pet.preload_resident_lengths[state]) != Z_OK ||
            decoded_size != g_pet.preload_raw_frame_size * frame_count) return 0;
    }
    else
    {
        int legacy_state = vb_pet_legacy_state_index(state);
        int frame;
        if (legacy_state < 0) return 0;
        for (frame = 0; frame < VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE; frame++)
        {
            uint32_t length = g_pet.preload_legacy_lengths[legacy_state][frame];
            uLongf decoded_size = g_pet.preload_raw_frame_size;
            if (uncompress(target + (uint32_t)frame * g_pet.preload_raw_frame_size,
                           &decoded_size, source, length) != Z_OK ||
                decoded_size != g_pet.preload_raw_frame_size) return 0;
            source += length;
        }
    }
    return 1;
}

static int vb_pet_inflate_segment_unlocked(int fd, uint32_t offset, uint32_t length,
                                            uint8_t *target, uint32_t target_size)
{
    z_stream stream;
    uint32_t remaining = length;
    int z_result = Z_OK;
    int result = 0;
    if (fd < 0 || !target || target_size == 0 ||
        lseek(fd, (off_t)offset, SEEK_SET) < 0) return 0;
    rt_memset(&stream, 0, sizeof(stream));
    if (inflateInit(&stream) != Z_OK) return 0;
    while (remaining > 0 && z_result != Z_STREAM_END)
    {
        uint32_t wanted = remaining > sizeof(g_vb_pet_preload_io) ?
                          sizeof(g_vb_pet_preload_io) : remaining;
        int count = read(fd, g_vb_pet_preload_io, wanted);
        if (count <= 0) goto finish;
        remaining -= (uint32_t)count;
        stream.next_in = g_vb_pet_preload_io;
        stream.avail_in = (uInt)count;
        while (stream.avail_in > 0 && z_result != Z_STREAM_END)
        {
            if (stream.total_out >= target_size) goto finish;
            stream.next_out = target + stream.total_out;
            stream.avail_out = (uInt)(target_size - stream.total_out);
            z_result = inflate(&stream, Z_NO_FLUSH);
            if (z_result != Z_OK && z_result != Z_STREAM_END) goto finish;
            if (stream.avail_out == 0 && z_result != Z_STREAM_END) goto finish;
        }
    }
    result = z_result == Z_STREAM_END && remaining == 0 && stream.avail_in == 0 &&
             stream.total_out == target_size;
finish:
    inflateEnd(&stream);
    return result;
}

static int vb_pet_load_transient_state_unlocked(int state, int bank)
{
    char state_path[96];
    uint8_t *target;
    uint32_t frame_count;
    int fd = -1;
    int result = 0;
    if (!g_pet.preloaded_data || vb_pet_state_is_task_resident(state) ||
        state < 0 || state >= VB_PET_ASSET_STATE_COUNT ||
        bank < 0 || bank >= VB_PET_PRELOAD_CACHE_BANKS) return 0;
    frame_count = g_pet.preload_state_frame_counts[state];
    target = g_pet.preloaded_data +
        (uint32_t)bank * g_pet.preload_max_frames * g_pet.preload_raw_frame_size;
    if (g_pet.preload_version == VB_PET_PRELOAD_VERSION && g_pet.preload_split)
    {
        rt_snprintf(state_path, sizeof(state_path),
                    VB_PET_PRELOAD_STATE_PATH_FORMAT, state);
        fd = open(state_path, O_RDONLY);
    }
    else fd = open(VB_PET_PRELOAD_PATH, O_RDONLY);
    if (fd < 0) goto finish;
    if (g_pet.preload_version == VB_PET_PRELOAD_VERSION)
    {
        uint32_t offset = g_pet.preload_split ? 0u : g_pet.preload_state_offsets[state];
        result = vb_pet_inflate_segment_unlocked(
            fd, offset, g_pet.preload_state_lengths[state], target,
            g_pet.preload_raw_frame_size * frame_count);
    }
    else
    {
        int legacy_state = vb_pet_legacy_state_index(state);
        int frame;
        if (legacy_state < 0) goto finish;
        result = 1;
        for (frame = 0; frame < VB_PET_PRELOAD_LEGACY_FRAMES_PER_STATE; frame++)
        {
            if (!vb_pet_inflate_segment_unlocked(
                    fd, g_pet.preload_legacy_offsets[legacy_state][frame],
                    g_pet.preload_legacy_lengths[legacy_state][frame],
                    target + (uint32_t)frame * g_pet.preload_raw_frame_size,
                    g_pet.preload_raw_frame_size))
            {
                result = 0;
                break;
            }
        }
    }
finish:
    if (fd >= 0) close(fd);
    return result;
}

static void vb_pet_preload_worker(void *parameter)
{
    (void)parameter;
    while (!g_pet.loader_stop)
    {
        uint32_t sequence;
        int state;
        int bank;
        int result = 0;
        if (rt_sem_take(g_pet.loader_sem, RT_WAITING_FOREVER) != RT_EOK) continue;
        if (g_pet.loader_stop) break;
        /* Coalesce state changes queued while SD/zlib work was in progress.
         * Replaying stale semaphore tokens can otherwise overwrite a bank that
         * the LVGL thread has already activated from the latest completion. */
        while (rt_sem_take(g_pet.loader_sem, RT_WAITING_NO) == RT_EOK) {}
        if (g_pet.loader_stop) break;
        sequence = g_pet.loader_request_sequence;
        state = g_pet.loader_request_state;
        bank = g_pet.loader_request_bank;
        if (vb_pet_state_is_task_resident(state))
        {
            g_vb_pet_loader_phase = 31;
            result = vb_pet_load_resident_state(state, bank);
        }
        else
        {
            g_vb_pet_loader_phase = 30;
            if (vb_runtime_storage_take(VB_PET_PRELOAD_STORAGE_WAIT_MS) == RT_EOK)
            {
                g_vb_pet_loader_phase = 32;
                result = vb_pet_load_transient_state_unlocked(state, bank);
                vb_runtime_storage_release();
            }
        }
        g_pet.loader_completed_state = state;
        g_pet.loader_completed_bank = bank;
        g_pet.loader_completed_result = result;
        g_pet.loader_completed_sequence = sequence;
        g_vb_pet_loader_phase = 0;
    }
    g_vb_pet_loader_phase = 0;
    g_pet.loader_thread = RT_NULL;
}

static int vb_pet_start_preload_worker(void)
{
    rt_thread_t thread;
    g_pet.loader_sem = rt_sem_create("vbpetld", 0, RT_IPC_FLAG_FIFO);
    if (!g_pet.loader_sem) return 0;
    thread = rt_thread_create("vbpetld", vb_pet_preload_worker, RT_NULL,
                              VB_PET_PRELOAD_THREAD_STACK,
                              RT_THREAD_PRIORITY_MIDDLE + 10,
                              RT_THREAD_TICK_DEFAULT);
    if (!thread)
    {
        rt_sem_delete(g_pet.loader_sem);
        g_pet.loader_sem = RT_NULL;
        return 0;
    }
    g_pet.loader_thread = thread;
    rt_thread_startup(thread);
    return 1;
}

static int vb_pet_preload_assets(void)
{
    int result = 0;
    if (vb_runtime_storage_take(VB_PET_PRELOAD_STORAGE_WAIT_MS) != RT_EOK) return 0;
    g_vb_pet_loader_phase = 20;
    if (vb_pet_parse_preload_unlocked())
    {
        g_vb_pet_loader_phase = 22;
        result = vb_pet_load_resident_state(VB_PET_ASSET_IDLE, 0);
    }
    vb_runtime_storage_release();
    g_vb_pet_loader_phase = 0;
    if (result)
    {
        g_pet.cache_state[0] = VB_PET_ASSET_IDLE;
        g_pet.cache_frame_counts[0] = g_pet.preload_state_frame_counts[VB_PET_ASSET_IDLE];
        g_pet.active_cache_bank = 0;
        result = vb_pet_start_preload_worker();
    }
    if (!result) (void)vb_pet_release_preloaded_assets();
    else
        rt_kprintf("[vb_runtime][codex_pet] preload v%d states=%d cache=%lu resident=%lu max_frames=%d\n",
                   g_pet.preload_version,
                   g_pet.preload_version == VB_PET_PRELOAD_VERSION ?
                       VB_PET_ASSET_STATE_COUNT : VB_PET_PRELOAD_LEGACY_STATE_COUNT,
                   (unsigned long)g_pet.preloaded_data_size,
                   (unsigned long)g_pet.preload_resident_compressed_bytes,
                   g_pet.preload_max_frames);
    return result;
}

static void vb_pet_update_custom_frame(void)
{
    lv_img_dsc_t *image;
    if (!g_pet.custom_available || !g_pet.pet_image || g_pet.custom_frame_count < 1) return;
    if (g_pet.custom_frame_index < 0 || g_pet.custom_frame_index >= g_pet.custom_frame_count)
        g_pet.custom_frame_index = 0;
    if (g_pet.custom_displayed_frame == g_pet.custom_frame_index) return;
    image = &g_pet.preloaded_frames[g_pet.active_cache_bank][g_pet.custom_frame_index];
    if (!image->data) return;
    lv_img_set_src(g_pet.pet_image, image);
    /* Native Petdex frames define the action. Keep the image geometry stable. */
    lv_obj_set_pos(g_pet.pet_image, VB_PET_IMAGE_X, VB_PET_IMAGE_Y);
    if (g_pet.page != VB_PET_PAGE_HOME)
        lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(g_pet.pet_image, LV_OBJ_FLAG_HIDDEN);
    g_pet.custom_displayed_frame = g_pet.custom_frame_index;
}

static void vb_pet_activate_cache_bank(int index, int bank, int state)
{
    g_pet.pet_index = index;
    vb_pet_copy(g_pet.pet_slug, sizeof(g_pet.pet_slug), g_pet.pet_slugs[index]);
    vb_pet_copy(g_pet.pet_name, sizeof(g_pet.pet_name), g_pet.pet_names[index]);
    g_pet.active_cache_bank = bank;
    g_pet.custom_state = state;
    g_pet.custom_frame_count = g_pet.cache_frame_counts[bank];
    g_pet.custom_frame_index = 0;
    g_pet.custom_displayed_frame = -1;
    g_pet.custom_frame_ms = g_pet.preload_version == VB_PET_PRELOAD_VERSION ?
                            VB_PET_NATIVE_FRAME_MS : VB_PET_LEGACY_FRAME_MS;
    g_pet.custom_next_frame_at = rt_tick_get() +
        rt_tick_from_millisecond(g_pet.custom_frame_ms);
    g_pet.custom_available = 1;
    vb_pet_update_custom_frame();
    g_pet.dirty = 1;
}

static int vb_pet_activate_preloaded_state(int index, int state)
{
    int bank;
    int frame;
    if (!g_pet.preloaded_data || index < 0 || index >= g_pet.pet_count ||
        state < 0 || state >= VB_PET_ASSET_STATE_COUNT) return 0;
    g_pet.requested_asset_state = state;
    for (bank = 0; bank < VB_PET_PRELOAD_CACHE_BANKS; bank++)
    {
        if (g_pet.cache_state[bank] == state)
        {
            vb_pet_activate_cache_bank(index, bank, state);
            return 1;
        }
    }
    if (!g_pet.loader_thread || !g_pet.loader_sem) return 0;
    if (g_pet.loader_request_state == state &&
        g_pet.loader_completed_sequence != g_pet.loader_request_sequence) return 1;
    bank = 1 - g_pet.active_cache_bank;
    for (frame = 0; frame < g_pet.preload_max_frames; frame++)
        lv_img_cache_invalidate_src(&g_pet.preloaded_frames[bank][frame]);
    g_pet.cache_state[bank] = -1;
    g_pet.cache_frame_counts[bank] = 0;
    g_pet.loader_request_state = state;
    g_pet.loader_request_bank = bank;
    g_pet.loader_request_sequence++;
    if (g_pet.loader_request_sequence == 0) g_pet.loader_request_sequence = 1;
    rt_sem_release(g_pet.loader_sem);
    return 1;
}

static void vb_pet_apply_preload_completion(void)
{
    uint32_t sequence = g_pet.loader_completed_sequence;
    int state;
    int bank;
    if (!sequence || sequence == g_pet.loader_applied_sequence) return;
    g_pet.loader_applied_sequence = sequence;
    if (sequence != g_pet.loader_request_sequence) return;
    state = g_pet.loader_completed_state;
    bank = g_pet.loader_completed_bank;
    if (!g_pet.loader_completed_result || state < 0 ||
        state >= VB_PET_ASSET_STATE_COUNT || bank < 0 ||
        bank >= VB_PET_PRELOAD_CACHE_BANKS)
    {
        rt_kprintf("[vb_runtime][codex_pet] state load failed state=%s seq=%lu\n",
                   vb_pet_asset_state_name(state), (unsigned long)sequence);
        return;
    }
    g_pet.cache_state[bank] = state;
    g_pet.cache_frame_counts[bank] = g_pet.preload_state_frame_counts[state];
    if (g_pet.requested_asset_state == state)
    {
        vb_pet_activate_cache_bank(g_pet.pet_index, bank, state);
    }
    rt_kprintf("[vb_runtime][codex_pet] state ready state=%s frames=%d bank=%d seq=%lu\n",
               vb_pet_asset_state_name(state), g_pet.cache_frame_counts[bank], bank,
               (unsigned long)sequence);
}

static void vb_pet_begin_transient(int state)
{
    if (!g_pet.custom_available || state < 0 || state >= VB_PET_ASSET_STATE_COUNT) return;
    g_pet.transient_asset_state = state;
    /* Arm before cache selection; cache hits do not visit loader completion. */
    g_pet.transient_started = 1;
    if (g_pet.custom_state == state && g_pet.custom_frame_count > 0)
    {
        g_pet.custom_frame_index = 0;
        g_pet.custom_displayed_frame = -1;
        g_pet.custom_next_frame_at = rt_tick_get() +
            rt_tick_from_millisecond(g_pet.custom_frame_ms);
        vb_pet_update_custom_frame();
    }
    g_pet.dirty = 1;
}

static void vb_pet_cancel_idle_motion(void)
{
    g_pet.idle_next_at = 0;
    if (g_pet.idle_transient)
    {
        g_pet.idle_transient = 0;
        g_pet.transient_asset_state = -1;
        g_pet.transient_started = 0;
        g_pet.dirty = 1;
    }
}

static int vb_pet_idle_action_allowed(void)
{
    return g_pet.custom_available && g_pet.page == VB_PET_PAGE_HOME &&
           !g_pet.approval_pending && g_pet.active_task_count == 0 &&
           (g_pet.state == VB_PET_IDLE || g_pet.state == VB_PET_READY);
}

static int vb_pet_asset_state_from_name(const char *name)
{
    int state;
    if (!name) return -1;
    for (state = 0; state < VB_PET_ASSET_STATE_COUNT; state++)
    {
        if (rt_strcmp(name, vb_pet_asset_state_name(state)) == 0) return state;
    }
    return -1;
}

static int vb_pet_select_index(int index, int persist)
{
    (void)persist;
    return vb_pet_activate_preloaded_state(index, vb_pet_desired_asset_state());
}

static int vb_pet_load_catalog(void)
{
    char catalog[VB_PET_ASSET_CATALOG_MAX];
    char *line;
    if (vb_pet_read_text(VB_PET_CATALOG_PATH, catalog, sizeof(catalog)) != RT_EOK ||
        strncmp(catalog, "VBPETS1\n", 8) != 0) return 0;
    line = catalog + 8;
    while (*line && g_pet.pet_count < VB_PET_MAX_ASSETS)
    {
        char *end = strchr(line, '\n');
        char *first;
        char *second;
        if (!end) end = line + rt_strlen(line);
        first = strchr(line, '|');
        second = first ? strchr(first + 1, '|') : RT_NULL;
        if (first && second && first < end && second < end)
        {
            vb_pet_copy_span(g_pet.pet_slugs[g_pet.pet_count], VB_PET_ASSET_SLUG_MAX,
                             line, (rt_size_t)(first - line));
            vb_pet_copy_span(g_pet.pet_names[g_pet.pet_count], VB_PET_ASSET_NAME_MAX,
                             first + 1, (rt_size_t)(second - first - 1));
            if (g_pet.pet_slugs[g_pet.pet_count][0] && g_pet.pet_names[g_pet.pet_count][0])
                g_pet.pet_count++;
        }
        line = *end ? end + 1 : end;
    }
    if (g_pet.pet_count == 0) return 0;
    if (!vb_pet_preload_assets()) return 0;
    return vb_pet_select_index(0, 0);
}

static int vb_pet_select_slug(const char *slug, int persist)
{
    int index;
    if (!slug) return 0;
    for (index = 0; index < g_pet.pet_count; index++)
    {
        if (rt_strcmp(slug, g_pet.pet_slugs[index]) == 0)
            return vb_pet_select_index(index, persist);
    }
    return 0;
}

static void vb_pet_set_label_font(lv_obj_t *label, uint16_t size, uint32_t color)
{
    if (!label) return;
    lv_ext_set_local_font(label, size, lv_color_hex(color));
    lv_obj_set_style_text_letter_space(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *vb_pet_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    vb_pet_set_label_font(label, FONT_NORMAL, color);
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

static lv_obj_t *vb_pet_quota_bar(lv_obj_t *parent, int y, uint32_t color, int width)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, width, VB_PET_USAGE_CONTEXT_BAR_HEIGHT);
    lv_obj_set_pos(bar, VB_PET_USAGE_LEFT, y);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

static int vb_pet_detail_is_approval(const char *detail)
{
    return detail && (strstr(detail, "approval") || strstr(detail, "Approval"));
}

static const char *vb_pet_status_text(void)
{
    switch (g_pet.state)
    {
    case VB_PET_RECORDING: return "Listening";
    case VB_PET_TRANSCRIBING: return "Transcribing";
    case VB_PET_RUNNING: return "Running";
    case VB_PET_NEEDS_INPUT:
        if (g_pet.approval_pending) return "Approval required";
        if (strstr(g_pet.task_detail, "credential") ||
            strstr(g_pet.task_detail, "Credential")) return "Credential on Mac";
        return "Input needed on Mac";
    case VB_PET_READY: return "Ready";
    case VB_PET_ERROR: return "Blocked";
    case VB_PET_DISCONNECTED: return "Disconnected";
    default: return "Ready";
    }
}

static const char *vb_pet_state_name(vb_pet_state_t state)
{
    switch (state)
    {
    case VB_PET_RECORDING: return "recording";
    case VB_PET_TRANSCRIBING: return "transcribing";
    case VB_PET_RUNNING: return "running";
    case VB_PET_NEEDS_INPUT: return "needs_input";
    case VB_PET_READY: return "ready";
    case VB_PET_ERROR: return "blocked";
    case VB_PET_DISCONNECTED: return "disconnected";
    default: return "connected";
    }
}

static const char *vb_pet_indicator_name(vb_pet_state_t state)
{
    switch (state)
    {
    case VB_PET_RECORDING:
    case VB_PET_TRANSCRIBING: return "cyan";
    case VB_PET_RUNNING: return "blue";
    case VB_PET_NEEDS_INPUT: return "yellow";
    case VB_PET_READY: return "green";
    case VB_PET_ERROR: return "red";
    default: return "off";
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

static uint64_t vb_pet_json_u64(const char *payload, const char *key, uint64_t fallback)
{
    const char *value;
    char *end = RT_NULL;
    unsigned long long parsed;
    if (!payload || !key) return fallback;
    value = strstr(payload, key);
    if (!value) return fallback;
    value = strchr(value, ':');
    if (!value) return fallback;
    parsed = strtoull(value + 1, &end, 10);
    return end == value + 1 ? fallback : (uint64_t)parsed;
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

static int vb_pet_json_u64_array7(const char *payload, const char *key,
                                  uint64_t values[7])
{
    char marker[32];
    const char *cursor;
    int index;
    if (!payload || !key || !values) return 0;
    rt_snprintf(marker, sizeof(marker), "\"%s\":[", key);
    cursor = strstr(payload, marker);
    if (!cursor) return 0;
    cursor += strlen(marker);
    for (index = 0; index < 7; index++)
    {
        char *end = RT_NULL;
        unsigned long long value = strtoull(cursor, &end, 10);
        if (*cursor < '0' || *cursor > '9' || end == cursor || value > 1000u) return 0;
        values[index] = (uint64_t)value;
        cursor = end;
        if (index < 6)
        {
            if (*cursor != ',') return 0;
            cursor++;
        }
    }
    return *cursor == ']';
}

static uint32_t vb_pet_ticks_to_ms(uint32_t ticks)
{
    uint32_t seconds = ticks / RT_TICK_PER_SECOND;
    uint32_t remainder = ticks % RT_TICK_PER_SECOND;
    return seconds * 1000u + (remainder * 1000u) / RT_TICK_PER_SECOND;
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
    g_pet.quota_auth_required = strstr(payload, "\"error\":\"auth\"") != RT_NULL;
    primary = vb_pet_json_int(payload, "\"pU\"", -1);
    secondary = vb_pet_json_int(payload, "\"sU\"", -1);
    g_pet.quota_primary_used = primary;
    g_pet.quota_secondary_used = secondary;
    g_pet.quota_primary_window_minutes = vb_pet_json_int(payload, "\"pW\"", -1);
    g_pet.quota_secondary_window_minutes = vb_pet_json_int(payload, "\"sW\"", -1);
    g_pet.quota_primary_reset_seconds = vb_pet_json_int(payload, "\"pD\"", -1);
    g_pet.quota_secondary_reset_seconds = vb_pet_json_int(payload, "\"sD\"", -1);
    g_pet.quota_received_at = rt_tick_get();
    if (primary >= 0)
        rt_snprintf(g_pet.quota, sizeof(g_pet.quota), "Quota primary %d%%", primary);
    else
        vb_pet_copy(g_pet.quota, sizeof(g_pet.quota), "Quota unavailable / stale");
    if (g_pet.page == VB_PET_PAGE_USAGE_CURRENT) g_pet.dirty = 1;
}

static void vb_pet_receive_usage(uint32_t sequence, const char *payload)
{
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.usage_sequence)) return;
    if (vb_pet_json_int(payload, "\"v\"", 0) != 1) return;
    g_pet.usage_sequence = sequence;
    g_pet.usage_live = strstr(payload, "\"s\":\"l\"") != RT_NULL;
    g_pet.usage_cost_valid = strstr(payload, "\"d\":") != RT_NULL;
    g_pet.usage_total_tokens = vb_pet_json_u64(payload, "\"a\"", 0);
    g_pet.usage_context_tokens = vb_pet_json_u64(payload, "\"x\"", 0);
    g_pet.usage_context_window = vb_pet_json_u64(payload, "\"w\"", 0);
    g_pet.usage_uncached_input_tokens = vb_pet_json_u64(payload, "\"i\"", 0);
    g_pet.usage_cached_input_tokens = vb_pet_json_u64(payload, "\"c\"", 0);
    g_pet.usage_output_tokens = vb_pet_json_u64(payload, "\"o\"", 0);
    g_pet.usage_turn_tokens = vb_pet_json_u64(payload, "\"t\"", 0);
    g_pet.usage_cost_microusd = vb_pet_json_u64(payload, "\"d\"", 0);
    g_pet.usage_turn_cost_microusd = vb_pet_json_u64(payload, "\"e\"", 0);
    if (!vb_pet_json_string(payload, "m", g_pet.usage_model, sizeof(g_pet.usage_model)))
        g_pet.usage_model[0] = '\0';
    if (g_pet.page == VB_PET_PAGE_USAGE_CURRENT) g_pet.dirty = 1;
}

static void vb_pet_receive_usage_summary(uint32_t sequence, const char *payload)
{
    char unit[4];
    uint64_t trend[7];
    int cost_complete;
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.usage_summary_sequence)) return;
    cost_complete = vb_pet_json_int(payload, "\"c\"", -1);
    if (vb_pet_json_int(payload, "\"v\"", 0) != 1 ||
        !strstr(payload, "\"s\":\"l\"") ||
        !strstr(payload, "\"t\":") || cost_complete < 0 || cost_complete > 1 ||
        !vb_pet_json_string(payload, "u", unit, sizeof(unit)) ||
        (rt_strcmp(unit, "c") != 0 && rt_strcmp(unit, "t") != 0) ||
        (rt_strcmp(unit, "c") == 0 && cost_complete != 1) ||
        (cost_complete == 1 && !strstr(payload, "\"d\":")) ||
        !vb_pet_json_u64_array7(payload, "w", trend)) return;
    g_pet.usage_summary_sequence = sequence;
    g_pet.usage_summary_live = 1;
    g_pet.usage_summary_cost_complete = cost_complete;
    g_pet.usage_summary_cost_trend = rt_strcmp(unit, "c") == 0;
    g_pet.usage_summary_today_tokens = vb_pet_json_u64(payload, "\"t\"", 0);
    g_pet.usage_summary_today_cost = vb_pet_json_u64(payload, "\"d\"", 0);
    rt_memcpy(g_pet.usage_summary_trend, trend, sizeof(trend));
    if (g_pet.page == VB_PET_PAGE_USAGE_SUMMARY) g_pet.dirty = 1;
}

static int vb_pet_valid_mood(const char *mood)
{
    return mood &&
        (rt_strcmp(mood, "focused") == 0 || rt_strcmp(mood, "attentive") == 0 ||
         rt_strcmp(mood, "concerned") == 0 || rt_strcmp(mood, "celebrating") == 0 ||
         rt_strcmp(mood, "proud") == 0 || rt_strcmp(mood, "calm") == 0 ||
         rt_strcmp(mood, "content") == 0);
}

static void vb_pet_receive_progress(uint32_t sequence, const char *payload)
{
    char mood[16];
    int level;
    int today_tasks;
    int active_seconds;
    int streak;
    uint64_t xp;
    uint64_t next_xp;
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.progress_sequence)) return;
    level = vb_pet_json_int(payload, "\"l\"", 0);
    today_tasks = vb_pet_json_int(payload, "\"d\"", -1);
    active_seconds = vb_pet_json_int(payload, "\"a\"", -1);
    streak = vb_pet_json_int(payload, "\"s\"", -1);
    xp = vb_pet_json_u64(payload, "\"x\"", 0);
    next_xp = vb_pet_json_u64(payload, "\"n\"", 0);
    if (vb_pet_json_int(payload, "\"v\"", 0) != 1 || level < 1 ||
        today_tasks < 0 || active_seconds < 0 || streak < 0 || next_xp <= xp ||
        !vb_pet_json_string(payload, "m", mood, sizeof(mood)) || !vb_pet_valid_mood(mood)) return;
    g_pet.progress_sequence = sequence;
    g_pet.progress_live = 1;
    g_pet.progress_level = level;
    g_pet.progress_xp = xp;
    g_pet.progress_next_xp = next_xp;
    g_pet.progress_today_tasks = today_tasks;
    g_pet.progress_today_active_seconds = active_seconds;
    g_pet.progress_streak = streak;
    vb_pet_copy(g_pet.progress_mood, sizeof(g_pet.progress_mood), mood);
    g_pet.dirty = 1;
}

static int vb_pet_valid_badge(const char *badge)
{
    return badge &&
        (rt_strcmp(badge, "first-task") == 0 || rt_strcmp(badge, "five-task-day") == 0 ||
         rt_strcmp(badge, "hour-together") == 0 || rt_strcmp(badge, "three-day-streak") == 0 ||
         rt_strcmp(badge, "seven-day-streak") == 0);
}

static const char *vb_pet_badge_name(const char *badge)
{
    if (rt_strcmp(badge, "first-task") == 0) return "First task";
    if (rt_strcmp(badge, "five-task-day") == 0) return "Five together";
    if (rt_strcmp(badge, "hour-together") == 0) return "Hour together";
    if (rt_strcmp(badge, "three-day-streak") == 0) return "Three days";
    if (rt_strcmp(badge, "seven-day-streak") == 0) return "Seven days";
    return "Achievement";
}

static void vb_pet_receive_achievement(uint32_t sequence, const char *payload)
{
    char badge[24];
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.achievement_sequence)) return;
    if (vb_pet_json_int(payload, "\"v\"", 0) != 1 ||
        !vb_pet_json_string(payload, "id", badge, sizeof(badge)) ||
        !vb_pet_valid_badge(badge)) return;
    g_pet.achievement_sequence = sequence;
    rt_snprintf(g_pet.progress_notice, sizeof(g_pet.progress_notice),
                "Badge unlocked: %s", vb_pet_badge_name(badge));
    g_pet.progress_notice_until = rt_tick_get() + rt_tick_from_millisecond(VB_PET_NOTICE_MS);
    vb_pet_begin_transient(VB_PET_ASSET_JUMPING);
    g_pet.dirty = 1;
}

static void vb_pet_receive_cue(uint32_t sequence, const char *payload)
{
    char event_id[17];
    char cue[16];
    int volume;
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.cue_sequence)) return;
    volume = vb_pet_json_int(payload, "\"n\"", -1);
    if (vb_pet_json_int(payload, "\"v\"", 0) != 1 || volume < 0 || volume > 15 ||
        !vb_pet_json_string(payload, "id", event_id, sizeof(event_id)) ||
        !vb_pet_json_string(payload, "c", cue, sizeof(cue)) ||
        strlen(event_id) != 16 ||
        (rt_strcmp(cue, "done") != 0 && rt_strcmp(cue, "needs_input") != 0 &&
         rt_strcmp(cue, "error") != 0)) return;
    g_pet.cue_sequence = sequence;
    if (rt_strcmp(event_id, g_pet.last_cue_id) == 0) return;
    vb_pet_copy(g_pet.last_cue_id, sizeof(g_pet.last_cue_id), event_id);
    vb_pet_play_cue(cue);
}

static void vb_pet_set_hidden(lv_obj_t *object, int hidden)
{
    int currently_hidden;
    if (!object) return;
    currently_hidden = lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
    if (!!hidden == currently_hidden) return;
    if (hidden)
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static void vb_pet_set_label_text(lv_obj_t *label, const char *text)
{
    const char *current;
    if (!label) return;
    if (!text) text = "";
    current = lv_label_get_text(label);
    if (current && rt_strcmp(current, text) == 0) return;
    lv_label_set_text(label, text);
}

static void vb_pet_set_home_visible(int visible)
{
    int show_fallback;
    show_fallback = visible && !g_pet.custom_available && !g_pet.rocky_available;
    vb_pet_set_hidden(g_pet.title_label, !visible);
    vb_pet_set_hidden(g_pet.connection_label, !visible);
    vb_pet_set_hidden(g_pet.pet_image, !visible);
    vb_pet_set_hidden(g_pet.pet_body, !show_fallback);
    vb_pet_set_hidden(g_pet.pet_tail, !show_fallback);
    vb_pet_set_hidden(g_pet.left_ear, !show_fallback);
    vb_pet_set_hidden(g_pet.right_ear, !show_fallback);
    vb_pet_set_hidden(g_pet.pet_face, !show_fallback);
    vb_pet_set_hidden(g_pet.status_label, !visible);
    vb_pet_set_hidden(g_pet.transcript_label, !visible);
    vb_pet_set_hidden(g_pet.task_label, !visible);
    if (!visible)
    {
        vb_pet_set_hidden(g_pet.new_button, 1);
        vb_pet_set_hidden(g_pet.continue_button, 1);
    }
}

static void vb_pet_set_usage_metrics_visible(int visible)
{
    vb_pet_set_hidden(g_pet.usage_new_label, !visible);
    vb_pet_set_hidden(g_pet.usage_new_value, !visible);
    vb_pet_set_hidden(g_pet.usage_cached_label, !visible);
    vb_pet_set_hidden(g_pet.usage_cached_value, !visible);
    vb_pet_set_hidden(g_pet.usage_output_label, !visible);
    vb_pet_set_hidden(g_pet.usage_output_value, !visible);
}

static void vb_pet_set_summary_chart_visible(int visible)
{
    int index;
    for (index = 0; index < 7; index++)
    {
        vb_pet_set_hidden(g_pet.summary_bars[index], !visible);
        vb_pet_set_hidden(g_pet.summary_day_labels[index], !visible);
    }
}

static void vb_pet_set_quota_visible(int visible)
{
    vb_pet_set_hidden(g_pet.quota_title_label, !visible);
    vb_pet_set_hidden(g_pet.quota_status_label, !visible);
    vb_pet_set_hidden(g_pet.quota_primary_label, !visible);
    vb_pet_set_hidden(g_pet.quota_primary_value_label, !visible);
    vb_pet_set_hidden(g_pet.quota_primary_bar, !visible);
    vb_pet_set_hidden(g_pet.quota_primary_fill, !visible);
    vb_pet_set_hidden(g_pet.quota_primary_reset_label, !visible);
    vb_pet_set_hidden(g_pet.quota_secondary_label, !visible);
    vb_pet_set_hidden(g_pet.quota_footer_label, !visible);
    if (!visible)
    {
        vb_pet_set_usage_metrics_visible(0);
        vb_pet_set_summary_chart_visible(0);
    }
}

static int vb_pet_quota_remaining(int seconds, uint32_t received_at, uint32_t now)
{
    uint32_t elapsed;
    if (seconds < 0) return -1;
    elapsed = vb_pet_ticks_to_ms(now - received_at) / 1000u;
    if (elapsed >= (uint32_t)seconds) return 0;
    return seconds - (int)elapsed;
}

static void vb_pet_quota_reset_text(char *dst, rt_size_t cap, int seconds)
{
    int days;
    int hours;
    int minutes;
    if (seconds < 0)
    {
        vb_pet_copy(dst, cap, "Reset time unavailable");
        return;
    }
    if (seconds == 0)
    {
        vb_pet_copy(dst, cap, "Resetting now");
        return;
    }
    days = seconds / (24 * 60 * 60);
    hours = (seconds % (24 * 60 * 60)) / (60 * 60);
    minutes = (seconds % (60 * 60)) / 60;
    if (days > 0)
        rt_snprintf(dst, cap, "Reset in %dd %dh", days, hours);
    else if (hours > 0)
        rt_snprintf(dst, cap, "Reset in %dh %dm", hours, minutes);
    else
        rt_snprintf(dst, cap, "Reset in %dm", minutes > 0 ? minutes : 1);
}

static void vb_pet_format_metric(char *dst, rt_size_t cap, uint64_t value)
{
    uint64_t whole;
    uint64_t decimal;
    if (!dst || cap == 0) return;
    if (value >= 1000000000u)
    {
        whole = value / 1000000000u;
        decimal = (value % 1000000000u) / 100000000u;
        rt_snprintf(dst, cap, "%lu.%luB", (unsigned long)whole, (unsigned long)decimal);
    }
    else if (value >= 1000000u)
    {
        whole = value / 1000000u;
        decimal = (value % 1000000u) / 100000u;
        rt_snprintf(dst, cap, "%lu.%luM", (unsigned long)whole, (unsigned long)decimal);
    }
    else if (value >= 1000u)
    {
        whole = value / 1000u;
        decimal = (value % 1000u) / 100u;
        rt_snprintf(dst, cap, "%lu.%luK", (unsigned long)whole, (unsigned long)decimal);
    }
    else
    {
        rt_snprintf(dst, cap, "%lu", (unsigned long)value);
    }
}

static void vb_pet_format_cost(char *dst, rt_size_t cap, uint64_t microusd)
{
    uint64_t dollars;
    uint64_t fraction;
    if (!dst || cap == 0) return;
    dollars = microusd / 1000000u;
    if (dollars >= 100u)
        rt_snprintf(dst, cap, "~$%lu", (unsigned long)dollars);
    else if (microusd >= 10000u)
    {
        fraction = (microusd % 1000000u) / 10000u;
        rt_snprintf(dst, cap, "~$%lu.%02lu", (unsigned long)dollars,
                    (unsigned long)fraction);
    }
    else
    {
        fraction = microusd / 100u;
        rt_snprintf(dst, cap, "~$0.%04lu", (unsigned long)fraction);
    }
}

static void vb_pet_usage_limit_line(char *dst, rt_size_t cap, int minutes, int used,
                                    int reset_seconds, uint32_t now)
{
    char window[12];
    char reset[32];
    int remaining;
    if (used < 0)
    {
        dst[0] = '\0';
        return;
    }
    if (minutes > 0 && minutes % (24 * 60) == 0)
        rt_snprintf(window, sizeof(window), "%dd", minutes / (24 * 60));
    else if (minutes > 0 && minutes % 60 == 0)
        rt_snprintf(window, sizeof(window), "%dh", minutes / 60);
    else if (minutes > 0)
        rt_snprintf(window, sizeof(window), "%dm", minutes);
    else
        vb_pet_copy(window, sizeof(window), "Plan");
    remaining = vb_pet_quota_remaining(reset_seconds, g_pet.quota_received_at, now);
    if (remaining < 0)
        rt_snprintf(dst, cap, "%s %d%% used", window, used);
    else
    {
        vb_pet_quota_reset_text(reset, sizeof(reset), remaining);
        rt_snprintf(dst, cap, "%s %d%% used | %s", window, used, reset);
    }
}

static void vb_pet_prepare_usage_page(const char *title, int live)
{
    int offline = g_pet.state == VB_PET_DISCONNECTED;
    const char *status = offline ? "Offline" : (live ? "Live" : "Waiting");
    uint32_t status_color = offline || !live ? 0xfbbf24 : 0x34d399;
    vb_pet_set_home_visible(0);
    vb_pet_set_quota_visible(1);
    vb_pet_set_label_text(g_pet.quota_title_label, title);
    lv_obj_set_pos(g_pet.quota_title_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_TITLE_Y);
    lv_obj_set_size(g_pet.quota_title_label, VB_PET_USAGE_TITLE_WIDTH, 30);
    vb_pet_set_label_font(g_pet.quota_title_label, FONT_SUBTITLE, 0xf9fafb);
    lv_label_set_long_mode(g_pet.quota_title_label, LV_LABEL_LONG_CLIP);
    vb_pet_set_label_text(g_pet.quota_status_label, status);
    lv_obj_set_pos(g_pet.quota_status_label, VB_PET_USAGE_STATUS_X,
                   VB_PET_USAGE_STATUS_Y);
    lv_obj_set_size(g_pet.quota_status_label, VB_PET_USAGE_STATUS_WIDTH, 22);
    vb_pet_set_label_font(g_pet.quota_status_label, FONT_SMALL, status_color);
    lv_obj_set_style_text_align(g_pet.quota_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(g_pet.quota_status_label, LV_LABEL_LONG_CLIP);
}

static void vb_pet_render_token_usage(uint32_t now)
{
    char total[20];
    char context[20];
    char window[20];
    char uncached[20];
    char cached[20];
    char output[20];
    char cost[20];
    char text[128];
    char quota[64];
    uint64_t percent = 0;
    int fill_width = 0;
    int show_context;
    int show_quota;
    vb_pet_prepare_usage_page("This session", g_pet.usage_live);
    vb_pet_set_summary_chart_visible(0);

    if (!g_pet.usage_live)
    {
        vb_pet_set_usage_metrics_visible(0);
        vb_pet_set_label_text(g_pet.quota_primary_value_label, "No session");
        lv_obj_set_pos(g_pet.quota_primary_value_label, VB_PET_USAGE_LEFT, 94);
        lv_obj_set_size(g_pet.quota_primary_value_label, VB_PET_USAGE_WIDTH, 36);
        vb_pet_set_label_font(g_pet.quota_primary_value_label, FONT_SUBTITLE, 0xf9fafb);
        vb_pet_set_label_text(g_pet.quota_primary_label,
                              g_pet.state == VB_PET_DISCONNECTED ?
                              "Reconnect Bridge to load usage" :
                              "Start a Codex task to see usage");
        lv_obj_set_pos(g_pet.quota_primary_label, VB_PET_USAGE_LEFT, 138);
        lv_obj_set_size(g_pet.quota_primary_label, VB_PET_USAGE_WIDTH, 48);
        vb_pet_set_label_font(g_pet.quota_primary_label, FONT_NORMAL, 0x94a3b8);
        lv_label_set_long_mode(g_pet.quota_primary_label, LV_LABEL_LONG_WRAP);
        vb_pet_set_hidden(g_pet.quota_primary_reset_label, 1);
        vb_pet_set_hidden(g_pet.quota_secondary_label, 1);
        vb_pet_set_hidden(g_pet.quota_primary_bar, 1);
        vb_pet_set_hidden(g_pet.quota_primary_fill, 1);
        vb_pet_set_hidden(g_pet.quota_footer_label, 1);
        g_pet.quota_rendered_at = now;
        return;
    }

    vb_pet_format_metric(total, sizeof(total), g_pet.usage_total_tokens);
    vb_pet_set_label_text(g_pet.quota_primary_value_label, total);
    lv_obj_set_pos(g_pet.quota_primary_value_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_HERO_Y);
    lv_obj_set_size(g_pet.quota_primary_value_label, VB_PET_USAGE_WIDTH, 36);
    vb_pet_set_label_font(g_pet.quota_primary_value_label, FONT_TITLE, 0xf9fafb);
    lv_label_set_long_mode(g_pet.quota_primary_value_label, LV_LABEL_LONG_CLIP);
    vb_pet_set_label_text(g_pet.quota_primary_label, "tokens");
    lv_obj_set_pos(g_pet.quota_primary_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_UNIT_Y);
    lv_obj_set_size(g_pet.quota_primary_label, VB_PET_USAGE_WIDTH, 20);
    vb_pet_set_label_font(g_pet.quota_primary_label, FONT_SMALL, 0xaebbd0);
    lv_label_set_long_mode(g_pet.quota_primary_label, LV_LABEL_LONG_CLIP);

    if (g_pet.usage_cost_valid)
    {
        vb_pet_format_cost(cost, sizeof(cost), g_pet.usage_cost_microusd);
        rt_snprintf(text, sizeof(text), "%s%s%s", g_pet.usage_model,
                    g_pet.usage_model[0] ? "  |  " : "", cost);
    }
    else
        vb_pet_copy(text, sizeof(text), g_pet.usage_model);
    vb_pet_set_label_text(g_pet.quota_primary_reset_label, text);
    lv_obj_set_pos(g_pet.quota_primary_reset_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_META_Y);
    lv_obj_set_size(g_pet.quota_primary_reset_label, VB_PET_USAGE_WIDTH, 22);
    vb_pet_set_label_font(g_pet.quota_primary_reset_label, FONT_SMALL, 0xcbd5e1);
    lv_label_set_long_mode(g_pet.quota_primary_reset_label, LV_LABEL_LONG_CLIP);
    vb_pet_set_hidden(g_pet.quota_primary_reset_label, text[0] == '\0');

    vb_pet_format_metric(context, sizeof(context), g_pet.usage_context_tokens);
    vb_pet_format_metric(window, sizeof(window), g_pet.usage_context_window);
    show_context = g_pet.usage_context_window > 0;
    if (show_context)
    {
        percent = (g_pet.usage_context_tokens * 100u) / g_pet.usage_context_window;
        if (percent > 100u) percent = 100u;
        fill_width = (int)((VB_PET_USAGE_WIDTH * percent) / 100u);
    }
    rt_snprintf(text, sizeof(text), "Context  %s / %s  %lu%%", context, window,
                (unsigned long)percent);
    vb_pet_set_label_text(g_pet.quota_secondary_label, text);
    lv_obj_set_pos(g_pet.quota_secondary_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_CONTEXT_Y);
    lv_obj_set_size(g_pet.quota_secondary_label, VB_PET_USAGE_WIDTH, 22);
    vb_pet_set_label_font(g_pet.quota_secondary_label, FONT_NORMAL, 0xcbd5e1);
    lv_label_set_long_mode(g_pet.quota_secondary_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(g_pet.quota_primary_bar, VB_PET_USAGE_LEFT, VB_PET_USAGE_CONTEXT_BAR_Y);
    lv_obj_set_size(g_pet.quota_primary_bar, VB_PET_USAGE_WIDTH,
                    VB_PET_USAGE_CONTEXT_BAR_HEIGHT);
    lv_obj_set_pos(g_pet.quota_primary_fill, VB_PET_USAGE_LEFT, VB_PET_USAGE_CONTEXT_BAR_Y);
    lv_obj_set_height(g_pet.quota_primary_fill, VB_PET_USAGE_CONTEXT_BAR_HEIGHT);
    lv_obj_set_width(g_pet.quota_primary_fill, fill_width);
    vb_pet_set_hidden(g_pet.quota_secondary_label, !show_context);
    vb_pet_set_hidden(g_pet.quota_primary_bar, !show_context);
    vb_pet_set_hidden(g_pet.quota_primary_fill, !show_context);

    vb_pet_set_usage_metrics_visible(1);
    vb_pet_format_metric(uncached, sizeof(uncached), g_pet.usage_uncached_input_tokens);
    vb_pet_format_metric(cached, sizeof(cached), g_pet.usage_cached_input_tokens);
    vb_pet_format_metric(output, sizeof(output), g_pet.usage_output_tokens);
    vb_pet_set_label_text(g_pet.usage_new_value, uncached);
    vb_pet_set_label_text(g_pet.usage_cached_value, cached);
    vb_pet_set_label_text(g_pet.usage_output_value, output);

    show_quota = g_pet.quota_live && g_pet.quota_primary_used >= 0 &&
                 !g_pet.quota_auth_required && g_pet.state != VB_PET_DISCONNECTED;
    vb_pet_usage_limit_line(quota, sizeof(quota), g_pet.quota_primary_window_minutes,
                            g_pet.quota_primary_used, g_pet.quota_primary_reset_seconds, now);
    vb_pet_set_label_text(g_pet.quota_footer_label, quota);
    lv_obj_set_size(g_pet.quota_footer_label, VB_PET_USAGE_WIDTH, 22);
    lv_obj_set_pos(g_pet.quota_footer_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_FOOTER_Y);
    vb_pet_set_label_font(g_pet.quota_footer_label, FONT_SMALL, 0x64748b);
    lv_obj_set_style_text_align(g_pet.quota_footer_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_pet.quota_footer_label, LV_LABEL_LONG_CLIP);
    vb_pet_set_hidden(g_pet.quota_footer_label, !show_quota);
    g_pet.quota_rendered_at = now;
}

static void vb_pet_render_usage_summary(uint32_t now)
{
    static const char *const day_labels[7] = {"-6", "-5", "-4", "-3", "-2", "-1", "Td"};
    char total[20];
    char value[32];
    uint64_t maximum = 0;
    int index;
    vb_pet_prepare_usage_page("Last 7 days", g_pet.usage_summary_live);
    vb_pet_set_usage_metrics_visible(0);
    vb_pet_set_hidden(g_pet.quota_primary_bar, 1);
    vb_pet_set_hidden(g_pet.quota_primary_fill, 1);
    vb_pet_set_hidden(g_pet.quota_secondary_label, 1);
    for (index = 0; index < 7; index++)
        if (g_pet.usage_summary_trend[index] > maximum)
            maximum = g_pet.usage_summary_trend[index];
    if (!g_pet.usage_summary_live || maximum == 0)
    {
        vb_pet_set_summary_chart_visible(0);
        vb_pet_set_label_text(g_pet.quota_primary_value_label,
                              g_pet.usage_summary_live ? "No usage yet" : "No history");
        lv_obj_set_pos(g_pet.quota_primary_value_label, VB_PET_USAGE_LEFT, 94);
        lv_obj_set_size(g_pet.quota_primary_value_label, VB_PET_USAGE_WIDTH, 36);
        vb_pet_set_label_font(g_pet.quota_primary_value_label, FONT_SUBTITLE, 0xf9fafb);
        vb_pet_set_label_text(g_pet.quota_primary_label,
                              g_pet.state == VB_PET_DISCONNECTED ?
                              "Reconnect Bridge to load history" :
                              (g_pet.usage_summary_live ?
                               "Usage will appear after your first task" :
                               "Waiting for seven-day usage"));
        lv_obj_set_pos(g_pet.quota_primary_label, VB_PET_USAGE_LEFT, 138);
        lv_obj_set_size(g_pet.quota_primary_label, VB_PET_USAGE_WIDTH, 48);
        vb_pet_set_label_font(g_pet.quota_primary_label, FONT_NORMAL, 0x94a3b8);
        lv_label_set_long_mode(g_pet.quota_primary_label, LV_LABEL_LONG_WRAP);
        vb_pet_set_hidden(g_pet.quota_primary_reset_label, 1);
        vb_pet_set_hidden(g_pet.quota_footer_label, 1);
        g_pet.quota_rendered_at = now;
        return;
    }

    vb_pet_set_summary_chart_visible(1);
    vb_pet_format_metric(total, sizeof(total), g_pet.usage_summary_today_tokens);
    vb_pet_set_label_text(g_pet.quota_primary_value_label, total);
    lv_obj_set_pos(g_pet.quota_primary_value_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_HERO_Y);
    lv_obj_set_size(g_pet.quota_primary_value_label, VB_PET_USAGE_WIDTH, 36);
    vb_pet_set_label_font(g_pet.quota_primary_value_label, FONT_TITLE, 0xf9fafb);
    vb_pet_set_label_text(g_pet.quota_primary_label, "today");
    lv_obj_set_pos(g_pet.quota_primary_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_UNIT_Y);
    lv_obj_set_size(g_pet.quota_primary_label, VB_PET_USAGE_WIDTH, 20);
    vb_pet_set_label_font(g_pet.quota_primary_label, FONT_SMALL, 0xaebbd0);
    lv_label_set_long_mode(g_pet.quota_primary_label, LV_LABEL_LONG_CLIP);
    if (g_pet.usage_summary_cost_complete)
    {
        vb_pet_format_cost(value, sizeof(value), g_pet.usage_summary_today_cost);
        rt_snprintf(total, sizeof(total), "%s today", value);
        vb_pet_set_label_text(g_pet.quota_primary_reset_label, total);
    }
    lv_obj_set_pos(g_pet.quota_primary_reset_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_META_Y);
    lv_obj_set_size(g_pet.quota_primary_reset_label, VB_PET_USAGE_WIDTH, 22);
    vb_pet_set_label_font(g_pet.quota_primary_reset_label, FONT_NORMAL, 0x34d399);
    lv_label_set_long_mode(g_pet.quota_primary_reset_label, LV_LABEL_LONG_CLIP);
    vb_pet_set_hidden(g_pet.quota_primary_reset_label,
                      !g_pet.usage_summary_cost_complete);

    for (index = 0; index < 7; index++)
    {
        uint64_t divisor = maximum / (VB_PET_SUMMARY_BAR_HEIGHT - 4u) +
            (maximum % (VB_PET_SUMMARY_BAR_HEIGHT - 4u) ? 1u : 0u);
        int scaled = maximum ? (int)(g_pet.usage_summary_trend[index] / divisor) : 0;
        int height;
        int x = VB_PET_USAGE_LEFT + index * (VB_PET_SUMMARY_BAR_WIDTH + VB_PET_SUMMARY_BAR_GAP);
        if (scaled > VB_PET_SUMMARY_BAR_HEIGHT - 4) scaled = VB_PET_SUMMARY_BAR_HEIGHT - 4;
        height = 4 + scaled;
        lv_obj_set_pos(g_pet.summary_bars[index], x,
                       VB_PET_SUMMARY_BAR_Y + VB_PET_SUMMARY_BAR_HEIGHT - height);
        lv_obj_set_size(g_pet.summary_bars[index], VB_PET_SUMMARY_BAR_WIDTH, height);
        lv_obj_set_style_bg_color(g_pet.summary_bars[index],
                                  lv_color_hex(index == 6 ? 0x34d399 : 0x3b82f6),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        vb_pet_set_label_text(g_pet.summary_day_labels[index], day_labels[index]);
    }
    vb_pet_set_label_text(g_pet.quota_footer_label,
                          g_pet.usage_summary_cost_trend ? "Est. cost per day" :
                                                           "Tokens per day");
    lv_obj_set_pos(g_pet.quota_footer_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_FOOTER_Y);
    lv_obj_set_size(g_pet.quota_footer_label, VB_PET_USAGE_WIDTH, 24);
    vb_pet_set_label_font(g_pet.quota_footer_label, FONT_SMALL, 0x94a3b8);
    lv_obj_set_style_text_align(g_pet.quota_footer_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_pet.quota_footer_label, LV_LABEL_LONG_CLIP);
    g_pet.quota_rendered_at = now;
}

static void vb_pet_render_quota(uint32_t now)
{
    if (g_pet.page == VB_PET_PAGE_USAGE_SUMMARY)
        vb_pet_render_usage_summary(now);
    else
        vb_pet_render_token_usage(now);
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

static void vb_pet_navigate_task(int direction)
{
    const char *action;
    int result;
    if (g_pet.approval_pending || !g_pet.ops.send_action) return;
    action = direction < 0 ? "prev" : "next";
    result = g_pet.ops.send_action(action, "tasks");
    if (result != RT_EOK)
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Bridge unavailable");
    else
        g_pet.error[0] = '\0';
    g_pet.dirty = 1;
}

static int vb_pet_swipe_start_allowed(int x, int y)
{
    if (x < 30 || x > 359 || y < VB_PET_SWIPE_ZONE_TOP ||
        y > VB_PET_SWIPE_ZONE_BOTTOM) return 0;
    /* Preserve the Runtime left-edge right-swipe home gesture. */
    if (x <= VB_PET_EDGE_BACK_X) return 0;
    return 1;
}

static int vb_pet_handle_horizontal_swipe(int dx, int dy)
{
    if (g_pet.touch_swipe_consumed || g_pet.approval_pending ||
        !vb_pet_swipe_start_allowed(g_pet.touch_press_x, g_pet.touch_press_y) ||
        abs(dx) < VB_PET_SWIPE_MIN_DX || abs(dx) < abs(dy) + 12 ||
        abs(dy) > VB_PET_SWIPE_MAX_DY) return 0;
    g_pet.touch_swipe_consumed = 1;
    vb_pet_cancel_idle_motion();
    if (g_pet.page != VB_PET_PAGE_HOME)
    {
        g_pet.page = g_pet.page == VB_PET_PAGE_USAGE_SUMMARY ?
                     VB_PET_PAGE_USAGE_CURRENT : VB_PET_PAGE_USAGE_SUMMARY;
        g_pet.dirty = 1;
        rt_kprintf("[vb_runtime][codex_pet] usage view %s\n",
                   g_pet.page == VB_PET_PAGE_USAGE_SUMMARY ? "summary" : "current");
        return 1;
    }
    /* A left swipe advances; a right swipe returns to the previous task. */
    vb_pet_begin_transient(dx < 0 ? VB_PET_ASSET_RUN_LEFT : VB_PET_ASSET_RUN_RIGHT);
    vb_pet_navigate_task(dx < 0 ? 1 : -1);
    rt_kprintf("[vb_runtime][codex_pet] task swipe %s dx=%d dy=%d\n",
               dx < 0 ? "next" : "prev", dx, dy);
    return 1;
}

static int vb_pet_handle_vertical_swipe(int dx, int dy)
{
    if (g_pet.touch_swipe_consumed || g_pet.approval_pending ||
        abs(dy) < VB_PET_SWIPE_MIN_DY || abs(dy) < abs(dx) + 12 ||
        abs(dx) > VB_PET_SWIPE_MAX_DX) return 0;
    if (g_pet.page == VB_PET_PAGE_HOME && dy > 0 &&
        g_pet.touch_press_x >= 30 && g_pet.touch_press_x <= 359 &&
        g_pet.touch_press_y <= VB_PET_TOP_EDGE_MAX_Y)
    {
        g_pet.page = VB_PET_PAGE_USAGE_CURRENT;
        vb_pet_cancel_idle_motion();
        g_pet.touch_swipe_consumed = 1;
        g_pet.dirty = 1;
        rt_kprintf("[vb_runtime][codex_pet] usage current open dy=%d\n", dy);
        return 1;
    }
    if (g_pet.page != VB_PET_PAGE_HOME && dy < 0)
    {
        g_pet.page = VB_PET_PAGE_HOME;
        g_pet.touch_swipe_consumed = 1;
        g_pet.dirty = 1;
        rt_kprintf("[vb_runtime][codex_pet] usage page close dy=%d\n", dy);
        return 1;
    }
    return 0;
}

static void vb_pet_touch_event(lv_event_t *event)
{
    lv_event_code_t code;
    lv_indev_t *indev;
    lv_point_t point = {0, 0};
    int dx;
    int dy;
    if (!event || !g_pet.active) return;
    code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
        code != LV_EVENT_CLICKED && code != LV_EVENT_GESTURE) return;
    indev = lv_event_get_indev(event);
    if (!indev) indev = lv_indev_get_act();
    if (indev) lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED)
    {
        vb_pet_cancel_idle_motion();
        g_pet.touch_press_x = point.x;
        g_pet.touch_press_y = point.y;
        g_pet.touch_swipe_consumed = 0;
        return;
    }
    dx = point.x - g_pet.touch_press_x;
    dy = point.y - g_pet.touch_press_y;
    if (code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
        if (dir == LV_DIR_LEFT) dx = -VB_PET_SWIPE_MIN_DX;
        else if (dir == LV_DIR_RIGHT) dx = VB_PET_SWIPE_MIN_DX;
        else if (dir == LV_DIR_BOTTOM)
        {
            dx = 0;
            dy = VB_PET_SWIPE_MIN_DY;
        }
        else if (dir == LV_DIR_TOP)
        {
            dx = 0;
            dy = -VB_PET_SWIPE_MIN_DY;
        }
        else return;
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) dy = 0;
    }
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED ||
        code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED ||
        code == LV_EVENT_GESTURE)
    {
        if (!vb_pet_handle_vertical_swipe(dx, dy))
            (void)vb_pet_handle_horizontal_swipe(dx, dy);
    }
}

static void vb_pet_render(void)
{
    uint32_t color;
    uint32_t now;
    uint32_t sync_age_ms;
    int recent_count;
    int show_task_detail = 1;
    const char *task_text;
    const char *status_text;
    if (!g_pet.active || !g_pet.root) return;
    now = rt_tick_get();
    if (g_pet.page != VB_PET_PAGE_HOME)
    {
        vb_pet_render_quota(now);
        g_pet.dirty = 0;
        return;
    }
    vb_pet_set_quota_visible(0);
    vb_pet_set_home_visible(1);
    sync_age_ms = g_pet.host_seen_at ? vb_pet_ticks_to_ms(now - g_pet.host_seen_at) : 0;
    color = vb_pet_state_color();
    lv_label_set_text_fmt(g_pet.title_label, "%s · Lv %d",
                          g_pet.pet_name[0] ? g_pet.pet_name : "Codex Pet",
                          g_pet.progress_level > 0 ? g_pet.progress_level : 1);
    if (g_pet.state == VB_PET_DISCONNECTED)
        lv_label_set_text(g_pet.connection_label, "Bridge offline");
    else if (sync_age_ms >= VB_PET_RECONNECT_AFTER_MS)
        lv_label_set_text_fmt(g_pet.connection_label, "Reconnecting %lus",
                              (unsigned long)(sync_age_ms / 1000u));
    else
        lv_label_set_text_fmt(g_pet.connection_label, "Synced %lus ago",
                              (unsigned long)(sync_age_ms / 1000u));
    lv_obj_set_style_text_color(g_pet.connection_label, lv_color_hex(color),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (g_pet.custom_available)
    {
        int asset_state = vb_pet_desired_asset_state();
        if (asset_state != g_pet.custom_state)
            (void)vb_pet_activate_preloaded_state(g_pet.pet_index, asset_state);
    }
    if (g_pet.custom_available)
    {
        vb_pet_update_custom_frame();
    }
    else if (g_pet.rocky_available)
    {
        vb_pet_update_rocky(g_pet.animation_phase);
    }
    else
    {
        if (g_pet.pet_image) lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.pet_body) lv_obj_clear_flag(g_pet.pet_body, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.pet_tail) lv_obj_clear_flag(g_pet.pet_tail, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.left_ear) lv_obj_clear_flag(g_pet.left_ear, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.right_ear) lv_obj_clear_flag(g_pet.right_ear, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.pet_face) lv_obj_clear_flag(g_pet.pet_face, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(g_pet.pet_body, lv_color_hex(0xdbe7ed),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_pet.pet_face, lv_color_hex(0xe8f1f4),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_pet.pet_tail, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    status_text = vb_pet_status_text();
    if (g_pet.active_task_count == 0 && !g_pet.approval_pending && g_pet.progress_live)
    {
        if (rt_strcmp(g_pet.progress_mood, "proud") == 0) status_text = "Proud";
        else if (rt_strcmp(g_pet.progress_mood, "celebrating") == 0) status_text = "Celebrating";
        else if (rt_strcmp(g_pet.progress_mood, "content") == 0) status_text = "Content";
        else if (rt_strcmp(g_pet.progress_mood, "calm") == 0) status_text = "Calm";
    }
    lv_label_set_text(g_pet.status_label, status_text);
    lv_obj_set_style_text_color(g_pet.status_label, lv_color_hex(color),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (g_pet.error[0]) task_text = g_pet.error;
    else if (g_pet.approval_pending)
    {
        /* The status row is the single source of truth for a real approval. */
        task_text = "";
        show_task_detail = 0;
    }
    else if (!g_pet.approval_pending && vb_pet_detail_is_approval(g_pet.task_detail))
    {
        /* PermissionRequest hooks can be informational under auto-approval. */
        task_text = "";
        show_task_detail = 0;
    }
    else if (g_pet.active_task_count == 0 && g_pet.progress_notice[0] &&
             (int32_t)(g_pet.progress_notice_until - now) > 0)
        task_text = g_pet.progress_notice;
    else task_text = g_pet.task_detail[0] ? g_pet.task_detail : "No active Codex tasks";
    lv_label_set_text(g_pet.transcript_label, task_text);
    if (show_task_detail)
        lv_obj_clear_flag(g_pet.transcript_label, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_pet.transcript_label, LV_OBJ_FLAG_HIDDEN);
    if (g_pet.active_task_count > 0)
    {
        recent_count = g_pet.task_count - g_pet.active_task_count;
        if (recent_count < 0) recent_count = 0;
        lv_label_set_text_fmt(g_pet.task_label, "%d active  |  %d recent  |  %d/%d",
                              g_pet.active_task_count, recent_count,
                              g_pet.task_index, g_pet.task_count);
        lv_obj_set_pos(g_pet.task_label, 30,
                       show_task_detail ? VB_PET_TASK_LABEL_FULL_Y : VB_PET_TASK_LABEL_COMPACT_Y);
        lv_obj_clear_flag(g_pet.task_label, LV_OBJ_FLAG_HIDDEN);
    }
    else if (g_pet.progress_live)
    {
        lv_label_set_text_fmt(g_pet.task_label, "Today %d tasks  |  %dm",
                              g_pet.progress_today_tasks,
                              g_pet.progress_today_active_seconds / 60);
        lv_obj_set_pos(g_pet.task_label, 30, VB_PET_TASK_LABEL_FULL_Y);
        lv_obj_clear_flag(g_pet.task_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
        lv_obj_add_flag(g_pet.task_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(g_pet.task_label, lv_color_hex(0x94a3b8),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    vb_pet_apply_mode_style();
    if (g_pet.approval_pending)
    {
        lv_label_set_text(g_pet.new_label, "Allow");
        lv_label_set_text(g_pet.continue_label, "Deny");
        lv_obj_clear_flag(g_pet.new_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_pet.continue_button, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(g_pet.new_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.continue_button, LV_OBJ_FLAG_HIDDEN);
    }

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
            lv_obj_set_height(g_pet.left_eye, 27);
            lv_obj_set_height(g_pet.right_eye, 27);
        }
        else
        {
            lv_obj_set_height(g_pet.left_eye, 20);
            lv_obj_set_height(g_pet.right_eye, 20);
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
    vb_pet_navigate_task(-1);
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
    vb_pet_navigate_task(1);
}

static void vb_pet_image_event(lv_event_t *event)
{
    int next;
    const char *next_slug;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || g_pet.approval_pending) return;
    if (g_pet.pet_count < 2)
    {
        vb_pet_begin_transient(VB_PET_ASSET_JUMPING);
        return;
    }
    next = (g_pet.pet_index + 1) % g_pet.pet_count;
    next_slug = g_pet.pet_slugs[next];
    if (!vb_pet_select_index(next, 1))
    {
        vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Pet asset unavailable");
        g_pet.dirty = 1;
        return;
    }
    g_pet.error[0] = '\0';
    if (g_pet.ops.send_action)
        (void)g_pet.ops.send_action("pet_select", next_slug);
    vb_pet_begin_transient(VB_PET_ASSET_JUMPING);
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
    g_pet.host_seen_at = now;
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
    else if (strstr(payload, "\"st\":\"c\"") && g_pet.state == VB_PET_DISCONNECTED)
        g_pet.state = g_pet.task_count > 0 ? g_pet.task_state : VB_PET_IDLE;
    if (g_pet.state != previous)
    {
        vb_pet_cancel_idle_motion();
        if (g_pet.state == VB_PET_READY && previous != VB_PET_DISCONNECTED)
        {
            g_pet.ready_idle_at = now + rt_tick_from_millisecond(2000);
            vb_pet_begin_transient(VB_PET_ASSET_WAVING);
        }
        else
            g_pet.ready_idle_at = 0;
    }
    g_pet.dirty = 1;
}

static void vb_pet_receive_approval(uint32_t sequence, const char *payload)
{
    char request_id[VB_PET_APPROVAL_ID_MAX];
    char status[16];
    if (!payload || !vb_pet_sequence_newer(sequence, g_pet.approval_sequence)) return;
    vb_pet_cancel_idle_motion();
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
    vb_pet_cancel_idle_motion();
    g_pet.host_seen_at = rt_tick_get();
    g_pet.host_deadline = g_pet.host_seen_at + rt_tick_from_millisecond(VB_PET_HEARTBEAT_TTL_MS);
    previous = g_pet.state;
    (void)vb_pet_json_string(payload, "p", g_pet.project, sizeof(g_pet.project));
    if (!vb_pet_json_string(payload, "d", g_pet.task_detail, sizeof(g_pet.task_detail)))
        vb_pet_copy(g_pet.task_detail, sizeof(g_pet.task_detail), "Codex task updated");
    g_pet.task_index = vb_pet_json_int(payload, "\"i\"", 0);
    g_pet.task_count = vb_pet_json_int(payload, "\"n\"", 0);
    g_pet.active_task_count = vb_pet_json_int(payload, "\"ac\"", g_pet.task_count);
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
        else if (rt_strcmp(status, "needs_input") == 0)
            g_pet.state = !approval && vb_pet_detail_is_approval(g_pet.task_detail)
                              ? VB_PET_RUNNING
                              : VB_PET_NEEDS_INPUT;
        else if (rt_strcmp(status, "blocked") == 0) g_pet.state = VB_PET_ERROR;
        else if (rt_strcmp(status, "ready") == 0) g_pet.state = VB_PET_READY;
        else if (rt_strcmp(status, "connected") == 0) g_pet.state = VB_PET_IDLE;
        g_pet.task_state = g_pet.state;
    }
    g_pet.error[0] = '\0';
    if (g_pet.state != previous)
    {
        if (g_pet.state == VB_PET_READY && previous != VB_PET_DISCONNECTED)
        {
            g_pet.ready_idle_at = rt_tick_get() + rt_tick_from_millisecond(2000);
            vb_pet_begin_transient(VB_PET_ASSET_WAVING);
        }
        else
            g_pet.ready_idle_at = 0;
    }
    g_pet.dirty = 1;
}

static void vb_pet_publish_status(void)
{
    vb_pet_status_snapshot_t snapshot;
    rt_base_t level;
    rt_memset(&snapshot, 0, sizeof(snapshot));
    snapshot.active = g_pet.active;
    snapshot.state = g_pet.state;
    snapshot.host_seen_at = g_pet.host_seen_at;
    snapshot.task_index = g_pet.task_index;
    snapshot.task_count = g_pet.task_count;
    snapshot.active_task_count = g_pet.active_task_count;
    snapshot.approval_pending = g_pet.approval_pending;
    snapshot.pet_index = g_pet.pet_index;
    snapshot.pet_count = g_pet.pet_count;
    snapshot.custom_available = g_pet.custom_available;
    snapshot.custom_frame_count = g_pet.custom_frame_count;
    snapshot.custom_frame_index = g_pet.custom_frame_index;
    snapshot.custom_frame_ms = g_pet.custom_frame_ms;
    snapshot.custom_state = g_pet.custom_state;
    snapshot.requested_asset_state = g_pet.requested_asset_state;
    snapshot.preload_version = g_pet.preload_version;
    snapshot.asset_state_count = g_pet.preload_version == VB_PET_PRELOAD_VERSION ?
                                 VB_PET_ASSET_STATE_COUNT : VB_PET_PRELOAD_LEGACY_STATE_COUNT;
    snapshot.preloaded_data_size = g_pet.preloaded_data_size;
    snapshot.preload_resident_compressed_bytes = g_pet.preload_resident_compressed_bytes;
    snapshot.ui_tick_count = g_pet.ui_tick_count;
    vb_pet_copy(snapshot.pet_slug, sizeof(snapshot.pet_slug), g_pet.pet_slug);
    vb_pet_copy(snapshot.rgb_color, sizeof(snapshot.rgb_color), g_pet.rgb_color);
    level = rt_hw_interrupt_disable();
    snapshot.queued_flows =
        (g_vb_pet_flow_write + VB_PET_FLOW_QUEUE_SIZE - g_vb_pet_flow_read) %
        VB_PET_FLOW_QUEUE_SIZE;
    snapshot.dropped_flows = g_vb_pet_flow_drops;
    g_vb_pet_status = snapshot;
    rt_hw_interrupt_enable(level);
}

int vb_codex_pet_start(lv_obj_t *root, const vb_codex_pet_ops_t *ops,
                       const char *project)
{
    lv_obj_t *label;
    int index;
    if (!root || !ops || !ops->send_action) return -RT_EINVAL;
    if (vb_codex_pet_stop() != RT_EOK) return -RT_EBUSY;
    rt_memset(&g_pet, 0, sizeof(g_pet));
    g_pet.root = root;
    g_pet.ops = *ops;
    g_pet.active = 1;
    g_pet.page = VB_PET_PAGE_HOME;
    g_pet.progress_level = 1;
    g_pet.idle_last_asset = -1;
    g_pet.state = VB_PET_DISCONNECTED;
    g_pet.task_state = VB_PET_IDLE;
    g_pet.quota_primary_used = -1;
    g_pet.quota_secondary_used = -1;
    g_pet.quota_primary_window_minutes = -1;
    g_pet.quota_secondary_window_minutes = -1;
    g_pet.quota_primary_reset_seconds = -1;
    g_pet.quota_secondary_reset_seconds = -1;
    g_pet.rocky_frame_key = -1;
    g_pet.custom_state = -1;
    g_pet.custom_displayed_frame = -1;
    g_pet.requested_asset_state = -1;
    g_pet.loader_request_state = -1;
    g_pet.loader_completed_state = -1;
    g_pet.preview_asset_state = -1;
    g_pet.transient_asset_state = -1;
    vb_pet_copy(g_pet.project, sizeof(g_pet.project), project);

    lv_obj_clean(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0b1118), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(root, lv_color_hex(0xf9fafb), LV_PART_MAIN | LV_STATE_DEFAULT);

    label = vb_pet_label(root, "Codex Companion", 0xf9fafb);
    g_pet.title_label = label;
    lv_obj_set_pos(label, 30, 36);
    g_pet.connection_label = vb_pet_label(root, "Bridge offline", 0x94a3b8);
    lv_obj_set_width(g_pet.connection_label, 150);
    lv_obj_set_style_text_align(g_pet.connection_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_pet.connection_label, 210, 62);

    g_pet.pet_tail = lv_obj_create(root);
    lv_obj_set_size(g_pet.pet_tail, 64, 24);
    lv_obj_set_pos(g_pet.pet_tail, 240, 195);
    lv_obj_set_style_radius(g_pet.pet_tail, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.pet_tail, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.pet_tail, LV_OBJ_FLAG_SCROLLABLE);

    g_pet.pet_body = lv_obj_create(root);
    lv_obj_set_size(g_pet.pet_body, 118, 90);
    lv_obj_set_pos(g_pet.pet_body, 136, 172);
    lv_obj_set_style_radius(g_pet.pet_body, 43, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.pet_body, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.pet_body, LV_OBJ_FLAG_SCROLLABLE);

    g_pet.left_ear = lv_obj_create(root);
    lv_obj_set_size(g_pet.left_ear, 46, 54);
    lv_obj_set_pos(g_pet.left_ear, 119, 103);
    lv_obj_set_style_radius(g_pet.left_ear, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.left_ear, lv_color_hex(0xb8cbd3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.left_ear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.left_ear, LV_OBJ_FLAG_SCROLLABLE);
    g_pet.right_ear = lv_obj_create(root);
    lv_obj_set_size(g_pet.right_ear, 46, 54);
    lv_obj_set_pos(g_pet.right_ear, 225, 103);
    lv_obj_set_style_radius(g_pet.right_ear, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.right_ear, lv_color_hex(0xb8cbd3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.right_ear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.right_ear, LV_OBJ_FLAG_SCROLLABLE);

    g_pet.pet_face = lv_obj_create(root);
    lv_obj_set_size(g_pet.pet_face, 136, 124);
    lv_obj_set_pos(g_pet.pet_face, 127, 112);
    lv_obj_set_style_radius(g_pet.pet_face, 48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.pet_face, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_pet.pet_face, LV_OBJ_FLAG_SCROLLABLE);
    g_pet.left_eye = lv_obj_create(g_pet.pet_face);
    lv_obj_set_size(g_pet.left_eye, 12, 20);
    lv_obj_set_pos(g_pet.left_eye, 36, 43);
    lv_obj_set_style_radius(g_pet.left_eye, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.left_eye, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.left_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    g_pet.right_eye = lv_obj_create(g_pet.pet_face);
    lv_obj_set_size(g_pet.right_eye, 12, 20);
    lv_obj_set_pos(g_pet.right_eye, 88, 43);
    lv_obj_set_style_radius(g_pet.right_eye, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_pet.right_eye, lv_color_hex(0x111827), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_pet.right_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    g_pet.mouth = vb_pet_label(g_pet.pet_face, "-", 0x111827);
    lv_obj_set_width(g_pet.mouth, 32);
    lv_obj_set_style_text_align(g_pet.mouth, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_pet.mouth, 52, 82);

    g_pet.custom_available = vb_pet_load_catalog();
    if (!g_pet.custom_available)
    {
        g_pet.pet_count = 0;
        g_pet.pet_slug[0] = '\0';
        g_pet.pet_name[0] = '\0';
        g_pet.rocky_available = vb_pet_load_rocky_frames();
    }
    if (g_pet.custom_available || g_pet.rocky_available)
    {
        g_pet.pet_image = lv_img_create(root);
        lv_obj_set_pos(g_pet.pet_image, VB_PET_IMAGE_X, VB_PET_IMAGE_Y);
        lv_img_set_pivot(g_pet.pet_image, 80, 86);
        lv_img_set_zoom(g_pet.pet_image, VB_PET_IMAGE_ZOOM);
        lv_img_set_antialias(g_pet.pet_image, false);
        lv_obj_clear_flag(g_pet.pet_image, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_EVENT_BUBBLE |
                         LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(g_pet.pet_image, vb_pet_image_event, LV_EVENT_CLICKED, RT_NULL);
        lv_obj_add_flag(g_pet.pet_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.pet_tail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.left_ear, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.right_ear, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.pet_face, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.custom_available) vb_pet_update_custom_frame();
        else vb_pet_update_rocky(0);
    }

    g_pet.status_label = vb_pet_label(root, "Disconnected", 0x94a3b8);
    lv_obj_set_width(g_pet.status_label, 330);
    lv_obj_set_style_text_align(g_pet.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_pet.status_label, 30, VB_PET_STATUS_Y);
    g_pet.transcript_label = vb_pet_label(root, "No active Codex tasks", 0xf9fafb);
    lv_obj_set_size(g_pet.transcript_label, 330, VB_PET_TRANSCRIPT_HEIGHT);
    lv_obj_set_pos(g_pet.transcript_label, 30, VB_PET_TRANSCRIPT_Y);
    lv_obj_set_style_text_align(g_pet.transcript_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_pet.transcript_label, LV_LABEL_LONG_WRAP);
    g_pet.task_label = vb_pet_label(root, "No active tasks", 0x94a3b8);
    lv_obj_set_width(g_pet.task_label, 330);
    lv_obj_set_style_text_align(g_pet.task_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_pet.task_label, 30, VB_PET_TASK_LABEL_FULL_Y);

    g_pet.quota_title_label = vb_pet_label(root, "This session", 0xf9fafb);
    lv_obj_set_pos(g_pet.quota_title_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_TITLE_Y);
    g_pet.quota_status_label = vb_pet_label(root, "Waiting", 0x94a3b8);
    lv_obj_set_width(g_pet.quota_status_label, VB_PET_USAGE_STATUS_WIDTH);
    lv_obj_set_style_text_align(g_pet.quota_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_pet.quota_status_label, VB_PET_USAGE_STATUS_X,
                   VB_PET_USAGE_STATUS_Y);

    g_pet.quota_primary_label = vb_pet_label(root, "tokens", 0x94a3b8);
    lv_obj_set_width(g_pet.quota_primary_label, VB_PET_USAGE_WIDTH);
    lv_obj_set_pos(g_pet.quota_primary_label, VB_PET_USAGE_LEFT, VB_PET_USAGE_UNIT_Y);
    g_pet.quota_primary_value_label = vb_pet_label(root, "No session", 0xf9fafb);
    lv_obj_set_width(g_pet.quota_primary_value_label, VB_PET_USAGE_WIDTH);
    lv_obj_set_pos(g_pet.quota_primary_value_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_HERO_Y);
    g_pet.quota_primary_bar = vb_pet_quota_bar(root, VB_PET_USAGE_CONTEXT_BAR_Y,
                                                0x243244, VB_PET_USAGE_WIDTH);
    g_pet.quota_primary_fill = vb_pet_quota_bar(root, VB_PET_USAGE_CONTEXT_BAR_Y,
                                                 0x3b82f6, 0);
    g_pet.quota_primary_reset_label = vb_pet_label(root, "", 0x94a3b8);
    lv_obj_set_width(g_pet.quota_primary_reset_label, VB_PET_USAGE_WIDTH);
    lv_obj_set_pos(g_pet.quota_primary_reset_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_META_Y);

    g_pet.quota_secondary_label = vb_pet_label(root, "Context", 0x94a3b8);
    lv_obj_set_width(g_pet.quota_secondary_label, VB_PET_USAGE_WIDTH);
    lv_obj_set_pos(g_pet.quota_secondary_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_CONTEXT_Y);
    g_pet.quota_footer_label = vb_pet_label(root, "", 0x94a3b8);
    lv_obj_set_size(g_pet.quota_footer_label, VB_PET_USAGE_WIDTH, 24);
    lv_obj_set_style_text_align(g_pet.quota_footer_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_pet.quota_footer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(g_pet.quota_footer_label, VB_PET_USAGE_LEFT,
                   VB_PET_USAGE_FOOTER_Y);

    g_pet.usage_new_label = vb_pet_label(root, "Input", 0x94a3b8);
    g_pet.usage_new_value = vb_pet_label(root, "0", 0xf9fafb);
    g_pet.usage_cached_label = vb_pet_label(root, "Cached", 0x94a3b8);
    g_pet.usage_cached_value = vb_pet_label(root, "0", 0xf9fafb);
    g_pet.usage_output_label = vb_pet_label(root, "Output", 0x94a3b8);
    g_pet.usage_output_value = vb_pet_label(root, "0", 0xf9fafb);
    lv_obj_set_pos(g_pet.usage_new_label, VB_PET_USAGE_METRIC_NEW_X,
                   VB_PET_USAGE_METRIC_LABEL_Y);
    lv_obj_set_pos(g_pet.usage_new_value, VB_PET_USAGE_METRIC_NEW_X,
                   VB_PET_USAGE_METRIC_VALUE_Y);
    lv_obj_set_pos(g_pet.usage_cached_label, VB_PET_USAGE_METRIC_CACHED_X,
                   VB_PET_USAGE_METRIC_LABEL_Y);
    lv_obj_set_pos(g_pet.usage_cached_value, VB_PET_USAGE_METRIC_CACHED_X,
                   VB_PET_USAGE_METRIC_VALUE_Y);
    lv_obj_set_pos(g_pet.usage_output_label, VB_PET_USAGE_METRIC_OUTPUT_X,
                   VB_PET_USAGE_METRIC_LABEL_Y);
    lv_obj_set_pos(g_pet.usage_output_value, VB_PET_USAGE_METRIC_OUTPUT_X,
                   VB_PET_USAGE_METRIC_VALUE_Y);
    lv_obj_set_size(g_pet.usage_new_label, VB_PET_USAGE_METRIC_COLUMN_WIDTH, 22);
    lv_obj_set_size(g_pet.usage_cached_label, VB_PET_USAGE_METRIC_COLUMN_WIDTH, 22);
    lv_obj_set_size(g_pet.usage_output_label, VB_PET_USAGE_METRIC_COLUMN_WIDTH, 22);
    lv_obj_set_size(g_pet.usage_new_value, VB_PET_USAGE_METRIC_COLUMN_WIDTH, 30);
    lv_obj_set_size(g_pet.usage_cached_value, VB_PET_USAGE_METRIC_COLUMN_WIDTH, 30);
    lv_obj_set_size(g_pet.usage_output_value, VB_PET_USAGE_METRIC_COLUMN_WIDTH, 30);
    vb_pet_set_label_font(g_pet.usage_new_label, FONT_SMALL, 0x94a3b8);
    vb_pet_set_label_font(g_pet.usage_cached_label, FONT_SMALL, 0x94a3b8);
    vb_pet_set_label_font(g_pet.usage_output_label, FONT_SMALL, 0x94a3b8);
    vb_pet_set_label_font(g_pet.usage_new_value, FONT_SUBTITLE, 0xf9fafb);
    vb_pet_set_label_font(g_pet.usage_cached_value, FONT_SUBTITLE, 0xf9fafb);
    vb_pet_set_label_font(g_pet.usage_output_value, FONT_SUBTITLE, 0xf9fafb);
    lv_obj_set_style_text_align(g_pet.usage_new_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(g_pet.usage_cached_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(g_pet.usage_output_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(g_pet.usage_new_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(g_pet.usage_cached_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(g_pet.usage_output_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_pet.usage_new_label, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(g_pet.usage_cached_label, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(g_pet.usage_output_label, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(g_pet.usage_new_value, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(g_pet.usage_cached_value, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(g_pet.usage_output_value, LV_LABEL_LONG_CLIP);
    for (index = 0; index < 7; index++)
    {
        int x = VB_PET_USAGE_LEFT + index *
            (VB_PET_SUMMARY_BAR_WIDTH + VB_PET_SUMMARY_BAR_GAP);
        g_pet.summary_bars[index] = lv_obj_create(root);
        lv_obj_set_pos(g_pet.summary_bars[index], x,
                       VB_PET_SUMMARY_BAR_Y + VB_PET_SUMMARY_BAR_HEIGHT - 4);
        lv_obj_set_size(g_pet.summary_bars[index], VB_PET_SUMMARY_BAR_WIDTH, 4);
        lv_obj_set_style_radius(g_pet.summary_bars[index], 3,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(g_pet.summary_bars[index], 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(g_pet.summary_bars[index], lv_color_hex(0x3b82f6),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(g_pet.summary_bars[index], LV_OBJ_FLAG_SCROLLABLE);
        g_pet.summary_day_labels[index] = vb_pet_label(root, index == 6 ? "Td" : "-", 0x94a3b8);
        lv_obj_set_pos(g_pet.summary_day_labels[index], x - 10, VB_PET_SUMMARY_DAY_Y);
        lv_obj_set_size(g_pet.summary_day_labels[index], 40, 22);
        lv_obj_set_style_text_align(g_pet.summary_day_labels[index], LV_TEXT_ALIGN_CENTER, 0);
        vb_pet_set_label_font(g_pet.summary_day_labels[index], FONT_SMALL, 0x94a3b8);
        lv_label_set_long_mode(g_pet.summary_day_labels[index], LV_LABEL_LONG_CLIP);
    }
    vb_pet_set_quota_visible(0);

    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(root, vb_pet_touch_event, LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(root, vb_pet_touch_event, LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(root, vb_pet_touch_event, LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(root, vb_pet_touch_event, LV_EVENT_PRESS_LOST, RT_NULL);
    lv_obj_add_event_cb(root, vb_pet_touch_event, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(root, vb_pet_touch_event, LV_EVENT_GESTURE, RT_NULL);

    g_pet.new_button = vb_pet_button(root, "<", VB_PET_ACTION_LEFT_X,
                                     VB_PET_ACTION_Y, VB_PET_ACTION_WIDTH,
                                     VB_PET_ACTION_HEIGHT,
                                     0x243244, vb_pet_new_event);
    g_pet.new_label = lv_obj_get_child(g_pet.new_button, 0);
    g_pet.continue_button = vb_pet_button(root, ">", VB_PET_ACTION_RIGHT_X,
                                          VB_PET_ACTION_Y, VB_PET_ACTION_WIDTH,
                                          VB_PET_ACTION_HEIGHT,
                                          0x243244, vb_pet_continue_event);
    g_pet.continue_label = lv_obj_get_child(g_pet.continue_button, 0);
    lv_obj_add_flag(g_pet.new_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_pet.continue_button, LV_OBJ_FLAG_HIDDEN);
    vb_pet_render();
    if (g_pet.custom_available)
        g_pet.startup_transient_at = rt_tick_get() +
            rt_tick_from_millisecond(VB_PET_STARTUP_ANIMATION_DELAY_MS);
    vb_pet_rgb_tick(rt_tick_get());
    vb_pet_reset_flow_queue(1);
    vb_pet_publish_status();
    return RT_EOK;
}

int vb_codex_pet_stop(void)
{
    vb_pet_reset_flow_queue(0);
    if (!g_pet.active)
    {
        vb_pet_publish_status();
        return RT_EOK;
    }
    if ((g_pet.state == VB_PET_RECORDING || g_pet.state == VB_PET_TRANSCRIBING) &&
        g_pet.ops.voice_clear) g_pet.ops.voice_clear();
    {
        int release_result = vb_pet_release_preloaded_assets();
        if (release_result == -RT_EBUSY) return release_result;
        if (release_result != RT_EOK)
        {
            vb_pet_copy(g_pet.error, sizeof(g_pet.error), "Asset loader shutdown failed");
            g_pet.state = VB_PET_ERROR;
            g_pet.dirty = 1;
            vb_pet_publish_status();
            return release_result;
        }
    }
    g_pet.active = 0;
    if (g_pet.root) lv_obj_remove_event_cb(g_pet.root, vb_pet_touch_event);
    if (g_pet.ops.rgb_set) (void)g_pet.ops.rgb_set("off");
    vb_pet_detach_custom_image();
    vb_pet_release_rocky_frames();
    rt_memset(&g_pet, 0, sizeof(g_pet));
    vb_pet_publish_status();
    return RT_EOK;
}

static void vb_pet_apply_flow(const char *channel, uint32_t sequence,
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
    else if (rt_strcmp(channel, "pet.usage") == 0)
    {
        vb_pet_receive_usage(sequence, payload);
    }
    else if (rt_strcmp(channel, "pet.usage.summary") == 0)
    {
        vb_pet_receive_usage_summary(sequence, payload);
    }
    else if (rt_strcmp(channel, "pet.progress") == 0)
    {
        vb_pet_receive_progress(sequence, payload);
    }
    else if (rt_strcmp(channel, "pet.achievement") == 0)
    {
        vb_pet_receive_achievement(sequence, payload);
    }
    else if (rt_strcmp(channel, "pet.cue") == 0)
    {
        vb_pet_receive_cue(sequence, payload);
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
    else if (rt_strcmp(channel, "pet.select") == 0)
    {
        vb_pet_cancel_idle_motion();
        vb_pet_copy(g_pet.pending_pet_slug, sizeof(g_pet.pending_pet_slug), payload);
        g_pet.pending_pet_attempts = 0;
        g_pet.pending_pet_retry_at = 0;
        g_pet.pending_pet_selection = 1;
    }
    else if (rt_strcmp(channel, "pet.preview") == 0)
    {
        char state_name[20];
        int state;
        if (!vb_pet_json_string(payload, "state", state_name, sizeof(state_name)))
            vb_pet_copy(state_name, sizeof(state_name), payload);
        if (rt_strcmp(state_name, "tap") == 0)
        {
            g_pet.preview_asset_state = -1;
            vb_pet_begin_transient(VB_PET_ASSET_JUMPING);
            return;
        }
        if (rt_strcmp(state_name, "auto") == 0)
        {
            g_pet.preview_asset_state = -1;
            g_pet.dirty = 1;
            return;
        }
        state = vb_pet_asset_state_from_name(state_name);
        if (state >= 0)
        {
            g_pet.preview_asset_state = state;
            g_pet.transient_asset_state = -1;
            g_pet.transient_started = 0;
            g_pet.dirty = 1;
        }
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

static void vb_pet_drain_flows(void)
{
    vb_pet_flow_message_t message;
    while (vb_pet_pop_flow(&message))
        vb_pet_apply_flow(message.channel, message.sequence, message.payload);
}

void vb_codex_pet_receive_flow(const char *channel, uint32_t sequence,
                               const char *payload)
{
    vb_pet_enqueue_flow(channel, sequence, payload);
}

void vb_codex_pet_tick(uint32_t now)
{
#if VB_PET_VOICE_UI_ENABLED
    vb_codex_pet_voice_snapshot_t snapshot;
    int key2 = 0;
#endif
    uint32_t animation_phase;
    if (!g_pet.active) return;
    g_pet.ui_tick_count++;
    vb_pet_drain_flows();
    vb_pet_apply_preload_completion();
    if (g_pet.progress_notice[0] &&
        (int32_t)(now - g_pet.progress_notice_until) >= 0)
    {
        g_pet.progress_notice[0] = '\0';
        g_pet.progress_notice_until = 0;
        g_pet.dirty = 1;
    }
    if (g_pet.ready_idle_at && (int32_t)(now - g_pet.ready_idle_at) >= 0)
    {
        g_pet.ready_idle_at = 0;
        if (g_pet.state == VB_PET_READY) g_pet.state = VB_PET_IDLE;
        if (g_pet.task_state == VB_PET_READY) g_pet.task_state = VB_PET_IDLE;
        g_pet.dirty = 1;
    }
    if (vb_pet_idle_action_allowed())
    {
        if (g_pet.transient_asset_state < 0 && !g_pet.idle_next_at)
            g_pet.idle_next_at = now + rt_tick_from_millisecond(
                VB_PET_IDLE_MIN_MS + (uint32_t)(rand() % VB_PET_IDLE_RANGE_MS));
        else if (g_pet.transient_asset_state < 0 && g_pet.idle_next_at &&
                 (int32_t)(now - g_pet.idle_next_at) >= 0)
        {
            int action = g_pet.idle_last_asset == VB_PET_ASSET_WAVING ?
                         VB_PET_ASSET_JUMPING : VB_PET_ASSET_WAVING;
            g_pet.idle_next_at = 0;
            g_pet.idle_last_asset = action;
            g_pet.idle_transient = 1;
            vb_pet_begin_transient(action);
        }
    }
    else
        vb_pet_cancel_idle_motion();
    if (g_pet.startup_transient_at &&
        (int32_t)(now - g_pet.startup_transient_at) >= 0)
    {
        g_pet.startup_transient_at = 0;
        vb_pet_begin_transient(VB_PET_ASSET_JUMPING);
    }
    if (g_pet.pending_pet_selection &&
        (!g_pet.pending_pet_retry_at ||
         (int32_t)(now - g_pet.pending_pet_retry_at) >= 0))
    {
        char slug[VB_PET_ASSET_SLUG_MAX];
        vb_pet_copy(slug, sizeof(slug), g_pet.pending_pet_slug);
        if (!vb_pet_select_slug(slug, 1))
        {
            /* The board has one active pet slot; stale desktop selections are ignored. */
            g_pet.pending_pet_selection = 0;
            g_pet.pending_pet_attempts = 0;
            g_pet.pending_pet_retry_at = 0;
        }
        else
        {
            g_pet.pending_pet_selection = 0;
            g_pet.pending_pet_attempts = 0;
            g_pet.pending_pet_retry_at = 0;
            g_pet.error[0] = '\0';
        }
        g_pet.dirty = 1;
    }
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
        vb_pet_cancel_idle_motion();
        g_pet.state = VB_PET_DISCONNECTED;
        g_pet.dirty = 1;
    }
    if (g_pet.page == VB_PET_PAGE_HOME &&
        (!g_pet.sync_label_updated_at ||
         (int32_t)(now - g_pet.sync_label_updated_at) >= (int32_t)RT_TICK_PER_SECOND))
    {
        g_pet.sync_label_updated_at = now;
        g_pet.dirty = 1;
    }
    if (g_pet.page == VB_PET_PAGE_USAGE_CURRENT && g_pet.quota_live &&
        (!g_pet.quota_rendered_at ||
         vb_pet_ticks_to_ms(now - g_pet.quota_rendered_at) >=
             VB_PET_USAGE_CLOCK_REFRESH_MS))
        g_pet.dirty = 1;
    if (g_pet.custom_available && g_pet.custom_frame_count > 0)
    {
        if ((int32_t)(now - g_pet.custom_next_frame_at) >= 0)
        {
            g_pet.custom_frame_index++;
            if (g_pet.custom_frame_index >= g_pet.custom_frame_count)
            {
                g_pet.custom_frame_index = 0;
                if (g_pet.transient_asset_state == g_pet.custom_state &&
                    g_pet.transient_started)
                {
                    g_pet.transient_asset_state = -1;
                    g_pet.transient_started = 0;
                    g_pet.idle_transient = 0;
                    g_pet.dirty = 1;
                }
            }
            g_pet.custom_next_frame_at = now + rt_tick_from_millisecond(g_pet.custom_frame_ms);
            vb_pet_update_custom_frame();
        }
    }
    else
    {
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
                    lv_obj_set_y(g_pet.pet_face, animation_phase ? 109 : 112);
                if (g_pet.pet_tail)
                    lv_obj_set_pos(g_pet.pet_tail, animation_phase ? 245 : 240,
                                   animation_phase ? 189 : 195);
            }
        }
    }
    vb_pet_rgb_tick(now);
    if (g_pet.dirty) vb_pet_render();
    vb_pet_publish_status();
}

int vb_codex_pet_active(void)
{
    int active;
    rt_base_t level = rt_hw_interrupt_disable();
    active = g_vb_pet_status.active;
    rt_hw_interrupt_enable(level);
    return active;
}

int vb_codex_pet_status_json(char *dst, rt_size_t cap)
{
    vb_pet_status_snapshot_t snapshot;
    const char *state_name;
    uint32_t sync_age_ms;
    int recent_tasks;
    rt_base_t level;
    if (!dst || cap == 0) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    snapshot = g_vb_pet_status;
    rt_hw_interrupt_enable(level);
    state_name = snapshot.active ? vb_pet_state_name(snapshot.state) : "inactive";
    sync_age_ms = snapshot.host_seen_at ?
        vb_pet_ticks_to_ms(rt_tick_get() - snapshot.host_seen_at) : 0;
    recent_tasks = snapshot.task_count - snapshot.active_task_count;
    if (recent_tasks < 0) recent_tasks = 0;
    rt_snprintf(dst, cap,
                "{\"api\":\"%s\",\"active\":%d,\"connected\":%d,"
                "\"state\":\"%s\",\"tasks\":%d,\"activeTasks\":%d,"
                "\"recentTasks\":%d,\"syncAgeMs\":%lu,"
                "\"taskIndex\":%d,\"approval\":%d,\"pet\":\"%s\","
                "\"petIndex\":%d,\"pets\":%d,\"custom\":%d,"
                "\"assetState\":\"%s\",\"requestedAssetState\":\"%s\","
                "\"assetStates\":%d,\"preloadVersion\":%d,"
                "\"frames\":%d,\"frame\":%d,\"frameMs\":%d,"
                "\"preloadedBytes\":%lu,\"residentCompressedBytes\":%lu,"
                "\"uiTicks\":%lu,\"loaderPhase\":%d,"
                "\"queuedFlows\":%lu,\"droppedFlows\":%lu,"
                "\"indicator\":\"%s\",\"rgb\":\"%s\"}",
                VB_PET_STATUS_API,
                snapshot.active,
                snapshot.active && snapshot.state != VB_PET_DISCONNECTED,
                state_name,
                snapshot.task_count,
                snapshot.active_task_count,
                recent_tasks,
                (unsigned long)sync_age_ms,
                snapshot.task_index,
                snapshot.approval_pending,
                snapshot.pet_slug[0] ? snapshot.pet_slug : "rocky",
                snapshot.pet_count > 0 ? snapshot.pet_index + 1 : 0,
                snapshot.pet_count,
                snapshot.custom_available,
                vb_pet_asset_state_name(snapshot.custom_state),
                vb_pet_asset_state_name(snapshot.requested_asset_state),
                snapshot.asset_state_count,
                snapshot.preload_version,
                snapshot.custom_frame_count,
                snapshot.custom_frame_index,
                snapshot.custom_frame_ms,
                (unsigned long)snapshot.preloaded_data_size,
                (unsigned long)snapshot.preload_resident_compressed_bytes,
                (unsigned long)snapshot.ui_tick_count,
                g_vb_pet_loader_phase,
                (unsigned long)snapshot.queued_flows,
                (unsigned long)snapshot.dropped_flows,
                vb_pet_indicator_name(snapshot.state),
                snapshot.rgb_color[0] ? snapshot.rgb_color : "off");
    dst[cap - 1] = '\0';
    return RT_EOK;
}
