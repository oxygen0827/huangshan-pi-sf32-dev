#include <rtthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "vb_runtime_audio.h"

#if defined(AUDIO) && defined(AUDIO_USING_MANAGER)
#include "audio_server.h"
#define VB_AUDIO_BUILT 1
#else
#define VB_AUDIO_BUILT 0
#endif

#define VB_AUDIO_PATH_MAX 192
#define VB_AUDIO_IO_CHUNK 1024
#define VB_AUDIO_CACHE_SIZE 4096
#define VB_AUDIO_THREAD_STACK 4096
#define VB_AUDIO_DRAIN_TIMEOUT_MS 3000
#define VB_AUDIO_DEFAULT_VOLUME 8

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

static vb_audio_state_t g_vb_audio = {
    .volume = VB_AUDIO_DEFAULT_VOLUME,
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
    uint32_t remaining;
    (void)parameter;

    fd = open(g_vb_audio.path, O_RDONLY);
    if (fd < 0)
    {
        result = -RT_ERROR;
        goto done;
    }
    result = vb_audio_parse_wav(fd, &wav);
    if (result != RT_EOK) goto done;
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
    while (remaining > 0 && !g_vb_audio.stop_requested)
    {
        uint32_t wanted = remaining > VB_AUDIO_IO_CHUNK ? VB_AUDIO_IO_CHUNK : remaining;
        int got = read(fd, buffer, wanted);
        if (got <= 0)
        {
            result = -RT_ERROR;
            break;
        }
        result = vb_audio_write_all(buffer, (uint32_t)got);
        if (result != RT_EOK) break;
        remaining -= (uint32_t)got;
    }
    if (result == RT_EOK) vb_audio_drain();
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
    g_vb_audio.playing = 0;
    g_vb_audio.ready = result == RT_EOK ? 1 : 0;
    g_vb_audio.last_error = g_vb_audio.stop_requested ? -RT_EINTR : result;
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

int vb_runtime_audio_play_wav(const char *path)
{
    rt_thread_t worker;
    if (!VB_AUDIO_BUILT) return -RT_ENOSYS;
    if (!path || rt_strncmp(path, "/sdcard/apps/", 13) != 0 || strstr(path, "..")) return -RT_EINVAL;
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
    g_vb_audio.last_error = 1;
    rt_strncpy(g_vb_audio.path, path, sizeof(g_vb_audio.path) - 1);
    g_vb_audio.path[sizeof(g_vb_audio.path) - 1] = '\0';
    worker = rt_thread_create("vbaudio", vb_audio_worker, RT_NULL,
                              VB_AUDIO_THREAD_STACK,
                              RT_THREAD_PRIORITY_MIDDLE + 3,
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
                       "\"err\":%d,\"path\":\"%s\"}",
                       VB_RUNTIME_AUDIO_API_VERSION, VB_AUDIO_BUILT,
                       g_vb_audio.playing, g_vb_audio.ready, g_vb_audio.suspended,
                       (unsigned long)g_vb_audio.sequence,
                       (unsigned long)g_vb_audio.sample_rate,
                       (unsigned long)g_vb_audio.channels,
                       (unsigned long)g_vb_audio.bits_per_sample,
                       (unsigned long)g_vb_audio.played_bytes,
                       (unsigned long)g_vb_audio.data_bytes,
                       g_vb_audio.volume, g_vb_audio.last_error, g_vb_audio.path);
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
    (void)vb_runtime_audio_stop();
}

static int vb_runtime_audio_msh(int argc, char **argv)
{
    char json[512];
    int result;
    if (argc < 2 || rt_strcmp(argv[1], "status") == 0)
    {
        result = vb_runtime_audio_read_json(json, sizeof(json));
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "play") == 0 && argc >= 3)
    {
        result = vb_runtime_audio_play_wav(argv[2]);
        vb_runtime_audio_read_json(json, sizeof(json));
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "stop") == 0)
    {
        result = vb_runtime_audio_stop();
        vb_runtime_audio_read_json(json, sizeof(json));
        rt_kprintf("%s\n", json);
        return result;
    }
    if (rt_strcmp(argv[1], "volume") == 0 && argc >= 3)
    {
        char *end = RT_NULL;
        long volume = strtol(argv[2], &end, 10);
        result = end != argv[2] && *end == '\0' && volume >= 0 && volume <= 15 ?
                 vb_runtime_audio_set_volume((int)volume) : -RT_EINVAL;
        vb_runtime_audio_read_json(json, sizeof(json));
        rt_kprintf("%s\n", json);
        return result;
    }
    rt_kprintf("usage: vb_runtime_audio [status|play <absolute-wav>|stop|volume <0-15>]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(vb_runtime_audio_msh, vb_runtime_audio, control VibeBoard WAV audio playback);
