#include <rtthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dfs_posix.h>

#include "vb_runtime_audio.h"
#include "vb_runtime_storage.h"
#include "app_mem.h"

#if defined(AUDIO) && defined(AUDIO_USING_MANAGER)
#include "audio_server.h"
#define VB_AUDIO_BUILT 1
#else
#define VB_AUDIO_BUILT 0
#endif

#define VB_AUDIO_PATH_MAX 192
#define VB_AUDIO_JSON_MAX 512
#define VB_AUDIO_IO_CHUNK VB_RUNTIME_STORAGE_IO_CHUNK_BYTES
#define VB_AUDIO_CACHE_SIZE 4096
#define VB_AUDIO_THREAD_STACK 4096
#define VB_AUDIO_DRAIN_TIMEOUT_MS 3000
#define VB_AUDIO_DEFAULT_VOLUME 8
#define VB_AUDIO_TONE_SAMPLE_RATE 16000
#define VB_AUDIO_TONE_DURATION_SECONDS 1
#define VB_AUDIO_CODEX_CUE_COUNT 5
#define VB_AUDIO_CODEX_CUE_MAX_BYTES (64u * 1024u)

typedef struct
{
    int playing;
    int ready;
    int suspended;
    int stop_requested;
    int last_error;
    int volume;
    uint32_t sequence;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t data_bytes;
    uint32_t played_bytes;
    char path[VB_AUDIO_PATH_MAX];
    int tone_requested;
    int tone_continuous;
    int capture_reserved;
    rt_thread_t worker;
#if VB_AUDIO_BUILT
    audio_client_t client;
#endif
} vb_audio_state_t;

typedef struct
{
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} vb_wav_info_t;

typedef struct
{
    const char *path;
    uint8_t *data;
    uint32_t size;
} vb_audio_cached_wav_t;

static vb_audio_state_t g_vb_audio = {
    .volume = VB_AUDIO_DEFAULT_VOLUME,
};
static char g_vb_audio_msh_json[VB_AUDIO_JSON_MAX];
static uint8_t *g_vb_audio_codex_cue_data;
static vb_audio_cached_wav_t g_vb_audio_codex_cues[VB_AUDIO_CODEX_CUE_COUNT] = {
    {"/sdcard/apps/codex_pet/assets/listening.wav", RT_NULL, 0},
    {"/sdcard/apps/codex_pet/assets/submitted.wav", RT_NULL, 0},
    {"/sdcard/apps/codex_pet/assets/needs_input.wav", RT_NULL, 0},
    {"/sdcard/apps/codex_pet/assets/done.wav", RT_NULL, 0},
    {"/sdcard/apps/codex_pet/assets/error.wav", RT_NULL, 0},
};

static uint16_t vb_audio_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t vb_audio_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int vb_audio_read_exact(int fd, void *buffer, rt_size_t length)
{
    rt_size_t total = 0;
    while (total < length)
    {
        int got = read(fd, (uint8_t *)buffer + total, length - total);
        if (got <= 0) return -RT_ERROR;
        total += (rt_size_t)got;
    }
    return RT_EOK;
}

static int vb_audio_parse_wav(int fd, vb_wav_info_t *info)
{
    uint8_t header[12];
    int have_format = 0;
    int have_data = 0;
    if (!info || lseek(fd, 0, SEEK_SET) < 0) return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    if (vb_audio_read_exact(fd, header, sizeof(header)) != RT_EOK) return -RT_ERROR;
    if (rt_memcmp(header, "RIFF", 4) != 0 || rt_memcmp(header + 8, "WAVE", 4) != 0)
    {
        return -RT_EINVAL;
    }
    while (!have_data)
    {
        uint8_t chunk[8];
        uint32_t size;
        off_t payload;
        if (vb_audio_read_exact(fd, chunk, sizeof(chunk)) != RT_EOK) break;
        size = vb_audio_le32(chunk + 4);
        payload = lseek(fd, 0, SEEK_CUR);
        if (payload < 0) return -RT_ERROR;
        if (rt_memcmp(chunk, "fmt ", 4) == 0)
        {
            uint8_t format[16];
            if (size < sizeof(format) || vb_audio_read_exact(fd, format, sizeof(format)) != RT_EOK)
            {
                return -RT_EINVAL;
            }
            if (vb_audio_le16(format) != 1) return -RT_ENOSYS;
            info->channels = vb_audio_le16(format + 2);
            info->sample_rate = vb_audio_le32(format + 4);
            info->bits_per_sample = vb_audio_le16(format + 14);
            have_format = 1;
        }
        else if (rt_memcmp(chunk, "data", 4) == 0)
        {
            info->data_offset = (uint32_t)payload;
            info->data_size = size;
            have_data = 1;
        }
        if (!have_data && lseek(fd, payload + (off_t)size + (size & 1u), SEEK_SET) < 0)
        {
            return -RT_ERROR;
        }
    }
    if (!have_format || !have_data || info->data_size == 0) return -RT_EINVAL;
    if ((info->channels != 1 && info->channels != 2) || info->bits_per_sample != 16 ||
        info->sample_rate < 8000 || info->sample_rate > 48000)
    {
        return -RT_ENOSYS;
    }
    if (lseek(fd, (off_t)info->data_offset, SEEK_SET) < 0) return -RT_ERROR;
    return RT_EOK;
}

static int vb_audio_parse_wav_memory(const uint8_t *data, uint32_t size,
                                     vb_wav_info_t *info)
{
    uint32_t offset = 12;
    int have_format = 0;
    if (!data || !info || size < 20 || rt_memcmp(data, "RIFF", 4) != 0 ||
        rt_memcmp(data + 8, "WAVE", 4) != 0) return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    while (offset <= size - 8u)
    {
        const uint8_t *chunk = data + offset;
        uint32_t chunk_size = vb_audio_le32(chunk + 4);
        uint32_t payload = offset + 8u;
        if (chunk_size > size - payload) return -RT_EINVAL;
        if (rt_memcmp(chunk, "fmt ", 4) == 0)
        {
            if (chunk_size < 16 || vb_audio_le16(data + payload) != 1) return -RT_ENOSYS;
            info->channels = vb_audio_le16(data + payload + 2);
            info->sample_rate = vb_audio_le32(data + payload + 4);
            info->bits_per_sample = vb_audio_le16(data + payload + 14);
            have_format = 1;
        }
        else if (rt_memcmp(chunk, "data", 4) == 0)
        {
            info->data_offset = payload;
            info->data_size = chunk_size;
            break;
        }
        if (chunk_size + (chunk_size & 1u) > size - payload) return -RT_EINVAL;
        offset = payload + chunk_size + (chunk_size & 1u);
    }
    if (!have_format || info->data_size == 0 ||
        (info->channels != 1 && info->channels != 2) ||
        info->bits_per_sample != 16 || info->sample_rate < 8000 || info->sample_rate > 48000)
        return -RT_ENOSYS;
    return RT_EOK;
}

static const vb_audio_cached_wav_t *vb_audio_find_cached_wav(const char *path)
{
    int index;
    if (!path || !g_vb_audio_codex_cue_data) return RT_NULL;
    for (index = 0; index < VB_AUDIO_CODEX_CUE_COUNT; index++)
    {
        if (g_vb_audio_codex_cues[index].data &&
            rt_strcmp(path, g_vb_audio_codex_cues[index].path) == 0)
            return &g_vb_audio_codex_cues[index];
    }
    return RT_NULL;
}

static int vb_audio_is_codex_cue_path(const char *path)
{
    int index;
    if (!path) return 0;
    for (index = 0; index < VB_AUDIO_CODEX_CUE_COUNT; index++)
        if (rt_strcmp(path, g_vb_audio_codex_cues[index].path) == 0) return 1;
    return 0;
}

#if VB_AUDIO_BUILT
static int vb_audio_callback(audio_server_callback_cmt_t command,
                             void *callback_userdata, uint32_t reserved)
{
    vb_audio_state_t *state = (vb_audio_state_t *)callback_userdata;
    (void)reserved;
    if (!state) return 0;
    if (command == as_callback_cmd_suspended) state->suspended = 1;
    else if (command == as_callback_cmd_resumed || command == as_callback_cmd_opened) state->suspended = 0;
    else if (command == as_callback_cmd_closed) state->playing = 0;
    return 0;
}

static int vb_audio_write_all(const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0;
    int idle_retries = 0;
    while (offset < length && !g_vb_audio.stop_requested)
    {
        int wrote = audio_write(g_vb_audio.client, (uint8_t *)data + offset, length - offset);
        if (wrote > 0)
        {
            offset += (uint32_t)wrote;
            g_vb_audio.played_bytes += (uint32_t)wrote;
            idle_retries = 0;
            rt_thread_mdelay(1);
        }
        else
        {
            if (++idle_retries > 500) return -RT_ETIMEOUT;
            rt_thread_mdelay(g_vb_audio.suspended ? 20 : 5);
        }
    }
    return g_vb_audio.stop_requested ? -RT_EINTR : RT_EOK;
}

static void vb_audio_drain(void)
{
    uint32_t elapsed = 0;
    while (!g_vb_audio.stop_requested && elapsed < VB_AUDIO_DRAIN_TIMEOUT_MS)
    {
        uint32_t queued = 0;
        if (audio_ioctl(g_vb_audio.client, AUDIO_IOCTL_BYTES_IN_CACHE, &queued) != 0 || queued == 0)
        {
            break;
        }
        rt_thread_mdelay(20);
        elapsed += 20;
    }
}
#endif

static void vb_audio_worker(void *parameter)
{
    int fd = -1;
    int result = -RT_ERROR;
    vb_wav_info_t wav;
    uint8_t *buffer = RT_NULL;
    const vb_audio_cached_wav_t *cached = RT_NULL;
    const uint8_t *cached_cursor = RT_NULL;
    uint32_t remaining;
    uint32_t tone_sample = 0;
    int storage_locked = 0;
    (void)parameter;

    if (g_vb_audio.tone_requested)
    {
        wav.sample_rate = VB_AUDIO_TONE_SAMPLE_RATE;
        wav.channels = 1;
        wav.bits_per_sample = 16;
        wav.data_offset = 0;
        wav.data_size = g_vb_audio.tone_continuous ? 0u :
                        VB_AUDIO_TONE_SAMPLE_RATE * 2u * VB_AUDIO_TONE_DURATION_SECONDS;
    }
    else
    {
        cached = vb_audio_find_cached_wav(g_vb_audio.path);
        if (cached)
        {
            result = vb_audio_parse_wav_memory(cached->data, cached->size, &wav);
            if (result != RT_EOK) goto done;
            cached_cursor = cached->data + wav.data_offset;
        }
        else
        {
            if (vb_runtime_storage_take(15000) != RT_EOK)
            {
                result = -RT_ETIMEOUT;
                goto done;
            }
            storage_locked = 1;
            fd = open(g_vb_audio.path, O_RDONLY);
            if (fd < 0)
            {
                result = -RT_ERROR;
                goto done;
            }
            result = vb_audio_parse_wav(fd, &wav);
            if (result != RT_EOK) goto done;
        }
    }
    g_vb_audio.sample_rate = wav.sample_rate;
    g_vb_audio.channels = wav.channels;
    g_vb_audio.bits_per_sample = wav.bits_per_sample;
    g_vb_audio.data_bytes = wav.data_size;

#if VB_AUDIO_BUILT
    {
        audio_parameter_t params;
        rt_memset(&params, 0, sizeof(params));
        params.write_samplerate = wav.sample_rate;
        params.write_cache_size = VB_AUDIO_CACHE_SIZE;
        params.write_channnel_num = (uint8_t)wav.channels;
        params.write_bits_per_sample = (uint8_t)wav.bits_per_sample;
        audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, (uint8_t)g_vb_audio.volume);
        g_vb_audio.client = audio_open(AUDIO_TYPE_LOCAL_MUSIC, AUDIO_TX, &params,
                                       vb_audio_callback, &g_vb_audio);
    }
    if (!g_vb_audio.client)
    {
        result = -RT_ERROR;
        goto done;
    }
    buffer = (uint8_t *)rt_malloc(VB_AUDIO_IO_CHUNK);
    if (!buffer)
    {
        result = -RT_ENOMEM;
        goto done;
    }
    remaining = wav.data_size;
    result = RT_EOK;
    while ((g_vb_audio.tone_continuous || remaining > 0) && !g_vb_audio.stop_requested)
    {
        uint32_t wanted = g_vb_audio.tone_continuous || remaining > VB_AUDIO_IO_CHUNK ?
                          VB_AUDIO_IO_CHUNK : remaining;
        int got;
        if (g_vb_audio.tone_requested)
        {
            static const int16_t sine_1khz[16] = {
                0, 9184, 16971, 22173, 24000, 22173, 16971, 9184,
                0, -9184, -16971, -22173, -24000, -22173, -16971, -9184
            };
            uint32_t i;
            int16_t *samples = (int16_t *)buffer;
            for (i = 0; i < wanted / 2u; i++)
            {
                samples[i] = sine_1khz[tone_sample & 15u];
                tone_sample++;
            }
            got = (int)wanted;
        }
        else if (cached_cursor)
        {
            rt_memcpy(buffer, cached_cursor, wanted);
            cached_cursor += wanted;
            got = (int)wanted;
        }
        else
        {
            got = read(fd, buffer, wanted);
        }
        if (got <= 0)
        {
            result = -RT_ERROR;
            break;
        }
        result = vb_audio_write_all(buffer, (uint32_t)got);
        if (result != RT_EOK) break;
        if (!g_vb_audio.tone_continuous) remaining -= (uint32_t)got;
    }
    if (result == RT_EOK)
    {
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
        if (storage_locked)
        {
            vb_runtime_storage_release();
            storage_locked = 0;
        }
        vb_audio_drain();
    }
#else
    remaining = 0;
    result = -RT_ENOSYS;
#endif

done:
#if VB_AUDIO_BUILT
    if (g_vb_audio.client)
    {
        audio_close(g_vb_audio.client);
        g_vb_audio.client = RT_NULL;
    }
#endif
    if (buffer) rt_free(buffer);
    if (fd >= 0) close(fd);
    if (storage_locked) vb_runtime_storage_release();
    g_vb_audio.playing = 0;
    g_vb_audio.ready = result == RT_EOK ? 1 : 0;
    /* A requested stop is a successful control action, not a playback fault. */
    g_vb_audio.last_error = result == -RT_EINTR ? RT_EOK : result;
    g_vb_audio.stop_requested = 0;
    rt_kprintf("[vb_runtime][audio] finished seq=%lu rc=%d bytes=%lu/%lu\n",
               (unsigned long)g_vb_audio.sequence, g_vb_audio.last_error,
               (unsigned long)g_vb_audio.played_bytes,
               (unsigned long)g_vb_audio.data_bytes);
    g_vb_audio.worker = RT_NULL;
}

int vb_runtime_audio_available(void)
{
    return VB_AUDIO_BUILT;
}

int vb_runtime_audio_is_playing(void)
{
    return g_vb_audio.worker || g_vb_audio.playing;
}

int vb_runtime_audio_prepare_capture(void)
{
    int result;
    if (g_vb_audio.capture_reserved) return -RT_EBUSY;
    g_vb_audio.capture_reserved = 1;
    result = vb_runtime_audio_stop();
    if (result != RT_EOK) g_vb_audio.capture_reserved = 0;
    return result;
}

void vb_runtime_audio_finish_capture(void)
{
    g_vb_audio.capture_reserved = 0;
}

int vb_runtime_audio_preload_codex_cues(void)
{
    uint32_t sizes[VB_AUDIO_CODEX_CUE_COUNT];
    uint32_t total = 0;
    uint8_t *data = RT_NULL;
    uint8_t *cursor;
    int index;
    int result = -RT_ERROR;
    if (g_vb_audio_codex_cue_data) return RT_EOK;
    if (g_vb_audio.worker || g_vb_audio.playing) return -RT_EBUSY;
    if (vb_runtime_storage_take(3000) != RT_EOK) return -RT_ETIMEOUT;
    for (index = 0; index < VB_AUDIO_CODEX_CUE_COUNT; index++)
    {
        struct stat st;
        if (stat(g_vb_audio_codex_cues[index].path, &st) != 0 || st.st_size <= 0 ||
            st.st_size > (off_t)(VB_AUDIO_CODEX_CUE_MAX_BYTES - total))
            goto finish;
        sizes[index] = (uint32_t)st.st_size;
        total += sizes[index];
    }
    data = (uint8_t *)app_cache_alloc(total, IMAGE_CACHE_PSRAM);
    if (!data) goto finish;
    cursor = data;
    for (index = 0; index < VB_AUDIO_CODEX_CUE_COUNT; index++)
    {
        vb_wav_info_t wav;
        int fd = open(g_vb_audio_codex_cues[index].path, O_RDONLY);
        if (fd < 0 || vb_audio_read_exact(fd, cursor, sizes[index]) != RT_EOK)
        {
            if (fd >= 0) close(fd);
            goto finish;
        }
        close(fd);
        if (vb_audio_parse_wav_memory(cursor, sizes[index], &wav) != RT_EOK) goto finish;
        g_vb_audio_codex_cues[index].data = cursor;
        g_vb_audio_codex_cues[index].size = sizes[index];
        cursor += sizes[index];
        rt_thread_yield();
    }
    g_vb_audio_codex_cue_data = data;
    data = RT_NULL;
    rt_kprintf("[vb_runtime][audio] preloaded Codex cues=%d bytes=%lu\n",
               VB_AUDIO_CODEX_CUE_COUNT, (unsigned long)total);
    result = RT_EOK;
finish:
    vb_runtime_storage_release();
    if (data) app_cache_free(data);
    if (result != RT_EOK)
    {
        for (index = 0; index < VB_AUDIO_CODEX_CUE_COUNT; index++)
        {
            g_vb_audio_codex_cues[index].data = RT_NULL;
            g_vb_audio_codex_cues[index].size = 0;
        }
    }
    return result;
}

void vb_runtime_audio_release_codex_cues(void)
{
    int index;
    (void)vb_runtime_audio_stop();
    if (g_vb_audio_codex_cue_data) app_cache_free(g_vb_audio_codex_cue_data);
    g_vb_audio_codex_cue_data = RT_NULL;
    for (index = 0; index < VB_AUDIO_CODEX_CUE_COUNT; index++)
    {
        g_vb_audio_codex_cues[index].data = RT_NULL;
        g_vb_audio_codex_cues[index].size = 0;
    }
}

int vb_runtime_audio_play_wav(const char *path)
{
    rt_thread_t worker;
    if (!VB_AUDIO_BUILT) return -RT_ENOSYS;
    if (g_vb_audio.capture_reserved) return -RT_EBUSY;
    if (!path || rt_strncmp(path, "/sdcard/apps/", 13) != 0 || strstr(path, "..")) return -RT_EINVAL;
    if (vb_audio_is_codex_cue_path(path) && !vb_audio_find_cached_wav(path)) return -RT_EIO;
    if (g_vb_audio.worker || g_vb_audio.playing) return -RT_EBUSY;
    g_vb_audio.sequence++;
    g_vb_audio.played_bytes = 0;
    g_vb_audio.data_bytes = 0;
    g_vb_audio.sample_rate = 0;
    g_vb_audio.channels = 0;
    g_vb_audio.bits_per_sample = 0;
    g_vb_audio.ready = 0;
    g_vb_audio.suspended = 0;
    g_vb_audio.stop_requested = 0;
    g_vb_audio.last_error = RT_EOK;
    g_vb_audio.tone_requested = 0;
    g_vb_audio.tone_continuous = 0;
    rt_strncpy(g_vb_audio.path, path, sizeof(g_vb_audio.path) - 1);
    g_vb_audio.path[sizeof(g_vb_audio.path) - 1] = '\0';
    worker = rt_thread_create("vbaudio", vb_audio_worker, RT_NULL,
                              VB_AUDIO_THREAD_STACK,
                              RT_THREAD_PRIORITY_MIDDLE + 8,
                              RT_THREAD_TICK_DEFAULT);
    if (!worker)
    {
        g_vb_audio.last_error = -RT_ENOMEM;
        return -RT_ENOMEM;
    }
    g_vb_audio.worker = worker;
    g_vb_audio.playing = 1;
    rt_thread_startup(worker);
    rt_kprintf("[vb_runtime][audio] play seq=%lu path=%s volume=%d\n",
               (unsigned long)g_vb_audio.sequence, g_vb_audio.path, g_vb_audio.volume);
    return RT_EOK;
}

int vb_runtime_audio_play_tone(int continuous)
{
    rt_thread_t worker;
    if (!VB_AUDIO_BUILT) return -RT_ENOSYS;
    if (g_vb_audio.capture_reserved) return -RT_EBUSY;
    if (g_vb_audio.worker || g_vb_audio.playing) return -RT_EBUSY;
    g_vb_audio.sequence++;
    g_vb_audio.played_bytes = 0;
    g_vb_audio.data_bytes = 0;
    g_vb_audio.sample_rate = 0;
    g_vb_audio.channels = 0;
    g_vb_audio.bits_per_sample = 0;
    g_vb_audio.ready = 0;
    g_vb_audio.suspended = 0;
    g_vb_audio.stop_requested = 0;
    g_vb_audio.last_error = RT_EOK;
    g_vb_audio.tone_requested = 1;
    g_vb_audio.tone_continuous = continuous ? 1 : 0;
    rt_strncpy(g_vb_audio.path, continuous ? "<1khz-continuous>" : "<1khz-tone>",
               sizeof(g_vb_audio.path) - 1);
    g_vb_audio.path[sizeof(g_vb_audio.path) - 1] = '\0';
    worker = rt_thread_create("vbaudio", vb_audio_worker, RT_NULL,
                              VB_AUDIO_THREAD_STACK,
                              RT_THREAD_PRIORITY_MIDDLE + 8,
                              RT_THREAD_TICK_DEFAULT);
    if (!worker)
    {
        g_vb_audio.last_error = -RT_ENOMEM;
        return -RT_ENOMEM;
    }
    g_vb_audio.worker = worker;
    g_vb_audio.playing = 1;
    rt_thread_startup(worker);
    rt_kprintf("[vb_runtime][audio] tone seq=%lu volume=%d continuous=%d\n",
               (unsigned long)g_vb_audio.sequence, g_vb_audio.volume,
               g_vb_audio.tone_continuous);
    return RT_EOK;
}

int vb_runtime_audio_stop(void)
{
    uint32_t waited = 0;
    if (!g_vb_audio.worker && !g_vb_audio.playing) return RT_EOK;
    g_vb_audio.stop_requested = 1;
    while (g_vb_audio.worker && waited < 2000)
    {
        rt_thread_mdelay(20);
        waited += 20;
    }
    return g_vb_audio.worker ? -RT_ETIMEOUT : RT_EOK;
}

int vb_runtime_audio_set_volume(int volume)
{
    int result;
    if (volume < 0 || volume > 15) return -RT_EINVAL;
#if VB_AUDIO_BUILT
    result = audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, (uint8_t)volume);
    if (result == 0) g_vb_audio.volume = volume;
    return result;
#else
    (void)result;
    return -RT_ENOSYS;
#endif
}

int vb_runtime_audio_read_json(char *dst, rt_size_t cap)
{
    int used;
    if (!dst || cap == 0) return -RT_EINVAL;
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"available\":%d,\"playing\":%d,\"ready\":%d,"
                       "\"suspended\":%d,\"seq\":%lu,\"rate\":%lu,\"channels\":%lu,"
                       "\"bits\":%lu,\"bytes\":%lu,\"total\":%lu,\"volume\":%d,"
                       "\"err\":%d,\"capture_reserved\":%d,\"cachedCues\":%d,"
                       "\"path\":\"%s\"}",
                       VB_RUNTIME_AUDIO_API_VERSION, VB_AUDIO_BUILT,
                       g_vb_audio.playing, g_vb_audio.ready, g_vb_audio.suspended,
                       (unsigned long)g_vb_audio.sequence,
                       (unsigned long)g_vb_audio.sample_rate,
                       (unsigned long)g_vb_audio.channels,
                       (unsigned long)g_vb_audio.bits_per_sample,
                       (unsigned long)g_vb_audio.played_bytes,
                       (unsigned long)g_vb_audio.data_bytes,
                       g_vb_audio.volume, g_vb_audio.last_error,
                       g_vb_audio.capture_reserved,
                       g_vb_audio_codex_cue_data ? VB_AUDIO_CODEX_CUE_COUNT : 0,
                       g_vb_audio.path);
    dst[cap - 1] = '\0';
    return used >= 0 && used < (int)cap ? RT_EOK : -RT_EFULL;
}

int vb_runtime_audio_format_text(const char *selector, char *dst, rt_size_t cap)
{
    if (!selector || !dst || cap == 0) return 0;
    if (rt_strcmp(selector, "state") == 0)
    {
        rt_snprintf(dst, cap, "%s", g_vb_audio.playing ?
                    (g_vb_audio.suspended ? "suspended" : "playing") :
                    (g_vb_audio.ready ? "finished" : "idle"));
        return 1;
    }
    if (rt_strcmp(selector, "progress") == 0)
    {
        rt_snprintf(dst, cap, "%lu/%lu",
                    (unsigned long)g_vb_audio.played_bytes,
                    (unsigned long)g_vb_audio.data_bytes);
        return 1;
    }
    if (rt_strcmp(selector, "format") == 0)
    {
        rt_snprintf(dst, cap, "%luHz %luch %lub",
                    (unsigned long)g_vb_audio.sample_rate,
                    (unsigned long)g_vb_audio.channels,
                    (unsigned long)g_vb_audio.bits_per_sample);
        return 1;
    }
    if (rt_strcmp(selector, "volume") == 0)
    {
        rt_snprintf(dst, cap, "%d/15", g_vb_audio.volume);
        return 1;
    }
    return 0;
}

void vb_runtime_audio_shutdown(void)
{
    vb_runtime_audio_release_codex_cues();
}

static int vb_runtime_audio_msh(int argc, char **argv)
{
    char *json = g_vb_audio_msh_json;
    int result;
    if (argc < 2 || rt_strcmp(argv[1], "status") == 0)
    {
        result = vb_runtime_audio_read_json(json, VB_AUDIO_JSON_MAX);
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "play") == 0 && argc >= 3)
    {
        result = vb_runtime_audio_play_wav(argv[2]);
        vb_runtime_audio_read_json(json, VB_AUDIO_JSON_MAX);
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "tone") == 0)
    {
        result = vb_runtime_audio_play_tone(0);
        vb_runtime_audio_read_json(json, VB_AUDIO_JSON_MAX);
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "stop") == 0)
    {
        result = vb_runtime_audio_stop();
        vb_runtime_audio_read_json(json, VB_AUDIO_JSON_MAX);
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "volume") == 0 && argc >= 3)
    {
        char *end = RT_NULL;
        long volume = strtol(argv[2], &end, 10);
        result = end != argv[2] && *end == '\0' && volume >= 0 && volume <= 15 ?
                 vb_runtime_audio_set_volume((int)volume) : -RT_EINVAL;
        vb_runtime_audio_read_json(json, VB_AUDIO_JSON_MAX);
        rt_kprintf("%s\n", json);
        return result;
    }
    rt_kprintf("usage: vb_runtime_audio [status|play <absolute-wav>|tone|stop|volume <0-15>]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_audio_msh, vb_runtime_audio, control VibeBoard WAV audio playback);
