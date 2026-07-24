#include "vb_runtime_codex_pet.h"
#include "vb_runtime_storage.h"
#include "app_mem.h"

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
#define VB_PET_ASSET_STATE_COUNT 5
#define VB_PET_ROCKY_RLE_MAGIC 0x454c5256u
#define VB_PET_ROCKY_RLE_VERSION 1u
#define VB_PET_ROCKY_RLE_HEADER_SIZE 20u
#define VB_PET_RLE_RECORD_BATCH 800u
#define VB_PET_PRELOAD_PATH VB_PET_ASSET_ROOT "/preload.bin"
#define VB_PET_PRELOAD_MAGIC 0x43504256u
#define VB_PET_PRELOAD_VERSION 1u
#define VB_PET_PRELOAD_HEADER_SIZE 16u
#define VB_PET_PRELOAD_PACK_STATE_COUNT VB_PET_ASSET_STATE_COUNT
#define VB_PET_PRELOAD_STATE_COUNT VB_PET_ASSET_STATE_COUNT
#define VB_PET_PRELOAD_FRAMES_PER_STATE 2
#define VB_PET_STATUS_API "vibeboard-huangshan-codex-pet/v1"
#define VB_PET_PRELOAD_STORAGE_WAIT_MS 3000
#define VB_PET_PRELOAD_MAX_BYTES (900000u)
#define VB_PET_PRELOAD_MAX_COMPRESSED_BYTES (128u * 1024u)
#define VB_PET_FLOW_QUEUE_SIZE 16
#define VB_PET_FLOW_CHANNEL_MAX 25
#define VB_PET_FLOW_PAYLOAD_MAX 193
#define VB_PET_RUNTIME_FRAME_LIMIT 2
#define VB_PET_PRELOAD_IO_CHUNK_BYTES (8u * 1024u)
#define VB_PET_NATIVE_FRAME_MS 180
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
#define VB_PET_EDGE_BACK_X 72

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
    uint32_t rgb_phase;
    int quota_live;
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
    uint32_t preloaded_data_size;
    uint8_t preloaded_frame_counts[VB_PET_MAX_ASSETS][VB_PET_PRELOAD_STATE_COUNT];
    uint32_t ui_tick_count;
    int pet_index;
    int pet_count;
    volatile int pending_pet_selection;
    int pending_pet_attempts;
    uint32_t custom_next_frame_at;
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
    lv_obj_t *connection_label;
    lv_obj_t *pet_face;
    lv_obj_t *pet_body;
    lv_obj_t *pet_tail;
    lv_obj_t *left_ear;
    lv_obj_t *right_ear;
    lv_obj_t *pet_image;
    lv_img_dsc_t *rocky_frames[5][2];
    lv_img_dsc_t preloaded_frames[VB_PET_MAX_ASSETS][VB_PET_PRELOAD_STATE_COUNT]
                                       [VB_PET_PRELOAD_FRAMES_PER_STATE];
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;
    lv_obj_t *mouth;
    lv_obj_t *status_label;
    lv_obj_t *pet_name_label;
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
    uint32_t preloaded_data_size;
    uint32_t ui_tick_count;
    uint32_t queued_flows;
    uint32_t dropped_flows;
    char pet_slug[VB_PET_ASSET_SLUG_MAX];
    char rgb_color[8];
} vb_pet_status_snapshot_t;

static vb_codex_pet_state_t g_pet;
static uint8_t g_vb_pet_rle_records[VB_PET_RLE_RECORD_BATCH * 5u];
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

static int vb_pet_asset_state_index(void)
{
    switch (g_pet.state)
    {
    case VB_PET_RECORDING: return 3;
    case VB_PET_TRANSCRIBING: return 4;
    case VB_PET_RUNNING: return 4;
    case VB_PET_NEEDS_INPUT: return 3;
    case VB_PET_READY: return 1;
    case VB_PET_ERROR: return 2;
    default: return 0;
    }
}

static int vb_pet_preload_state_index(int asset_state)
{
    return asset_state >= 0 && asset_state < VB_PET_PRELOAD_STATE_COUNT ? asset_state : -1;
}

static void vb_pet_release_preloaded_assets(void)
{
    int pet;
    int state;
    int frame;
    vb_pet_clear_custom_state();
    for (pet = 0; pet < VB_PET_MAX_ASSETS; pet++)
    {
        for (state = 0; state < VB_PET_PRELOAD_STATE_COUNT; state++)
        {
            for (frame = 0; frame < VB_PET_PRELOAD_FRAMES_PER_STATE; frame++)
            {
                lv_img_dsc_t *image = &g_pet.preloaded_frames[pet][state][frame];
                if (image->data) lv_img_cache_invalidate_src(image);
            }
        }
    }
    if (g_pet.preloaded_data) app_cache_free(g_pet.preloaded_data);
    g_pet.preloaded_data = RT_NULL;
    g_pet.preloaded_data_size = 0;
    rt_memset(g_pet.preloaded_frame_counts, 0, sizeof(g_pet.preloaded_frame_counts));
    rt_memset(g_pet.preloaded_frames, 0, sizeof(g_pet.preloaded_frames));
}

static int vb_pet_validate_preload_index(const uint8_t *pack, uint32_t index_bytes,
                                         off_t file_size, uint32_t *max_compressed)
{
    uint32_t entry_count = index_bytes / 8u;
    uint32_t entry;
    if (!pack || !max_compressed || index_bytes == 0 || index_bytes % 8u != 0) return 0;
    *max_compressed = 0;
    for (entry = 0; entry < entry_count; entry++)
    {
        uint32_t offset = vb_pet_read_le32(&pack[VB_PET_PRELOAD_HEADER_SIZE + entry * 8u]);
        uint32_t length = vb_pet_read_le32(&pack[VB_PET_PRELOAD_HEADER_SIZE + entry * 8u + 4u]);
        if (offset < VB_PET_PRELOAD_HEADER_SIZE + index_bytes || length == 0 ||
            length > VB_PET_PRELOAD_MAX_COMPRESSED_BYTES ||
            (off_t)offset > file_size || (off_t)length > file_size - (off_t)offset)
            return 0;
        if (length > *max_compressed) *max_compressed = length;
    }
    return *max_compressed > 0;
}

static int vb_pet_fill_preloaded_assets_unlocked(void)
{
    uint8_t pack[VB_PET_PRELOAD_HEADER_SIZE +
                 VB_PET_MAX_ASSETS * VB_PET_PRELOAD_PACK_STATE_COUNT *
                 VB_PET_PRELOAD_FRAMES_PER_STATE * 8u];
    uint8_t *compressed = RT_NULL;
    uint8_t *cursor;
    uint32_t entry_count;
    uint32_t index_bytes;
    uint32_t max_compressed;
    uint32_t loaded_compressed = 0;
    uint32_t raw_size;
    uint32_t total_bytes;
    uint16_t width;
    uint16_t height;
    struct stat st;
    int fd = -1;
    int pet;
    int state;
    int frame;
    int result = 0;
    fd = open(VB_PET_PRELOAD_PATH, O_RDONLY);
    if (fd < 0 || vb_pet_read_full(fd, pack, VB_PET_PRELOAD_HEADER_SIZE) != RT_EOK ||
        fstat(fd, &st) != 0) goto finish;
    width = vb_pet_read_le16(&pack[8]);
    height = vb_pet_read_le16(&pack[10]);
    entry_count = (uint32_t)g_pet.pet_count * VB_PET_PRELOAD_PACK_STATE_COUNT *
                  VB_PET_PRELOAD_FRAMES_PER_STATE;
    index_bytes = entry_count * 8u;
    if (vb_pet_read_le32(&pack[0]) != VB_PET_PRELOAD_MAGIC ||
        vb_pet_read_le16(&pack[4]) != VB_PET_PRELOAD_VERSION ||
        vb_pet_read_le16(&pack[6]) != g_pet.pet_count ||
        width == 0 || width > 240 || height == 0 || height > 240 ||
        vb_pet_read_le16(&pack[12]) != VB_PET_PRELOAD_PACK_STATE_COUNT ||
        vb_pet_read_le16(&pack[14]) != VB_PET_PRELOAD_FRAMES_PER_STATE ||
        vb_pet_read_full(fd, &pack[VB_PET_PRELOAD_HEADER_SIZE], index_bytes) != RT_EOK ||
        !vb_pet_validate_preload_index(pack, index_bytes, st.st_size, &max_compressed))
        goto finish;
    raw_size = (uint32_t)width * (uint32_t)height * 3u;
    total_bytes = raw_size * (uint32_t)g_pet.pet_count *
                  VB_PET_PRELOAD_STATE_COUNT * VB_PET_PRELOAD_FRAMES_PER_STATE;
    if (total_bytes == 0 || total_bytes > VB_PET_PRELOAD_MAX_BYTES) goto finish;
    g_pet.preloaded_data = (uint8_t *)app_cache_alloc(total_bytes, IMAGE_CACHE_PSRAM);
    compressed = (uint8_t *)app_cache_alloc(max_compressed, IMAGE_CACHE_PSRAM);
    if (!g_pet.preloaded_data || !compressed) goto finish;
    g_pet.preloaded_data_size = total_bytes;
    cursor = g_pet.preloaded_data;
    for (pet = 0; pet < g_pet.pet_count; pet++)
    {
        for (state = 0; state < VB_PET_PRELOAD_STATE_COUNT; state++)
        {
            for (frame = 0; frame < VB_PET_PRELOAD_FRAMES_PER_STATE; frame++)
            {
                uint32_t entry = ((uint32_t)pet * VB_PET_PRELOAD_PACK_STATE_COUNT +
                                  (uint32_t)state) * VB_PET_PRELOAD_FRAMES_PER_STATE +
                                 (uint32_t)frame;
                uint32_t offset = vb_pet_read_le32(
                    &pack[VB_PET_PRELOAD_HEADER_SIZE + entry * 8u]);
                uint32_t length = vb_pet_read_le32(
                    &pack[VB_PET_PRELOAD_HEADER_SIZE + entry * 8u + 4u]);
                uLongf decoded_size = raw_size;
                lv_img_dsc_t *image = &g_pet.preloaded_frames[pet][state][frame];
                if (lseek(fd, (off_t)offset, SEEK_SET) < 0 ||
                    vb_pet_read_full(fd, compressed, length) != RT_EOK ||
                    uncompress(cursor, &decoded_size, compressed, length) != Z_OK ||
                    decoded_size != raw_size) goto finish;
                rt_memset(image, 0, sizeof(*image));
                image->header.always_zero = 0;
                image->header.w = width;
                image->header.h = height;
                image->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
                image->data_size = raw_size;
                image->data = cursor;
                cursor += raw_size;
                loaded_compressed += length;
                rt_thread_yield();
            }
            g_pet.preloaded_frame_counts[pet][state] = VB_PET_PRELOAD_FRAMES_PER_STATE;
        }
    }
    rt_kprintf("[vb_runtime][codex_pet] preloaded pets=%d states=%d frames=%d bytes=%lu compressed=%lu\n",
               g_pet.pet_count, g_pet.pet_count * VB_PET_PRELOAD_STATE_COUNT,
               g_pet.pet_count * VB_PET_PRELOAD_FRAMES_PER_STATE,
               (unsigned long)g_pet.preloaded_data_size,
               (unsigned long)loaded_compressed);
    result = 1;
finish:
    if (compressed) app_cache_free(compressed);
    if (fd >= 0) close(fd);
    return result;
}

static int vb_pet_preload_assets(void)
{
    int result = 0;
    if (vb_runtime_storage_take(VB_PET_PRELOAD_STORAGE_WAIT_MS) != RT_EOK) return 0;
    g_vb_pet_loader_phase = 20;
    g_vb_pet_loader_phase = 22;
    result = vb_pet_fill_preloaded_assets_unlocked();
finish:
    vb_runtime_storage_release();
    g_vb_pet_loader_phase = 0;
    if (!result) vb_pet_release_preloaded_assets();
    return result;
}

static void vb_pet_update_custom_frame(void)
{
    lv_img_dsc_t *image;
    int preload_state;
    if (!g_pet.custom_available || !g_pet.pet_image || g_pet.custom_frame_count < 1) return;
    if (g_pet.custom_frame_index < 0 || g_pet.custom_frame_index >= g_pet.custom_frame_count)
        g_pet.custom_frame_index = 0;
    if (g_pet.custom_displayed_frame == g_pet.custom_frame_index) return;
    preload_state = vb_pet_preload_state_index(g_pet.custom_state);
    if (preload_state < 0) return;
    image = &g_pet.preloaded_frames[g_pet.pet_index][preload_state]
                                         [g_pet.custom_frame_index];
    if (!image->data) return;
    lv_img_set_src(g_pet.pet_image, image);
    /* Native Petdex frames define the action. Keep the image geometry stable. */
    lv_obj_set_pos(g_pet.pet_image, VB_PET_IMAGE_X, VB_PET_IMAGE_Y);
    lv_obj_clear_flag(g_pet.pet_image, LV_OBJ_FLAG_HIDDEN);
    g_pet.custom_displayed_frame = g_pet.custom_frame_index;
}

static int vb_pet_activate_preloaded_state(int index, int state)
{
    int frame_count;
    int preload_state;
    if (!g_pet.preloaded_data || index < 0 || index >= g_pet.pet_count ||
        state < 0 || state >= VB_PET_ASSET_STATE_COUNT) return 0;
    preload_state = vb_pet_preload_state_index(state);
    if (preload_state < 0) return 0;
    frame_count = g_pet.preloaded_frame_counts[index][preload_state];
    if (frame_count < 1 || frame_count > VB_PET_RUNTIME_FRAME_LIMIT ||
        !g_pet.preloaded_frames[index][preload_state][0].data) return 0;
    g_pet.pet_index = index;
    vb_pet_copy(g_pet.pet_slug, sizeof(g_pet.pet_slug), g_pet.pet_slugs[index]);
    vb_pet_copy(g_pet.pet_name, sizeof(g_pet.pet_name), g_pet.pet_names[index]);
    g_pet.custom_state = state;
    g_pet.custom_frame_count = frame_count;
    g_pet.custom_frame_index = 0;
    g_pet.custom_displayed_frame = -1;
    g_pet.custom_frame_ms = VB_PET_NATIVE_FRAME_MS;
    g_pet.custom_next_frame_at = rt_tick_get() +
        rt_tick_from_millisecond(g_pet.custom_frame_ms);
    g_pet.custom_available = 1;
    vb_pet_update_custom_frame();
    g_pet.dirty = 1;
    return 1;
}

static int vb_pet_select_index(int index, int persist)
{
    (void)persist;
    return vb_pet_activate_preloaded_state(index, vb_pet_asset_state_index());
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
    /* A left swipe advances; a right swipe returns to the previous task. */
    vb_pet_navigate_task(dx < 0 ? 1 : -1);
    rt_kprintf("[vb_runtime][codex_pet] task swipe %s dx=%d dy=%d\n",
               dx < 0 ? "next" : "prev", dx, dy);
    return 1;
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
        else return;
        dy = 0;
    }
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED ||
        code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED ||
        code == LV_EVENT_GESTURE)
        (void)vb_pet_handle_horizontal_swipe(dx, dy);
}

static void vb_pet_render(void)
{
    uint32_t color;
    uint32_t now;
    uint32_t sync_age_ms;
    int recent_count;
    int show_task_detail = 1;
    const char *task_text;
    if (!g_pet.active || !g_pet.root) return;
    now = rt_tick_get();
    sync_age_ms = g_pet.host_seen_at ? vb_pet_ticks_to_ms(now - g_pet.host_seen_at) : 0;
    color = vb_pet_state_color();
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
        int asset_state = vb_pet_asset_state_index();
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
    lv_label_set_text(g_pet.status_label, vb_pet_status_text());
    lv_obj_set_style_text_color(g_pet.status_label, lv_color_hex(color),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (g_pet.pet_name_label)
        lv_label_set_text(g_pet.pet_name_label, g_pet.pet_name[0] ? g_pet.pet_name : "Rocky");
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
    else task_text = g_pet.task_detail[0] ? g_pet.task_detail : "No active Codex tasks";
    lv_label_set_text(g_pet.transcript_label, task_text);
    if (show_task_detail)
        lv_obj_clear_flag(g_pet.transcript_label, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_pet.transcript_label, LV_OBJ_FLAG_HIDDEN);
    if (g_pet.task_count > 0)
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
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || g_pet.approval_pending ||
        g_pet.pet_count < 2) return;
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
        if (g_pet.state == VB_PET_NEEDS_INPUT) vb_pet_play_cue("needs_input");
        else if (g_pet.state == VB_PET_READY) vb_pet_play_cue("done");
        else if (g_pet.state == VB_PET_ERROR) vb_pet_play_cue("error");
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
    snapshot.preloaded_data_size = g_pet.preloaded_data_size;
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
    if (!root || !ops || !ops->send_action) return -RT_EINVAL;
    vb_codex_pet_stop();
    rt_memset(&g_pet, 0, sizeof(g_pet));
    g_pet.root = root;
    g_pet.ops = *ops;
    g_pet.active = 1;
    g_pet.state = VB_PET_DISCONNECTED;
    g_pet.task_state = VB_PET_IDLE;
    g_pet.rocky_frame_key = -1;
    g_pet.custom_state = -1;
    g_pet.custom_displayed_frame = -1;
    vb_pet_copy(g_pet.project, sizeof(g_pet.project), project);

    lv_obj_clean(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0b1118), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(root, lv_color_hex(0xf9fafb), LV_PART_MAIN | LV_STATE_DEFAULT);

    label = vb_pet_label(root, "Codex Companion", 0xf9fafb);
    lv_obj_set_pos(label, 30, 36);
    g_pet.connection_label = vb_pet_label(root, "Bridge offline", 0x94a3b8);
    lv_obj_set_width(g_pet.connection_label, 150);
    lv_obj_set_style_text_align(g_pet.connection_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_pet.connection_label, 220, 62);

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
        if (g_pet.pet_count > 1)
        {
            lv_obj_add_flag(g_pet.pet_image, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(g_pet.pet_image, vb_pet_image_event, LV_EVENT_CLICKED, RT_NULL);
        }
        else
        {
            lv_obj_clear_flag(g_pet.pet_image, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_add_flag(g_pet.pet_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.pet_tail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.left_ear, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.right_ear, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_pet.pet_face, LV_OBJ_FLAG_HIDDEN);
        if (g_pet.custom_available) vb_pet_update_custom_frame();
        else vb_pet_update_rocky(0);
    }

    g_pet.pet_name_label = vb_pet_label(root,
        g_pet.pet_name[0] ? g_pet.pet_name : "Rocky", 0x94a3b8);
    lv_obj_set_width(g_pet.pet_name_label, 120);
    lv_obj_set_style_text_align(g_pet.pet_name_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_pet.pet_name_label, 240, 36);

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
    vb_pet_rgb_tick(rt_tick_get());
    vb_pet_reset_flow_queue(1);
    vb_pet_publish_status();
    return RT_EOK;
}

void vb_codex_pet_stop(void)
{
    vb_pet_reset_flow_queue(0);
    if (!g_pet.active)
    {
        vb_pet_publish_status();
        return;
    }
    if ((g_pet.state == VB_PET_RECORDING || g_pet.state == VB_PET_TRANSCRIBING) &&
        g_pet.ops.voice_clear) g_pet.ops.voice_clear();
    g_pet.active = 0;
    if (g_pet.root) lv_obj_remove_event_cb(g_pet.root, vb_pet_touch_event);
    if (g_pet.ops.rgb_set) (void)g_pet.ops.rgb_set("off");
    vb_pet_detach_custom_image();
    vb_pet_release_preloaded_assets();
    vb_pet_release_rocky_frames();
    rt_memset(&g_pet, 0, sizeof(g_pet));
    vb_pet_publish_status();
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
        vb_pet_copy(g_pet.pending_pet_slug, sizeof(g_pet.pending_pet_slug), payload);
        g_pet.pending_pet_attempts = 0;
        g_pet.pending_pet_retry_at = 0;
        g_pet.pending_pet_selection = 1;
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
        g_pet.state = VB_PET_DISCONNECTED;
        g_pet.dirty = 1;
    }
    if (!g_pet.sync_label_updated_at ||
        (int32_t)(now - g_pet.sync_label_updated_at) >= (int32_t)RT_TICK_PER_SECOND)
    {
        g_pet.sync_label_updated_at = now;
        g_pet.dirty = 1;
    }
    if (g_pet.custom_available && g_pet.custom_frame_count > 0)
    {
        if ((int32_t)(now - g_pet.custom_next_frame_at) >= 0)
        {
            g_pet.custom_frame_index = (g_pet.custom_frame_index + 1) % g_pet.custom_frame_count;
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
                "\"frames\":%d,\"frame\":%d,\"frameMs\":%d,"
                "\"preloadedBytes\":%lu,\"uiTicks\":%lu,\"loaderPhase\":%d,"
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
                snapshot.custom_frame_count,
                snapshot.custom_frame_index,
                snapshot.custom_frame_ms,
                (unsigned long)snapshot.preloaded_data_size,
                (unsigned long)snapshot.ui_tick_count,
                g_vb_pet_loader_phase,
                (unsigned long)snapshot.queued_flows,
                (unsigned long)snapshot.dropped_flows,
                vb_pet_indicator_name(snapshot.state),
                snapshot.rgb_color[0] ? snapshot.rgb_color : "off");
    dst[cap - 1] = '\0';
    return RT_EOK;
}
