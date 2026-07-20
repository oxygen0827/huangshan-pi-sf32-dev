#include "vb_runtime_storage.h"

static struct rt_mutex g_vb_runtime_storage_mutex;
static int g_vb_runtime_storage_ready;

static int vb_runtime_storage_init(void)
{
    if (rt_mutex_init(&g_vb_runtime_storage_mutex, "vbstor", RT_IPC_FLAG_PRIO) != RT_EOK)
        return -RT_ERROR;
    g_vb_runtime_storage_ready = 1;
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(vb_runtime_storage_init);

int vb_runtime_storage_take(uint32_t timeout_ms)
{
    rt_int32_t timeout;
    if (!g_vb_runtime_storage_ready) return -RT_ERROR;
    timeout = timeout_ms ? (rt_int32_t)rt_tick_from_millisecond(timeout_ms) : 0;
    return rt_mutex_take(&g_vb_runtime_storage_mutex, timeout) == RT_EOK ?
        RT_EOK : -RT_ETIMEOUT;
}

void vb_runtime_storage_release(void)
{
    if (g_vb_runtime_storage_ready)
        (void)rt_mutex_release(&g_vb_runtime_storage_mutex);
}
