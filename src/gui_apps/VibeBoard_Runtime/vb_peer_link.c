#include "vb_peer_link.h"

#include <dfs_posix.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(RT_USING_BLUETOOTH) && defined(BLUETOOTH) && defined(BSP_BLE_SIBLES) && \
    defined(BLE_GAP_CENTRAL) && defined(BLE_GATT_CLIENT)

#include "bf0_ble_common.h"
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "ble_connection_manager.h"

#define VB_PEER_STORAGE_DIR "/sdcard/apps/.peer"
#define VB_PEER_PAIR_FILE VB_PEER_STORAGE_DIR "/pair.bin"
#define VB_PEER_PAIR_TMP VB_PEER_STORAGE_DIR "/pair.tmp"
#define VB_PEER_HISTORY_FILE VB_PEER_STORAGE_DIR "/messages.bin"
#define VB_PEER_HISTORY_TMP VB_PEER_STORAGE_DIR "/messages.tmp"
#define VB_PEER_PAIR_MAGIC 0x56505052u
#define VB_PEER_HISTORY_MAGIC 0x56504853u
#define VB_PEER_STORAGE_VERSION 1u
#define VB_PEER_PROTOCOL_VERSION 1u
#define VB_PEER_FRAME_HEADER 12u
#define VB_PEER_PAIR_WINDOW_MS 60000u
#define VB_PEER_CANDIDATE_SETTLE_MS 400u
#define VB_PEER_ACK_TIMEOUT_MS 2000u
#define VB_PEER_MAX_RETRIES 3u
#define VB_PEER_ADV_ACTIVE 0x01u
#define VB_PEER_ADV_PAIRABLE 0x02u
#define VB_PEER_ADV_PAIRED 0x04u
#define VB_PEER_ROLE_CENTRAL 0
#define VB_PEER_ROLE_PERIPHERAL 1
#define VB_PEER_INVALID_CONN 0xffu
#define VB_PEER_EVENT_DATA_MAX (VB_PEER_FRAME_HEADER + VB_PEER_MAX_TEXT)
#define VB_PEER_SCAN_INTERVAL 0x60u
#define VB_PEER_SCAN_WINDOW 0x30u

enum vb_peer_att_list
{
    VB_PEER_SVC = 0,
    VB_PEER_RX_CHAR,
    VB_PEER_RX_VALUE,
    VB_PEER_TX_CHAR,
    VB_PEER_TX_VALUE,
    VB_PEER_TX_CCCD,
    VB_PEER_ATT_NB
};

typedef enum
{
    VB_PEER_PACKET_PAIR = 1,
    VB_PEER_PACKET_TEXT,
    VB_PEER_PACKET_ACK,
    VB_PEER_PACKET_PING
} vb_peer_packet_type_t;

typedef enum
{
    VB_PEER_PAIR_HELLO = 1,
    VB_PEER_PAIR_CONFIRM,
    VB_PEER_PAIR_CANCEL
} vb_peer_pair_subtype_t;

typedef enum
{
    VB_PEER_EVT_POWER = 1,
    VB_PEER_EVT_ADV,
    VB_PEER_EVT_CONNECTED,
    VB_PEER_EVT_DISCONNECTED,
    VB_PEER_EVT_SCAN_STOPPED,
    VB_PEER_EVT_CLIENT_READY,
    VB_PEER_EVT_RX,
    VB_PEER_EVT_CCCD,
    VB_PEER_EVT_MTU,
    VB_PEER_EVT_SEND_PENDING,
    VB_PEER_EVT_STATE_CHANGED
} vb_peer_event_type_t;

typedef struct
{
    uint8_t type;
    uint8_t conn_idx;
    uint8_t role;
    uint8_t addr_type;
    int8_t rssi;
    uint8_t flags;
    uint16_t mtu;
    uint16_t rx_handle;
    uint16_t tx_handle;
    uint16_t cccd_handle;
    uint16_t remote_handle;
    uint16_t length;
    bd_addr_t addr;
    uint8_t data[VB_PEER_EVENT_DATA_MAX];
} vb_peer_event_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint8_t addr_type;
    uint8_t reserved;
    uint8_t address[BD_ADDR_LEN];
    uint32_t crc32;
} vb_peer_pair_file_t;

typedef struct __attribute__((packed))
{
    uint32_t id;
    uint16_t length;
    uint8_t outgoing;
    uint8_t status;
    char text[VB_PEER_MAX_TEXT + 1];
} vb_peer_message_disk_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t write_index;
    uint16_t reserved;
    uint32_t tx_sequence;
    vb_peer_message_disk_t messages[VB_PEER_HISTORY_CAPACITY];
    uint32_t crc32;
} vb_peer_history_file_t;

typedef struct
{
    int active;
    uint8_t type;
    uint32_t id;
    uint16_t total;
    uint16_t received;
    uint16_t crc16;
    uint8_t data[VB_PEER_MAX_TEXT + 1];
} vb_peer_reassembly_t;

typedef struct
{
    int initialized;
    int power_on;
    int enabled;
    int paired;
    int pairing;
    int local_confirmed;
    int remote_confirmed;
    int scanning;
    int connecting;
    int client_ready;
    int peer_connected;
    int history_loaded;
    int pair_loaded;
    int last_error;
    int role;
    int8_t candidate_rssi;
    uint8_t conn_idx;
    uint8_t peer_addr_type;
    uint8_t candidate_addr_type;
    uint16_t mtu;
    uint16_t notify_cccd;
    uint16_t remote_rx_handle;
    uint16_t remote_tx_handle;
    uint16_t remote_tx_cccd;
    uint16_t remote_handle;
    uint32_t local_nonce;
    uint32_t remote_nonce;
    uint32_t pairing_code;
    uint32_t pairing_deadline;
    uint32_t candidate_tick;
    uint32_t reconnect_deadline;
    uint32_t connect_deadline;
    uint32_t hello_deadline;
    uint32_t ack_deadline;
    uint32_t awaiting_id;
    uint32_t tx_sequence;
    uint32_t rx_sequence;
    uint32_t revision;
    uint32_t retries;
    uint8_t reconnect_attempt;
    uint8_t pending_connect;
    bd_addr_t local_addr;
    bd_addr_t peer_addr;
    bd_addr_t candidate_addr;
    sibles_hdl service_handle;
    rt_mailbox_t mailbox;
    rt_mutex_t mutex;
    rt_thread_t worker;
    vb_peer_state_t state;
    vb_peer_reassembly_t rx;
    vb_peer_message_t history[VB_PEER_HISTORY_CAPACITY];
    int history_count;
    int history_write_index;
    int unread;
} vb_peer_env_t;

static vb_peer_env_t g_peer;

static void vb_peer_disconnect(void);

#ifndef SERIAL_UUID_16
#define SERIAL_UUID_16(x) {((uint8_t)((x) & 0xff)), ((uint8_t)((x) >> 8))}
#endif

#define VB_PEER_UUID128(a) { \
    0x56, 0x42, 0x52, 0x54, 0x50, 0x45, 0x45, 0x52, \
    0x00, 0x00, 0x00, (uint8_t)(a), 0x52, 0x54, 0x4d, 0x45 \
}

static uint8_t g_peer_service_uuid[ATT_UUID_128_LEN] = VB_PEER_UUID128(0x01);
static uint8_t g_peer_rx_uuid[ATT_UUID_128_LEN] = VB_PEER_UUID128(0x02);
static uint8_t g_peer_tx_uuid[ATT_UUID_128_LEN] = VB_PEER_UUID128(0x03);

BLE_GATT_SERVICE_DEFINE_128(vb_peer_att_db)
{
    BLE_GATT_SERVICE_DECLARE(VB_PEER_SVC, SERIAL_UUID_16_PRI_SERVICE, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_DECLARE(VB_PEER_RX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(VB_PEER_RX_VALUE, VB_PEER_UUID128(0x02),
                                BLE_GATT_PERM_WRITE_REQ_ENABLE | BLE_GATT_PERM_WRITE_COMMAND_ENABLE,
                                BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE,
                                VB_PEER_EVENT_DATA_MAX),
    BLE_GATT_CHAR_DECLARE(VB_PEER_TX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(VB_PEER_TX_VALUE, VB_PEER_UUID128(0x03),
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_NOTIFY_ENABLE,
                                BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE,
                                VB_PEER_EVENT_DATA_MAX),
    BLE_GATT_DESCRIPTOR_DECLARE(VB_PEER_TX_CCCD, SERIAL_UUID_16_CLIENT_CHAR_CFG,
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE,
                                BLE_GATT_VALUE_PERM_RI_ENABLE, 2),
};

__attribute__((weak)) void vb_peer_advertising_changed(uint8_t flags)
{
    (void)flags;
}

static uint32_t vb_peer_ticks(uint32_t milliseconds)
{
    return (milliseconds * RT_TICK_PER_SECOND + 999u) / 1000u;
}

static int vb_peer_tick_due(uint32_t now, uint32_t deadline)
{
    return deadline != 0 && (int32_t)(now - deadline) >= 0;
}

static uint16_t vb_peer_get_u16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t vb_peer_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void vb_peer_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)(value >> 8);
}

static void vb_peer_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)((value >> 8) & 0xff);
    dst[2] = (uint8_t)((value >> 16) & 0xff);
    dst[3] = (uint8_t)((value >> 24) & 0xff);
}

static uint16_t vb_peer_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xffff;
    uint16_t index;
    for (index = 0; index < length; index++)
    {
        int bit;
        crc ^= (uint16_t)data[index] << 8;
        for (bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint32_t vb_peer_crc32(const void *data, rt_size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    rt_size_t index;
    for (index = 0; index < length; index++)
    {
        int bit;
        crc ^= bytes[index];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

static int vb_peer_addr_compare(const bd_addr_t *left, const bd_addr_t *right)
{
    return memcmp(left->addr, right->addr, BD_ADDR_LEN);
}

static int vb_peer_addr_equal(const bd_addr_t *left, const bd_addr_t *right)
{
    return vb_peer_addr_compare(left, right) == 0;
}

static void vb_peer_id(const bd_addr_t *addr, char *dst, rt_size_t cap)
{
    if (!dst || cap == 0) return;
    rt_snprintf(dst, cap, "%02X%02X%02X%02X%02X%02X",
                addr->addr[5], addr->addr[4], addr->addr[3],
                addr->addr[2], addr->addr[1], addr->addr[0]);
    dst[cap - 1] = '\0';
}

static const char *vb_peer_state_name(vb_peer_state_t state)
{
    switch (state)
    {
    case VB_PEER_STATE_DISABLED: return "disabled";
    case VB_PEER_STATE_ADVERTISING_SCANNING: return "advertising_scanning";
    case VB_PEER_STATE_CONNECTING: return "connecting";
    case VB_PEER_STATE_PAIRING: return "pairing";
    case VB_PEER_STATE_READY: return "ready";
    case VB_PEER_STATE_RECONNECTING: return "reconnecting";
    default: return "unknown";
    }
}

static void vb_peer_lock(void)
{
    if (g_peer.mutex) rt_mutex_take(g_peer.mutex, RT_WAITING_FOREVER);
}

static void vb_peer_unlock(void)
{
    if (g_peer.mutex) rt_mutex_release(g_peer.mutex);
}

static void vb_peer_touch_locked(void)
{
    g_peer.revision++;
}

static void vb_peer_set_state(vb_peer_state_t state, int error)
{
    uint8_t flags;
    vb_peer_lock();
    g_peer.state = state;
    g_peer.last_error = error;
    vb_peer_touch_locked();
    flags = vb_peer_advertising_flags();
    vb_peer_unlock();
    vb_peer_advertising_changed(flags);
    rt_kprintf("[vb_peer] state=%s error=%d\n", vb_peer_state_name(state), error);
}

static int vb_peer_write_file(const char *path, const void *data, rt_size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    rt_size_t written = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) return -RT_ERROR;
    while (written < length)
    {
        int result = write(fd, bytes + written, length - written);
        if (result <= 0)
        {
            close(fd);
            return -RT_ERROR;
        }
        written += (rt_size_t)result;
    }
    close(fd);
    return RT_EOK;
}

static int vb_peer_read_file(const char *path, void *data, rt_size_t length)
{
    uint8_t *bytes = (uint8_t *)data;
    rt_size_t received = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -RT_ERROR;
    while (received < length)
    {
        int result = read(fd, bytes + received, length - received);
        if (result <= 0)
        {
            close(fd);
            return -RT_ERROR;
        }
        received += (rt_size_t)result;
    }
    close(fd);
    return RT_EOK;
}

static int vb_peer_ensure_storage(void)
{
    if (access(VB_PEER_STORAGE_DIR, 0) == 0) return RT_EOK;
    return mkdir(VB_PEER_STORAGE_DIR, 0) == 0 ? RT_EOK : -RT_ERROR;
}

static int vb_peer_pair_save(void)
{
    vb_peer_pair_file_t file;
    if (vb_peer_ensure_storage() != RT_EOK) return -RT_ERROR;
    rt_memset(&file, 0, sizeof(file));
    file.magic = VB_PEER_PAIR_MAGIC;
    file.version = VB_PEER_STORAGE_VERSION;
    vb_peer_lock();
    file.addr_type = g_peer.peer_addr_type;
    rt_memcpy(file.address, g_peer.peer_addr.addr, BD_ADDR_LEN);
    vb_peer_unlock();
    file.crc32 = vb_peer_crc32(&file, offsetof(vb_peer_pair_file_t, crc32));
    if (vb_peer_write_file(VB_PEER_PAIR_TMP, &file, sizeof(file)) != RT_EOK) return -RT_ERROR;
    unlink(VB_PEER_PAIR_FILE);
    if (rename(VB_PEER_PAIR_TMP, VB_PEER_PAIR_FILE) != 0) return -RT_ERROR;
    return RT_EOK;
}

static int vb_peer_pair_load(void)
{
    vb_peer_pair_file_t file;
    if (vb_peer_read_file(VB_PEER_PAIR_FILE, &file, sizeof(file)) != RT_EOK) return -RT_ERROR;
    if (file.magic != VB_PEER_PAIR_MAGIC || file.version != VB_PEER_STORAGE_VERSION ||
        file.crc32 != vb_peer_crc32(&file, offsetof(vb_peer_pair_file_t, crc32)))
        return -RT_EINVAL;
    vb_peer_lock();
    g_peer.peer_addr_type = file.addr_type;
    rt_memcpy(g_peer.peer_addr.addr, file.address, BD_ADDR_LEN);
    g_peer.paired = 1;
    g_peer.pair_loaded = 1;
    vb_peer_touch_locked();
    vb_peer_unlock();
    return RT_EOK;
}

static int vb_peer_history_save(void)
{
    vb_peer_history_file_t *file;
    int index;
    int result;
    if (vb_peer_ensure_storage() != RT_EOK) return -RT_ERROR;
    file = (vb_peer_history_file_t *)rt_malloc(sizeof(*file));
    if (!file) return -RT_ENOMEM;
    rt_memset(file, 0, sizeof(*file));
    file->magic = VB_PEER_HISTORY_MAGIC;
    file->version = VB_PEER_STORAGE_VERSION;
    vb_peer_lock();
    file->count = (uint16_t)g_peer.history_count;
    file->write_index = (uint16_t)g_peer.history_write_index;
    file->tx_sequence = g_peer.tx_sequence;
    for (index = 0; index < VB_PEER_HISTORY_CAPACITY; index++)
    {
        file->messages[index].id = g_peer.history[index].id;
        file->messages[index].length = g_peer.history[index].length;
        file->messages[index].outgoing = g_peer.history[index].outgoing;
        file->messages[index].status = g_peer.history[index].status;
        rt_memcpy(file->messages[index].text, g_peer.history[index].text,
                  sizeof(file->messages[index].text));
    }
    vb_peer_unlock();
    file->crc32 = vb_peer_crc32(file, offsetof(vb_peer_history_file_t, crc32));
    result = vb_peer_write_file(VB_PEER_HISTORY_TMP, file, sizeof(*file));
    if (result == RT_EOK)
    {
        unlink(VB_PEER_HISTORY_FILE);
        if (rename(VB_PEER_HISTORY_TMP, VB_PEER_HISTORY_FILE) != 0) result = -RT_ERROR;
    }
    rt_free(file);
    return result;
}

static int vb_peer_history_load(void)
{
    vb_peer_history_file_t *file;
    int index;
    int result;
    file = (vb_peer_history_file_t *)rt_malloc(sizeof(*file));
    if (!file) return -RT_ENOMEM;
    result = vb_peer_read_file(VB_PEER_HISTORY_FILE, file, sizeof(*file));
    if (result != RT_EOK || file->magic != VB_PEER_HISTORY_MAGIC ||
        file->version != VB_PEER_STORAGE_VERSION ||
        file->count > VB_PEER_HISTORY_CAPACITY || file->write_index >= VB_PEER_HISTORY_CAPACITY ||
        file->crc32 != vb_peer_crc32(file, offsetof(vb_peer_history_file_t, crc32)))
    {
        rt_free(file);
        return -RT_EINVAL;
    }
    vb_peer_lock();
    g_peer.history_count = file->count;
    g_peer.history_write_index = file->write_index;
    g_peer.tx_sequence = file->tx_sequence;
    for (index = 0; index < VB_PEER_HISTORY_CAPACITY; index++)
    {
        g_peer.history[index].id = file->messages[index].id;
        g_peer.history[index].length = file->messages[index].length <= VB_PEER_MAX_TEXT ?
                                       file->messages[index].length : 0;
        g_peer.history[index].outgoing = file->messages[index].outgoing ? 1 : 0;
        g_peer.history[index].status = file->messages[index].status;
        rt_memcpy(g_peer.history[index].text, file->messages[index].text,
                  sizeof(g_peer.history[index].text));
        g_peer.history[index].text[VB_PEER_MAX_TEXT] = '\0';
    }
    g_peer.history_loaded = 1;
    vb_peer_touch_locked();
    vb_peer_unlock();
    rt_free(file);
    return RT_EOK;
}

static int vb_peer_history_find_locked(uint32_t id, int outgoing)
{
    int index;
    for (index = 0; index < VB_PEER_HISTORY_CAPACITY; index++)
    {
        if (g_peer.history[index].id == id &&
            g_peer.history[index].outgoing == (outgoing ? 1 : 0)) return index;
    }
    return -1;
}

static int vb_peer_history_append(uint32_t id, int outgoing, uint8_t status,
                                  const uint8_t *text, uint16_t length)
{
    int slot;
    if (!text || length > VB_PEER_MAX_TEXT) return -RT_EINVAL;
    vb_peer_lock();
    if (vb_peer_history_find_locked(id, outgoing) >= 0)
    {
        vb_peer_unlock();
        return 0;
    }
    slot = g_peer.history_write_index;
    rt_memset(&g_peer.history[slot], 0, sizeof(g_peer.history[slot]));
    g_peer.history[slot].id = id;
    g_peer.history[slot].length = length;
    g_peer.history[slot].outgoing = outgoing ? 1 : 0;
    g_peer.history[slot].status = status;
    rt_memcpy(g_peer.history[slot].text, text, length);
    g_peer.history[slot].text[length] = '\0';
    g_peer.history_write_index = (slot + 1) % VB_PEER_HISTORY_CAPACITY;
    if (g_peer.history_count < VB_PEER_HISTORY_CAPACITY) g_peer.history_count++;
    if (!outgoing)
    {
        g_peer.unread++;
        g_peer.rx_sequence = id;
    }
    vb_peer_touch_locked();
    vb_peer_unlock();
    return 1;
}

static vb_peer_event_t *vb_peer_event_create(uint8_t type)
{
    vb_peer_event_t *event = (vb_peer_event_t *)rt_malloc(sizeof(*event));
    if (!event) return RT_NULL;
    rt_memset(event, 0, sizeof(*event));
    event->type = type;
    return event;
}

static int vb_peer_post(vb_peer_event_t *event)
{
    if (!event) return -RT_ENOMEM;
    if (!g_peer.mailbox || rt_mb_send(g_peer.mailbox, (rt_uint32_t)(uintptr_t)event) != RT_EOK)
    {
        rt_free(event);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static void vb_peer_service_init(void)
{
    if (g_peer.service_handle) return;
    BLE_GATT_SERVICE_INIT_128(service, vb_peer_att_db, VB_PEER_ATT_NB,
                              BLE_GATT_SERVICE_PERM_NOAUTH | BLE_GATT_SERVICE_PERM_UUID_128 |
                              BLE_GATT_SERVICE_PERM_MULTI_LINK,
                              g_peer_service_uuid);
    g_peer.service_handle = sibles_register_svc_128(&service);
    if (!g_peer.service_handle)
    {
        vb_peer_set_state(g_peer.state, -RT_ERROR);
        rt_kprintf("[vb_peer] service register failed\n");
    }
}

static uint8_t *vb_peer_gatts_get(uint8_t conn_idx, uint8_t index, uint16_t *length)
{
    static uint8_t version = VB_PEER_PROTOCOL_VERSION;
    (void)conn_idx;
    if (!length) return RT_NULL;
    switch (index)
    {
    case VB_PEER_TX_VALUE:
        *length = 1;
        return &version;
    case VB_PEER_TX_CCCD:
        *length = sizeof(g_peer.notify_cccd);
        return (uint8_t *)&g_peer.notify_cccd;
    default:
        *length = 0;
        return RT_NULL;
    }
}

static uint8_t vb_peer_gatts_set(uint8_t conn_idx, sibles_set_cbk_t *parameters)
{
    vb_peer_event_t *event;
    if (!parameters) return 0;
    if (parameters->idx == VB_PEER_RX_VALUE)
    {
        if (!parameters->value || parameters->len > VB_PEER_EVENT_DATA_MAX) return 0;
        event = vb_peer_event_create(VB_PEER_EVT_RX);
        if (!event) return 0;
        event->conn_idx = conn_idx;
        event->length = parameters->len;
        rt_memcpy(event->data, parameters->value, parameters->len);
        vb_peer_post(event);
    }
    else if (parameters->idx == VB_PEER_TX_CCCD && parameters->value && parameters->len >= 2)
    {
        event = vb_peer_event_create(VB_PEER_EVT_CCCD);
        if (!event) return 0;
        event->conn_idx = conn_idx;
        event->flags = parameters->value[0] ? 1 : 0;
        vb_peer_post(event);
    }
    return 0;
}

static int vb_peer_gattc_event(uint16_t event_id, uint8_t *data, uint16_t length)
{
    vb_peer_event_t *event;
    (void)length;
    if (event_id == SIBLES_REGISTER_REMOTE_SVC_RSP)
    {
        sibles_register_remote_svc_rsp_t *response = (sibles_register_remote_svc_rsp_t *)data;
        if (!response || response->status != HL_ERR_NO_ERROR) return 0;
        event = vb_peer_event_create(VB_PEER_EVT_CLIENT_READY);
        if (!event) return 0;
        event->conn_idx = response->conn_idx;
        vb_peer_post(event);
    }
    else if (event_id == SIBLES_REMOTE_EVENT_IND)
    {
        sibles_remote_event_ind_t *indication = (sibles_remote_event_ind_t *)data;
        if (!indication || indication->length > VB_PEER_EVENT_DATA_MAX) return 0;
        event = vb_peer_event_create(VB_PEER_EVT_RX);
        if (!event) return 0;
        event->conn_idx = indication->conn_idx;
        event->length = indication->length;
        rt_memcpy(event->data, indication->value, indication->length);
        vb_peer_post(event);
    }
    return 0;
}

static int vb_peer_adv_flags(const uint8_t *data, uint16_t length, uint8_t *flags)
{
    uint16_t offset = 0;
    while (offset + 2 <= length)
    {
        uint8_t field_length = data[offset];
        uint16_t end;
        if (field_length == 0) break;
        end = offset + 1u + field_length;
        if (end > length) break;
        if (data[offset + 1] == BLE_GAP_AD_TYPE_MANU_SPECIFIC_DATA && field_length >= 9 &&
            data[offset + 4] == 'V' && data[offset + 5] == 'B' &&
            data[offset + 6] == 'R' && data[offset + 7] == 'T' &&
            (data[offset + 9] >> 4) == VB_PEER_PROTOCOL_VERSION)
        {
            if (flags) *flags = data[offset + 9] & 0x0f;
            return 1;
        }
        offset = end;
    }
    return 0;
}

static void vb_peer_search_result(sibles_svc_search_rsp_t *response)
{
    sibles_svc_search_char_t *characteristic;
    uint16_t rx_handle = 0;
    uint16_t tx_handle = 0;
    uint16_t cccd_handle = 0;
    uint16_t offset;
    uint8_t index;
    uint16_t remote_handle;
    if (!response || response->result != HL_ERR_NO_ERROR || !response->svc ||
        response->conn_idx != g_peer.conn_idx) return;
    characteristic = (sibles_svc_search_char_t *)response->svc->att_db;
    for (index = 0; index < response->svc->char_count; index++)
    {
        if (characteristic->uuid_len == ATT_UUID_128_LEN &&
            rt_memcmp(characteristic->uuid, g_peer_rx_uuid, ATT_UUID_128_LEN) == 0)
            rx_handle = characteristic->pointer_hdl;
        else if (characteristic->uuid_len == ATT_UUID_128_LEN &&
                 rt_memcmp(characteristic->uuid, g_peer_tx_uuid, ATT_UUID_128_LEN) == 0)
        {
            tx_handle = characteristic->pointer_hdl;
            cccd_handle = sibles_descriptor_handle_find(characteristic, ATT_DESC_CLIENT_CHAR_CFG);
        }
        offset = sizeof(sibles_svc_search_char_t) +
                 characteristic->desc_count * sizeof(struct sibles_disc_char_desc_ind);
        characteristic = (sibles_svc_search_char_t *)((uint8_t *)characteristic + offset);
    }
    if (!rx_handle || !tx_handle || !cccd_handle) return;
    remote_handle = sibles_register_remote_svc(response->conn_idx,
                                                response->svc->hdl_start,
                                                response->svc->hdl_end,
                                                vb_peer_gattc_event);
    vb_peer_lock();
    g_peer.remote_rx_handle = rx_handle;
    g_peer.remote_tx_handle = tx_handle;
    g_peer.remote_tx_cccd = cccd_handle;
    g_peer.remote_handle = remote_handle;
    vb_peer_unlock();
}

static int vb_peer_ble_event(uint16_t event_id, uint8_t *data, uint16_t length, uint32_t context)
{
    vb_peer_event_t *event;
    (void)length;
    (void)context;
    switch (event_id)
    {
    case BLE_POWER_ON_IND:
        vb_peer_post(vb_peer_event_create(VB_PEER_EVT_POWER));
        break;
    case BLE_GAP_EXT_ADV_REPORT_IND:
    {
        ble_gap_ext_adv_report_ind_t *report = (ble_gap_ext_adv_report_ind_t *)data;
        uint8_t flags;
        if (!report || !vb_peer_adv_flags(report->data, report->length, &flags)) break;
        event = vb_peer_event_create(VB_PEER_EVT_ADV);
        if (!event) break;
        event->addr_type = report->addr.addr_type;
        event->addr = report->addr.addr;
        event->rssi = report->rssi;
        event->flags = flags;
        vb_peer_post(event);
        break;
    }
    case BLE_GAP_CONNECTED_IND:
    {
        ble_gap_connect_ind_t *indication = (ble_gap_connect_ind_t *)data;
        if (!indication) break;
        event = vb_peer_event_create(VB_PEER_EVT_CONNECTED);
        if (!event) break;
        event->conn_idx = indication->conn_idx;
        event->role = indication->role;
        event->addr_type = indication->peer_addr_type;
        event->addr = indication->peer_addr;
        vb_peer_post(event);
        break;
    }
    case BLE_GAP_DISCONNECTED_IND:
    {
        ble_gap_disconnected_ind_t *indication = (ble_gap_disconnected_ind_t *)data;
        if (!indication) break;
        event = vb_peer_event_create(VB_PEER_EVT_DISCONNECTED);
        if (!event) break;
        event->conn_idx = indication->conn_idx;
        event->flags = indication->reason;
        vb_peer_post(event);
        break;
    }
    case BLE_GAP_SCAN_START_CNF:
    {
        ble_gap_start_scan_cnf_t *confirmation = (ble_gap_start_scan_cnf_t *)data;
        vb_peer_lock();
        g_peer.scanning = confirmation && confirmation->status == HL_ERR_NO_ERROR;
        if (confirmation && confirmation->status != HL_ERR_NO_ERROR) g_peer.last_error = confirmation->status;
        vb_peer_unlock();
        break;
    }
    case BLE_GAP_SCAN_STOPPED_IND:
        vb_peer_post(vb_peer_event_create(VB_PEER_EVT_SCAN_STOPPED));
        break;
    case SIBLES_SEARCH_SVC_RSP:
        vb_peer_search_result((sibles_svc_search_rsp_t *)data);
        break;
    case SIBLES_MTU_EXCHANGE_IND:
    {
        sibles_mtu_exchange_ind_t *indication = (sibles_mtu_exchange_ind_t *)data;
        if (!indication) break;
        event = vb_peer_event_create(VB_PEER_EVT_MTU);
        if (!event) break;
        event->conn_idx = indication->conn_idx;
        event->mtu = indication->mtu;
        vb_peer_post(event);
        break;
    }
    default:
        break;
    }
    return 0;
}
BLE_EVENT_REGISTER(vb_peer_ble_event, NULL);

static int vb_peer_start_scan(void)
{
    ble_gap_scan_start_t parameters;
    uint8_t result;
    if (!g_peer.power_on || !g_peer.enabled || g_peer.peer_connected || g_peer.scanning) return RT_EOK;
    rt_memset(&parameters, 0, sizeof(parameters));
    parameters.own_addr_type = GAPM_STATIC_ADDR;
    parameters.type = GAPM_SCAN_TYPE_OBSERVER;
    parameters.dup_filt_pol = 0;
    parameters.scan_param_1m.scan_intv = VB_PEER_SCAN_INTERVAL;
    parameters.scan_param_1m.scan_wd = VB_PEER_SCAN_WINDOW;
    parameters.duration = 1000;
    result = ble_gap_scan_start(&parameters);
    if (result != HL_ERR_NO_ERROR)
    {
        vb_peer_lock();
        g_peer.last_error = result;
        vb_peer_unlock();
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int vb_peer_create_connection(void)
{
    ble_gap_connection_create_param_t parameters;
    uint8_t result;
    rt_memset(&parameters, 0, sizeof(parameters));
    parameters.own_addr_type = GAPM_STATIC_ADDR;
    parameters.conn_to = 500;
    parameters.type = GAPM_INIT_TYPE_DIRECT_CONN_EST;
    parameters.conn_param_1m.scan_intv = 0x30;
    parameters.conn_param_1m.scan_wd = 0x30;
    parameters.conn_param_1m.conn_intv_min = 24;
    parameters.conn_param_1m.conn_intv_max = 40;
    parameters.conn_param_1m.conn_latency = 0;
    parameters.conn_param_1m.supervision_to = 500;
    parameters.conn_param_1m.ce_len_min = 0;
    parameters.conn_param_1m.ce_len_max = 48;
    parameters.peer_addr.addr_type = g_peer.candidate_addr_type;
    parameters.peer_addr.addr = g_peer.candidate_addr;
    result = ble_gap_create_connection(&parameters);
    vb_peer_lock();
    g_peer.connecting = result == HL_ERR_NO_ERROR;
    g_peer.pending_connect = 0;
    g_peer.connect_deadline = rt_tick_get() + vb_peer_ticks(6000);
    vb_peer_unlock();
    vb_peer_set_state(VB_PEER_STATE_CONNECTING, result == HL_ERR_NO_ERROR ? 0 : result);
    return result == HL_ERR_NO_ERROR ? RT_EOK : -RT_ERROR;
}

static int vb_peer_wire_send(const uint8_t *data, uint16_t length)
{
    if (!data || !length || !g_peer.peer_connected || g_peer.conn_idx == VB_PEER_INVALID_CONN)
        return -RT_ERROR;
    if (g_peer.role == VB_PEER_ROLE_CENTRAL)
    {
        sibles_write_remote_value_t value;
        int8_t result;
        if (!g_peer.client_ready || !g_peer.remote_rx_handle) return -RT_EBUSY;
        value.write_type = SIBLES_WRITE_WITHOUT_RSP;
        value.handle = g_peer.remote_rx_handle;
        value.len = length;
        value.value = (uint8_t *)data;
        result = sibles_write_remote_value(g_peer.remote_handle, g_peer.conn_idx, &value);
        return result == SIBLES_WRITE_NO_ERR ? RT_EOK : -RT_ERROR;
    }
    else
    {
        sibles_value_t value;
        int result;
        if (!g_peer.notify_cccd || !g_peer.service_handle) return -RT_EBUSY;
        value.hdl = g_peer.service_handle;
        value.idx = VB_PEER_TX_VALUE;
        value.len = length;
        value.value = (uint8_t *)data;
        result = sibles_write_value(g_peer.conn_idx, &value);
        return result == length ? RT_EOK : -RT_ERROR;
    }
}

static int vb_peer_send_packet(uint8_t type, uint32_t id, const uint8_t *payload, uint16_t length)
{
    uint8_t frame[VB_PEER_EVENT_DATA_MAX];
    uint16_t crc;
    uint16_t offset = 0;
    uint16_t mtu_payload;
    uint16_t fragment_capacity;
    if (length > VB_PEER_MAX_TEXT || (length && !payload)) return -RT_EINVAL;
    crc = vb_peer_crc16(payload, length);
    mtu_payload = g_peer.mtu > 3 ? (uint16_t)(g_peer.mtu - 3) : 20;
    if (mtu_payload > sizeof(frame)) mtu_payload = sizeof(frame);
    if (mtu_payload <= VB_PEER_FRAME_HEADER) mtu_payload = VB_PEER_FRAME_HEADER + 1;
    fragment_capacity = mtu_payload - VB_PEER_FRAME_HEADER;
    do
    {
        uint16_t chunk = length - offset;
        int result;
        int retry;
        if (chunk > fragment_capacity) chunk = fragment_capacity;
        frame[0] = VB_PEER_PROTOCOL_VERSION;
        frame[1] = type;
        vb_peer_put_u32(frame + 2, id);
        vb_peer_put_u16(frame + 6, length);
        vb_peer_put_u16(frame + 8, offset);
        vb_peer_put_u16(frame + 10, crc);
        if (chunk) rt_memcpy(frame + VB_PEER_FRAME_HEADER, payload + offset, chunk);
        result = -RT_ERROR;
        for (retry = 0; retry < 20; retry++)
        {
            result = vb_peer_wire_send(frame, VB_PEER_FRAME_HEADER + chunk);
            if (result == RT_EOK) break;
            rt_thread_mdelay(10);
        }
        if (result != RT_EOK) return result;
        offset += chunk;
        if (length > fragment_capacity) rt_thread_mdelay(5);
    } while (offset < length);
    return RT_EOK;
}

static int vb_peer_send_pair(uint8_t subtype)
{
    uint8_t payload[13];
    rt_memset(payload, 0, sizeof(payload));
    payload[0] = subtype;
    rt_memcpy(payload + 1, g_peer.local_addr.addr, BD_ADDR_LEN);
    vb_peer_put_u32(payload + 7, g_peer.local_nonce);
    payload[11] = g_peer.paired ? 1 : 0;
    payload[12] = g_peer.local_confirmed ? 1 : 0;
    return vb_peer_send_packet(VB_PEER_PACKET_PAIR, 0, payload, sizeof(payload));
}

static void vb_peer_compute_pairing_code(void)
{
    uint8_t material[20];
    const bd_addr_t *first = &g_peer.local_addr;
    const bd_addr_t *second = &g_peer.candidate_addr;
    uint32_t first_nonce = g_peer.local_nonce;
    uint32_t second_nonce = g_peer.remote_nonce;
    if (vb_peer_addr_compare(first, second) > 0)
    {
        first = &g_peer.candidate_addr;
        second = &g_peer.local_addr;
        first_nonce = g_peer.remote_nonce;
        second_nonce = g_peer.local_nonce;
    }
    rt_memcpy(material, first->addr, BD_ADDR_LEN);
    rt_memcpy(material + 6, second->addr, BD_ADDR_LEN);
    vb_peer_put_u32(material + 12, first_nonce);
    vb_peer_put_u32(material + 16, second_nonce);
    vb_peer_lock();
    g_peer.pairing_code = vb_peer_crc32(material, sizeof(material)) % 1000000u;
    vb_peer_touch_locked();
    vb_peer_unlock();
}

static void vb_peer_ready(void)
{
    vb_peer_lock();
    g_peer.pairing = 0;
    g_peer.pairing_deadline = 0;
    g_peer.connecting = 0;
    g_peer.retries = 0;
    g_peer.reconnect_attempt = 0;
    g_peer.ack_deadline = 0;
    vb_peer_unlock();
    vb_peer_set_state(VB_PEER_STATE_READY, 0);
    vb_peer_post(vb_peer_event_create(VB_PEER_EVT_SEND_PENDING));
}

static void vb_peer_commit_pair(void)
{
    vb_peer_lock();
    g_peer.peer_addr = g_peer.candidate_addr;
    g_peer.peer_addr_type = g_peer.candidate_addr_type;
    g_peer.paired = 1;
    vb_peer_touch_locked();
    vb_peer_unlock();
    if (vb_peer_pair_save() != RT_EOK)
    {
        vb_peer_set_state(VB_PEER_STATE_PAIRING, -RT_ERROR);
        return;
    }
    vb_peer_ready();
}

static void vb_peer_handle_pair(const uint8_t *payload, uint16_t length)
{
    uint8_t subtype;
    bd_addr_t remote_addr;
    uint32_t remote_nonce;
    int first_hello;
    if (!payload || length != 13) return;
    subtype = payload[0];
    rt_memcpy(remote_addr.addr, payload + 1, BD_ADDR_LEN);
    remote_nonce = vb_peer_get_u32(payload + 7);
    if (!vb_peer_addr_equal(&remote_addr, &g_peer.candidate_addr)) return;
    if (subtype == VB_PEER_PAIR_CANCEL)
    {
        vb_peer_lock();
        g_peer.pairing = 0;
        g_peer.local_confirmed = 0;
        g_peer.remote_confirmed = 0;
        g_peer.pairing_code = 0;
        g_peer.pairing_deadline = 0;
        vb_peer_touch_locked();
        vb_peer_unlock();
        vb_peer_disconnect();
        if (g_peer.enabled) vb_peer_set_state(VB_PEER_STATE_ADVERTISING_SCANNING, 0);
        return;
    }
    vb_peer_lock();
    first_hello = g_peer.remote_nonce == 0;
    g_peer.remote_nonce = remote_nonce;
    if (subtype == VB_PEER_PAIR_CONFIRM || payload[12]) g_peer.remote_confirmed = 1;
    vb_peer_touch_locked();
    vb_peer_unlock();
    if (g_peer.paired)
    {
        if (!vb_peer_addr_equal(&remote_addr, &g_peer.peer_addr)) return;
        vb_peer_ready();
        return;
    }
    if (!g_peer.pairing) return;
    vb_peer_compute_pairing_code();
    vb_peer_set_state(VB_PEER_STATE_PAIRING, 0);
    if (subtype == VB_PEER_PAIR_HELLO && first_hello) vb_peer_send_pair(VB_PEER_PAIR_HELLO);
    if (g_peer.local_confirmed) vb_peer_send_pair(VB_PEER_PAIR_CONFIRM);
    if (g_peer.local_confirmed && g_peer.remote_confirmed) vb_peer_commit_pair();
}

static void vb_peer_ack(uint32_t id)
{
    uint8_t status = 0;
    vb_peer_send_packet(VB_PEER_PACKET_ACK, id, &status, 1);
}

static void vb_peer_handle_ack(uint32_t id)
{
    int index;
    vb_peer_lock();
    index = vb_peer_history_find_locked(id, 1);
    if (index >= 0)
    {
        g_peer.history[index].status = VB_PEER_MESSAGE_DELIVERED;
        if (g_peer.awaiting_id == id)
        {
            g_peer.ack_deadline = 0;
            g_peer.awaiting_id = 0;
            g_peer.retries = 0;
        }
        vb_peer_touch_locked();
    }
    vb_peer_unlock();
    if (index >= 0)
    {
        vb_peer_history_save();
        vb_peer_post(vb_peer_event_create(VB_PEER_EVT_SEND_PENDING));
    }
}

static void vb_peer_handle_text(uint32_t id, const uint8_t *payload, uint16_t length)
{
    int added;
    if (g_peer.state != VB_PEER_STATE_READY || length == 0) return;
    added = vb_peer_history_append(id, 0, VB_PEER_MESSAGE_RECEIVED, payload, length);
    if (added > 0) vb_peer_history_save();
    vb_peer_ack(id);
}

static void vb_peer_handle_complete_packet(void)
{
    uint8_t type = g_peer.rx.type;
    uint32_t id = g_peer.rx.id;
    uint16_t length = g_peer.rx.total;
    uint8_t payload[VB_PEER_MAX_TEXT + 1];
    rt_memcpy(payload, g_peer.rx.data, length);
    payload[length] = '\0';
    rt_memset(&g_peer.rx, 0, sizeof(g_peer.rx));
    switch (type)
    {
    case VB_PEER_PACKET_PAIR: vb_peer_handle_pair(payload, length); break;
    case VB_PEER_PACKET_TEXT: vb_peer_handle_text(id, payload, length); break;
    case VB_PEER_PACKET_ACK: vb_peer_handle_ack(id); break;
    case VB_PEER_PACKET_PING: vb_peer_ack(id); break;
    default: break;
    }
}

static void vb_peer_receive_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t version;
    uint8_t type;
    uint32_t id;
    uint16_t total;
    uint16_t offset;
    uint16_t crc;
    uint16_t chunk;
    if (!frame || length < VB_PEER_FRAME_HEADER) return;
    version = frame[0];
    type = frame[1];
    id = vb_peer_get_u32(frame + 2);
    total = vb_peer_get_u16(frame + 6);
    offset = vb_peer_get_u16(frame + 8);
    crc = vb_peer_get_u16(frame + 10);
    chunk = length - VB_PEER_FRAME_HEADER;
    if (version != VB_PEER_PROTOCOL_VERSION || type < VB_PEER_PACKET_PAIR ||
        type > VB_PEER_PACKET_PING || total > VB_PEER_MAX_TEXT || offset + chunk > total)
        return;
    if (offset == 0)
    {
        rt_memset(&g_peer.rx, 0, sizeof(g_peer.rx));
        g_peer.rx.active = 1;
        g_peer.rx.type = type;
        g_peer.rx.id = id;
        g_peer.rx.total = total;
        g_peer.rx.crc16 = crc;
    }
    if (!g_peer.rx.active || g_peer.rx.type != type || g_peer.rx.id != id ||
        g_peer.rx.total != total || g_peer.rx.crc16 != crc) return;
    if (offset < g_peer.rx.received) return;
    if (offset != g_peer.rx.received)
    {
        rt_memset(&g_peer.rx, 0, sizeof(g_peer.rx));
        return;
    }
    if (chunk) rt_memcpy(g_peer.rx.data + offset, frame + VB_PEER_FRAME_HEADER, chunk);
    g_peer.rx.received += chunk;
    if (g_peer.rx.received == total)
    {
        if (vb_peer_crc16(g_peer.rx.data, total) != g_peer.rx.crc16)
        {
            rt_memset(&g_peer.rx, 0, sizeof(g_peer.rx));
            return;
        }
        vb_peer_handle_complete_packet();
    }
}

static int vb_peer_send_pending(void)
{
    vb_peer_message_t message;
    int logical;
    int count;
    if (g_peer.state != VB_PEER_STATE_READY || g_peer.ack_deadline) return RT_EOK;
    count = vb_peer_get_message_count();
    for (logical = 0; logical < count; logical++)
    {
        if (vb_peer_get_message(logical, &message) != RT_EOK) continue;
        if (!message.outgoing || message.status != VB_PEER_MESSAGE_PENDING) continue;
        if (vb_peer_send_packet(VB_PEER_PACKET_TEXT, message.id,
                                (const uint8_t *)message.text, message.length) != RT_EOK)
        {
            return -RT_ERROR;
        }
        vb_peer_lock();
        g_peer.ack_deadline = rt_tick_get() + vb_peer_ticks(VB_PEER_ACK_TIMEOUT_MS);
        g_peer.awaiting_id = message.id;
        vb_peer_unlock();
        return RT_EOK;
    }
    return RT_EOK;
}

static void vb_peer_disconnect(void)
{
    ble_gap_disconnect_t parameters;
    if (!g_peer.peer_connected || g_peer.conn_idx == VB_PEER_INVALID_CONN) return;
    parameters.conn_idx = g_peer.conn_idx;
    parameters.reason = CO_ERROR_REMOTE_USER_TERM_CON;
    ble_gap_disconnect(&parameters);
}

static void vb_peer_schedule_reconnect(uint32_t delay_ms)
{
    static const uint16_t delays[] = {1000, 2000, 4000, 8000, 16000, 30000};
    if (delay_ms == 0)
    {
        uint8_t index = g_peer.reconnect_attempt;
        if (index >= sizeof(delays) / sizeof(delays[0])) index = sizeof(delays) / sizeof(delays[0]) - 1;
        delay_ms = delays[index];
        if (g_peer.reconnect_attempt < sizeof(delays) / sizeof(delays[0]) - 1)
            g_peer.reconnect_attempt++;
    }
    vb_peer_lock();
    g_peer.reconnect_deadline = rt_tick_get() + vb_peer_ticks(delay_ms);
    vb_peer_unlock();
    vb_peer_set_state(VB_PEER_STATE_RECONNECTING, g_peer.last_error);
}

static void vb_peer_accept_connection(vb_peer_event_t *event)
{
    int eligible = 0;
    if (!g_peer.enabled || g_peer.peer_connected) return;
    if (g_peer.paired && vb_peer_addr_equal(&event->addr, &g_peer.peer_addr)) eligible = 1;
    else if (g_peer.pairing && event->role == VB_PEER_ROLE_PERIPHERAL) eligible = 1;
    else if (g_peer.pairing && vb_peer_addr_equal(&event->addr, &g_peer.candidate_addr)) eligible = 1;
    else if (event->role == VB_PEER_ROLE_CENTRAL && g_peer.connecting &&
             vb_peer_addr_equal(&event->addr, &g_peer.candidate_addr)) eligible = 1;
    if (!eligible) return;
    vb_peer_lock();
    g_peer.conn_idx = event->conn_idx;
    g_peer.role = event->role;
    g_peer.peer_connected = 1;
    g_peer.connecting = 0;
    g_peer.scanning = 0;
    g_peer.mtu = 23;
    g_peer.candidate_addr = event->addr;
    g_peer.candidate_addr_type = event->addr_type;
    g_peer.hello_deadline = 0;
    vb_peer_touch_locked();
    vb_peer_unlock();
    sibles_exchange_mtu(event->conn_idx);
    if (event->role == VB_PEER_ROLE_CENTRAL)
        sibles_search_service(event->conn_idx, ATT_UUID_128_LEN, g_peer_service_uuid);
    vb_peer_set_state(g_peer.paired ? VB_PEER_STATE_CONNECTING : VB_PEER_STATE_PAIRING, 0);
}

static void vb_peer_handle_adv(vb_peer_event_t *event)
{
    int eligible = 0;
    if (!g_peer.enabled || g_peer.peer_connected || !(event->flags & VB_PEER_ADV_ACTIVE)) return;
    if (g_peer.paired)
    {
        eligible = (event->flags & VB_PEER_ADV_PAIRED) && vb_peer_addr_equal(&event->addr, &g_peer.peer_addr);
    }
    else if (g_peer.pairing)
    {
        eligible = (event->flags & VB_PEER_ADV_PAIRABLE) != 0;
    }
    if (!eligible || vb_peer_addr_equal(&event->addr, &g_peer.local_addr)) return;
    vb_peer_lock();
    if (!g_peer.candidate_tick || event->rssi > g_peer.candidate_rssi)
    {
        g_peer.candidate_addr = event->addr;
        g_peer.candidate_addr_type = event->addr_type;
        g_peer.candidate_rssi = event->rssi;
        g_peer.candidate_tick = rt_tick_get();
        vb_peer_touch_locked();
    }
    vb_peer_unlock();
}

static void vb_peer_client_ready(void)
{
    sibles_write_remote_value_t value;
    uint8_t cccd[2] = {1, 0};
    if (g_peer.conn_idx == VB_PEER_INVALID_CONN || !g_peer.remote_tx_cccd) return;
    value.write_type = SIBLES_WRITE;
    value.handle = g_peer.remote_tx_cccd;
    value.len = sizeof(cccd);
    value.value = cccd;
    {
        int result = sibles_write_remote_value(g_peer.remote_handle, g_peer.conn_idx, &value);
        if (result != SIBLES_WRITE_NO_ERR)
        {
            vb_peer_set_state(g_peer.state, -RT_ERROR);
            return;
        }
    }
    vb_peer_lock();
    g_peer.client_ready = 1;
    g_peer.hello_deadline = rt_tick_get() + vb_peer_ticks(80);
    vb_peer_touch_locked();
    vb_peer_unlock();
}

static void vb_peer_handle_disconnect(vb_peer_event_t *event)
{
    if (!g_peer.peer_connected || event->conn_idx != g_peer.conn_idx) return;
    vb_peer_lock();
    g_peer.peer_connected = 0;
    g_peer.client_ready = 0;
    g_peer.notify_cccd = 0;
    g_peer.conn_idx = VB_PEER_INVALID_CONN;
    g_peer.remote_handle = 0;
    g_peer.remote_rx_handle = 0;
    g_peer.remote_tx_handle = 0;
    g_peer.remote_tx_cccd = 0;
    g_peer.ack_deadline = 0;
    g_peer.awaiting_id = 0;
    g_peer.retries = 0;
    g_peer.last_error = event->flags;
    rt_memset(&g_peer.rx, 0, sizeof(g_peer.rx));
    vb_peer_touch_locked();
    vb_peer_unlock();
    if (g_peer.enabled)
        vb_peer_schedule_reconnect(0);
    else
        vb_peer_set_state(VB_PEER_STATE_DISABLED, 0);
}

static void vb_peer_process_event(vb_peer_event_t *event)
{
    if (!event) return;
    switch (event->type)
    {
    case VB_PEER_EVT_POWER:
        g_peer.power_on = 1;
        ble_get_public_address(&g_peer.local_addr);
        vb_peer_service_init();
        if (g_peer.service_handle)
            sibles_register_cbk(g_peer.service_handle, vb_peer_gatts_get, vb_peer_gatts_set);
        if (g_peer.enabled) vb_peer_start_scan();
        break;
    case VB_PEER_EVT_ADV: vb_peer_handle_adv(event); break;
    case VB_PEER_EVT_CONNECTED: vb_peer_accept_connection(event); break;
    case VB_PEER_EVT_DISCONNECTED: vb_peer_handle_disconnect(event); break;
    case VB_PEER_EVT_SCAN_STOPPED:
        g_peer.scanning = 0;
        if (g_peer.pending_connect) g_peer.connect_deadline = rt_tick_get() + vb_peer_ticks(80);
        else if (g_peer.enabled && !g_peer.peer_connected) vb_peer_schedule_reconnect(0);
        break;
    case VB_PEER_EVT_CLIENT_READY:
        if (event->conn_idx == g_peer.conn_idx) vb_peer_client_ready();
        break;
    case VB_PEER_EVT_RX:
        if (event->conn_idx == g_peer.conn_idx) vb_peer_receive_frame(event->data, event->length);
        break;
    case VB_PEER_EVT_CCCD:
        if (event->conn_idx == g_peer.conn_idx)
        {
            g_peer.notify_cccd = event->flags ? 1 : 0;
            if (g_peer.notify_cccd) g_peer.hello_deadline = rt_tick_get() + vb_peer_ticks(50);
        }
        break;
    case VB_PEER_EVT_MTU:
        if (event->conn_idx == g_peer.conn_idx) g_peer.mtu = event->mtu;
        break;
    case VB_PEER_EVT_SEND_PENDING: vb_peer_send_pending(); break;
    default: break;
    }
}

static void vb_peer_process_timeouts(void)
{
    uint32_t now = rt_tick_get();
    if (!g_peer.enabled) return;
    if (g_peer.pairing && vb_peer_tick_due(now, g_peer.pairing_deadline))
    {
        vb_peer_pair_cancel();
        return;
    }
    if (!g_peer.peer_connected && g_peer.candidate_tick &&
        (int32_t)(now - g_peer.candidate_tick) >= (int32_t)vb_peer_ticks(VB_PEER_CANDIDATE_SETTLE_MS))
    {
        g_peer.candidate_tick = 0;
        if (vb_peer_addr_compare(&g_peer.local_addr, &g_peer.candidate_addr) < 0)
        {
            g_peer.pending_connect = 1;
            g_peer.connect_deadline = now + vb_peer_ticks(100);
            if (g_peer.scanning) ble_gap_scan_stop();
        }
    }
    if (!g_peer.peer_connected && g_peer.pending_connect && vb_peer_tick_due(now, g_peer.connect_deadline))
    {
        vb_peer_create_connection();
        return;
    }
    if (!g_peer.peer_connected && !g_peer.connecting && vb_peer_tick_due(now, g_peer.reconnect_deadline))
    {
        g_peer.reconnect_deadline = 0;
        g_peer.candidate_rssi = -127;
        vb_peer_set_state(VB_PEER_STATE_ADVERTISING_SCANNING, 0);
        vb_peer_start_scan();
    }
    if (g_peer.connecting && vb_peer_tick_due(now, g_peer.connect_deadline))
    {
        g_peer.connecting = 0;
        vb_peer_schedule_reconnect(0);
    }
    if (g_peer.peer_connected && vb_peer_tick_due(now, g_peer.hello_deadline))
    {
        g_peer.hello_deadline = 0;
        vb_peer_send_pair(VB_PEER_PAIR_HELLO);
    }
    if (g_peer.state == VB_PEER_STATE_READY && vb_peer_tick_due(now, g_peer.ack_deadline))
    {
        int pending_index = -1;
        vb_peer_lock();
        if (g_peer.retries < VB_PEER_MAX_RETRIES)
        {
            g_peer.retries++;
            g_peer.ack_deadline = 0;
        }
        else
        {
            pending_index = vb_peer_history_find_locked(g_peer.awaiting_id, 1);
            if (pending_index >= 0)
                g_peer.history[pending_index].status = VB_PEER_MESSAGE_FAILED;
            g_peer.ack_deadline = 0;
            g_peer.awaiting_id = 0;
            g_peer.retries = 0;
            vb_peer_touch_locked();
        }
        vb_peer_unlock();
        if (pending_index >= 0) vb_peer_history_save();
        vb_peer_post(vb_peer_event_create(VB_PEER_EVT_SEND_PENDING));
    }
}

static void vb_peer_worker(void *parameter)
{
    (void)parameter;
    while (1)
    {
        rt_uint32_t value = 0;
        if (rt_mb_recv(g_peer.mailbox, &value, vb_peer_ticks(50)) == RT_EOK)
        {
            vb_peer_event_t *event = (vb_peer_event_t *)(uintptr_t)value;
            vb_peer_process_event(event);
            rt_free(event);
        }
        vb_peer_process_timeouts();
    }
}

int vb_peer_init(void)
{
    if (g_peer.initialized) return RT_EOK;
    rt_memset(&g_peer, 0, sizeof(g_peer));
    g_peer.conn_idx = VB_PEER_INVALID_CONN;
    g_peer.candidate_rssi = -127;
    g_peer.mtu = 23;
    g_peer.state = VB_PEER_STATE_DISABLED;
    g_peer.mutex = rt_mutex_create("vbpeer", RT_IPC_FLAG_FIFO);
    g_peer.mailbox = rt_mb_create("vbpeer", 24, RT_IPC_FLAG_FIFO);
    if (!g_peer.mutex || !g_peer.mailbox) return -RT_ENOMEM;
    g_peer.worker = rt_thread_create("vbpeer", vb_peer_worker, RT_NULL, 6144,
                                     RT_THREAD_PRIORITY_MIDDLE + 2, RT_THREAD_TICK_DEFAULT);
    if (!g_peer.worker) return -RT_ENOMEM;
    ble_get_public_address(&g_peer.local_addr);
    vb_peer_pair_load();
    vb_peer_history_load();
    g_peer.initialized = 1;
    rt_thread_startup(g_peer.worker);
    rt_kprintf("[vb_peer] initialized api=%s paired=%d\n", VB_PEER_API_VERSION, g_peer.paired);
    return RT_EOK;
}

int vb_peer_enable(int enabled)
{
    int was_scanning;
    if (!g_peer.initialized && vb_peer_init() != RT_EOK) return -RT_ERROR;
    if (enabled)
    {
        if (!g_peer.pair_loaded) vb_peer_pair_load();
        if (!g_peer.history_loaded) vb_peer_history_load();
        vb_peer_lock();
        g_peer.enabled = 1;
        g_peer.candidate_rssi = -127;
        g_peer.reconnect_attempt = 0;
        g_peer.reconnect_deadline = rt_tick_get() + vb_peer_ticks(100);
        vb_peer_touch_locked();
        vb_peer_unlock();
        vb_peer_set_state(VB_PEER_STATE_ADVERTISING_SCANNING, 0);
        return RT_EOK;
    }
    vb_peer_lock();
    g_peer.enabled = 0;
    was_scanning = g_peer.scanning;
    g_peer.scanning = 0;
    g_peer.pairing = 0;
    g_peer.pairing_deadline = 0;
    g_peer.pending_connect = 0;
    g_peer.reconnect_deadline = 0;
    vb_peer_touch_locked();
    vb_peer_unlock();
    if (was_scanning) ble_gap_scan_stop();
    vb_peer_disconnect();
    vb_peer_set_state(VB_PEER_STATE_DISABLED, 0);
    return RT_EOK;
}

void vb_peer_release_host_connection(uint8_t conn_idx)
{
    if (!g_peer.initialized || !g_peer.peer_connected ||
        g_peer.conn_idx != conn_idx || g_peer.role != VB_PEER_ROLE_PERIPHERAL)
        return;
    vb_peer_lock();
    g_peer.peer_connected = 0;
    g_peer.client_ready = 0;
    g_peer.notify_cccd = 0;
    g_peer.conn_idx = VB_PEER_INVALID_CONN;
    g_peer.remote_handle = 0;
    g_peer.remote_rx_handle = 0;
    g_peer.remote_tx_handle = 0;
    g_peer.remote_tx_cccd = 0;
    g_peer.candidate_tick = 0;
    g_peer.pending_connect = 0;
    g_peer.connecting = 0;
    g_peer.reconnect_deadline = rt_tick_get();
    g_peer.hello_deadline = 0;
    g_peer.ack_deadline = 0;
    g_peer.awaiting_id = 0;
    g_peer.retries = 0;
    vb_peer_touch_locked();
    vb_peer_unlock();
    if (g_peer.enabled)
        vb_peer_set_state(g_peer.pairing ? VB_PEER_STATE_ADVERTISING_SCANNING :
                          VB_PEER_STATE_RECONNECTING, 0);
}

int vb_peer_pair_start(void)
{
    if (!g_peer.initialized && vb_peer_init() != RT_EOK) return -RT_ERROR;
    if (g_peer.paired) return -RT_EBUSY;
    vb_peer_enable(1);
    vb_peer_lock();
    g_peer.pairing = 1;
    g_peer.local_confirmed = 0;
    g_peer.remote_confirmed = 0;
    g_peer.remote_nonce = 0;
    g_peer.pairing_code = 0;
    g_peer.local_nonce = vb_peer_crc32(g_peer.local_addr.addr, BD_ADDR_LEN) ^ rt_tick_get();
    if (!g_peer.local_nonce) g_peer.local_nonce = 1;
    g_peer.pairing_deadline = rt_tick_get() + vb_peer_ticks(VB_PEER_PAIR_WINDOW_MS);
    g_peer.candidate_tick = 0;
    g_peer.candidate_rssi = -127;
    vb_peer_touch_locked();
    vb_peer_unlock();
    vb_peer_set_state(VB_PEER_STATE_ADVERTISING_SCANNING, 0);
    return RT_EOK;
}

int vb_peer_pair_confirm(void)
{
    if (!g_peer.pairing || !g_peer.pairing_code) return -RT_EBUSY;
    vb_peer_lock();
    g_peer.local_confirmed = 1;
    vb_peer_touch_locked();
    vb_peer_unlock();
    /* The management link can temporarily replace the single Peer link. Keep
     * the local decision and send it from vb_peer_handle_pair after reconnect. */
    if (g_peer.peer_connected && vb_peer_send_pair(VB_PEER_PAIR_CONFIRM) != RT_EOK)
        return -RT_ERROR;
    if (g_peer.peer_connected && g_peer.remote_confirmed) vb_peer_commit_pair();
    return RT_EOK;
}

int vb_peer_pair_cancel(void)
{
    if (g_peer.peer_connected) vb_peer_send_pair(VB_PEER_PAIR_CANCEL);
    vb_peer_lock();
    g_peer.pairing = 0;
    g_peer.local_confirmed = 0;
    g_peer.remote_confirmed = 0;
    g_peer.pairing_code = 0;
    g_peer.pairing_deadline = 0;
    vb_peer_touch_locked();
    vb_peer_unlock();
    vb_peer_disconnect();
    if (g_peer.enabled) vb_peer_set_state(VB_PEER_STATE_ADVERTISING_SCANNING, 0);
    return RT_EOK;
}

int vb_peer_forget(void)
{
    vb_peer_pair_cancel();
    unlink(VB_PEER_PAIR_FILE);
    unlink(VB_PEER_PAIR_TMP);
    unlink(VB_PEER_HISTORY_FILE);
    unlink(VB_PEER_HISTORY_TMP);
    vb_peer_lock();
    g_peer.paired = 0;
    g_peer.pair_loaded = 1;
    rt_memset(&g_peer.peer_addr, 0, sizeof(g_peer.peer_addr));
    g_peer.peer_addr_type = 0;
    rt_memset(g_peer.history, 0, sizeof(g_peer.history));
    g_peer.history_count = 0;
    g_peer.history_write_index = 0;
    g_peer.history_loaded = 1;
    g_peer.tx_sequence = 0;
    g_peer.rx_sequence = 0;
    g_peer.unread = 0;
    g_peer.awaiting_id = 0;
    g_peer.ack_deadline = 0;
    g_peer.retries = 0;
    vb_peer_touch_locked();
    vb_peer_unlock();
    vb_peer_set_state(g_peer.enabled ? VB_PEER_STATE_ADVERTISING_SCANNING : VB_PEER_STATE_DISABLED, 0);
    return RT_EOK;
}

int vb_peer_send_text(const char *text)
{
    uint16_t length;
    uint32_t id;
    int result;
    if (!text) return -RT_EINVAL;
    length = (uint16_t)rt_strlen(text);
    if (!length || length > VB_PEER_MAX_TEXT) return -RT_EINVAL;
    vb_peer_lock();
    id = ++g_peer.tx_sequence;
    if (!id) id = ++g_peer.tx_sequence;
    vb_peer_unlock();
    result = vb_peer_history_append(id, 1, VB_PEER_MESSAGE_PENDING, (const uint8_t *)text, length);
    if (result <= 0) return result < 0 ? result : -RT_ERROR;
    vb_peer_history_save();
    vb_peer_post(vb_peer_event_create(VB_PEER_EVT_SEND_PENDING));
    return (int)id;
}

static int vb_peer_hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int vb_peer_send_fill(const char *length_text, const char *byte_text)
{
    char text[VB_PEER_MAX_TEXT + 1];
    int length;
    int high;
    int low;
    int value;
    if (!length_text || !byte_text || rt_strlen(byte_text) != 2) return -RT_EINVAL;
    length = atoi(length_text);
    high = vb_peer_hex_digit(byte_text[0]);
    low = vb_peer_hex_digit(byte_text[1]);
    value = high < 0 || low < 0 ? -1 : (high << 4) | low;
    if (length <= 0 || length > VB_PEER_MAX_TEXT || value < 0x20 || value > 0x7e)
        return -RT_EINVAL;
    rt_memset(text, value, length);
    text[length] = '\0';
    return vb_peer_send_text(text);
}

int vb_peer_send_hex(const char *hex_text)
{
    char text[VB_PEER_MAX_TEXT + 1];
    int length;
    int index;
    if (!hex_text) return -RT_EINVAL;
    length = rt_strlen(hex_text);
    if (!length || (length & 1) || length > VB_PEER_MAX_TEXT * 2) return -RT_EINVAL;
    for (index = 0; index < length; index += 2)
    {
        int high = vb_peer_hex_digit(hex_text[index]);
        int low = vb_peer_hex_digit(hex_text[index + 1]);
        if (high < 0 || low < 0) return -RT_EINVAL;
        text[index / 2] = (char)((high << 4) | low);
        if (!text[index / 2]) return -RT_EINVAL;
    }
    text[length / 2] = '\0';
    return vb_peer_send_text(text);
}

int vb_peer_mark_read(void)
{
    vb_peer_lock();
    if (g_peer.unread)
    {
        g_peer.unread = 0;
        vb_peer_touch_locked();
    }
    vb_peer_unlock();
    return RT_EOK;
}

static int vb_peer_pending_locked(void)
{
    int index;
    int count = 0;
    for (index = 0; index < VB_PEER_HISTORY_CAPACITY; index++)
        if (g_peer.history[index].outgoing &&
            g_peer.history[index].status == VB_PEER_MESSAGE_PENDING) count++;
    return count;
}

int vb_peer_get_status(vb_peer_status_t *status)
{
    if (!status) return -RT_EINVAL;
    rt_memset(status, 0, sizeof(*status));
    vb_peer_lock();
    status->state = g_peer.state;
    status->available = g_peer.initialized ? 1 : 0;
    status->enabled = g_peer.enabled;
    status->paired = g_peer.paired;
    status->pairing = g_peer.pairing;
    status->connected = g_peer.peer_connected && g_peer.state == VB_PEER_STATE_READY;
    status->role = g_peer.peer_connected ? g_peer.role : -1;
    status->pending = vb_peer_pending_locked();
    status->unread = g_peer.unread;
    status->last_error = g_peer.last_error;
    status->pairing_code = g_peer.pairing_code;
    status->tx_sequence = g_peer.tx_sequence;
    status->rx_sequence = g_peer.rx_sequence;
    status->revision = g_peer.revision;
    if (g_peer.paired) vb_peer_id(&g_peer.peer_addr, status->peer_id, sizeof(status->peer_id));
    else if (g_peer.candidate_tick || g_peer.peer_connected)
        vb_peer_id(&g_peer.candidate_addr, status->peer_id, sizeof(status->peer_id));
    else rt_snprintf(status->peer_id, sizeof(status->peer_id), "--");
    vb_peer_unlock();
    return RT_EOK;
}

int vb_peer_get_message_count(void)
{
    int count;
    vb_peer_lock();
    count = g_peer.history_count;
    vb_peer_unlock();
    return count;
}

int vb_peer_get_message(int logical_index, vb_peer_message_t *message)
{
    int physical;
    if (!message || logical_index < 0) return -RT_EINVAL;
    vb_peer_lock();
    if (logical_index >= g_peer.history_count)
    {
        vb_peer_unlock();
        return -RT_EINVAL;
    }
    physical = (g_peer.history_write_index + VB_PEER_HISTORY_CAPACITY -
                g_peer.history_count + logical_index) % VB_PEER_HISTORY_CAPACITY;
    *message = g_peer.history[physical];
    vb_peer_unlock();
    return RT_EOK;
}

int vb_peer_status_json(char *dst, rt_size_t cap)
{
    vb_peer_status_t status;
    int used;
    if (!dst || cap == 0) return -RT_EINVAL;
    vb_peer_get_status(&status);
    used = rt_snprintf(dst, cap,
                       "{\"api\":\"%s\",\"state\":\"%s\",\"available\":%d,\"enabled\":%d,"
                       "\"paired\":%d,\"pairing\":%d,\"connected\":%d,\"peerId\":\"%s\",\"role\":%d,"
                       "\"pending\":%d,\"unread\":%d,\"txSeq\":%lu,\"rxSeq\":%lu,"
                       "\"pairingCode\":%lu,\"lastError\":%d}",
                       VB_PEER_API_VERSION, vb_peer_state_name(status.state), status.available,
                       status.enabled, status.paired, status.pairing, status.connected, status.peer_id, status.role,
                       status.pending, status.unread, (unsigned long)status.tx_sequence,
                       (unsigned long)status.rx_sequence, (unsigned long)status.pairing_code,
                       status.last_error);
    dst[cap - 1] = '\0';
    return used >= 0 && used < (int)cap ? RT_EOK : -RT_ERROR;
}

static void vb_peer_json_escape(const char *src, char *dst, rt_size_t cap)
{
    rt_size_t used = 0;
    if (!dst || cap == 0) return;
    while (src && *src && used + 2 < cap)
    {
        unsigned char value = (unsigned char)*src++;
        if (value == '"' || value == '\\')
        {
            dst[used++] = '\\';
            dst[used++] = (char)value;
        }
        else if (value >= 0x20)
        {
            dst[used++] = (char)value;
        }
    }
    dst[used] = '\0';
}

int vb_peer_messages_page_json(char *dst, rt_size_t cap, int offset, int limit)
{
    int count = vb_peer_get_message_count();
    int index;
    int used;
    int emitted = 0;
    if (!dst || cap == 0) return -RT_EINVAL;
    if (offset < 0) offset = 0;
    if (limit <= 0 || limit > 3) limit = 2;
    used = rt_snprintf(dst, cap, "{\"api\":\"%s\",\"offset\":%d,\"total\":%d,\"items\":[",
                       VB_PEER_API_VERSION, offset, count);
    if (used < 0 || used >= (int)cap) return -RT_ERROR;
    for (index = offset; index < count && emitted < limit; index++)
    {
        vb_peer_message_t message;
        char escaped[VB_PEER_MAX_TEXT * 2 + 1];
        int appended;
        if (vb_peer_get_message(index, &message) != RT_EOK) break;
        vb_peer_json_escape(message.text, escaped, sizeof(escaped));
        appended = rt_snprintf(dst + used, cap - used,
                               "%s{\"id\":%lu,\"out\":%d,\"status\":%d,\"bytes\":%u,\"text\":\"%s\"}",
                               emitted ? "," : "", (unsigned long)message.id, message.outgoing,
                               message.status, message.length, escaped);
        if (appended < 0 || appended >= (int)(cap - used)) return -RT_ERROR;
        used += appended;
        emitted++;
    }
    if (rt_snprintf(dst + used, cap - used, "]}") >= (int)(cap - used)) return -RT_ERROR;
    return RT_EOK;
}

int vb_peer_format_text(const char *selector, char *dst, rt_size_t cap)
{
    vb_peer_status_t status;
    int count;
    vb_peer_message_t latest;
    if (!selector || !dst || cap == 0) return 0;
    vb_peer_get_status(&status);
    if (rt_strcmp(selector, "state") == 0 || rt_strcmp(selector, "status") == 0)
        rt_snprintf(dst, cap, "%s", vb_peer_state_name(status.state));
    else if (rt_strcmp(selector, "peer") == 0 || rt_strcmp(selector, "peerId") == 0)
        rt_snprintf(dst, cap, "%s", status.peer_id);
    else if (rt_strcmp(selector, "unread") == 0) rt_snprintf(dst, cap, "%d", status.unread);
    else if (rt_strcmp(selector, "pending") == 0) rt_snprintf(dst, cap, "%d", status.pending);
    else if (rt_strcmp(selector, "latest") == 0 || rt_strcmp(selector, "messages") == 0)
    {
        count = vb_peer_get_message_count();
        if (count > 0 && vb_peer_get_message(count - 1, &latest) == RT_EOK)
            rt_snprintf(dst, cap, "%s", latest.text);
        else rt_snprintf(dst, cap, "--");
    }
    else return 0;
    dst[cap - 1] = '\0';
    return 1;
}

int vb_peer_command(int argc, char **argv, char *dst, rt_size_t cap)
{
    int result = -RT_EINVAL;
    if (!argv || argc <= 0 || !dst || cap == 0) return -RT_EINVAL;
    if (rt_strcmp(argv[0], "peer_status") == 0)
        return vb_peer_status_json(dst, cap);
    if (rt_strcmp(argv[0], "peer_pair") == 0 && argc >= 2)
    {
        if (rt_strcmp(argv[1], "start") == 0) result = vb_peer_pair_start();
        else if (rt_strcmp(argv[1], "confirm") == 0) result = vb_peer_pair_confirm();
        else if (rt_strcmp(argv[1], "cancel") == 0) result = vb_peer_pair_cancel();
        else if (rt_strcmp(argv[1], "forget") == 0) result = vb_peer_forget();
    }
    else if (rt_strcmp(argv[0], "peer_send") == 0 && argc >= 2)
        result = vb_peer_send_hex(argv[1]);
    else if (rt_strcmp(argv[0], "peer_send_fill") == 0 && argc >= 3)
        result = vb_peer_send_fill(argv[1], argv[2]);
    else if (rt_strcmp(argv[0], "peer_messages_page") == 0)
    {
        int offset = argc >= 2 ? atoi(argv[1]) : 0;
        int limit = argc >= 3 ? atoi(argv[2]) : 2;
        return vb_peer_messages_page_json(dst, cap, offset, limit);
    }
    if (result >= 0)
        rt_snprintf(dst, cap, "ok %s rc=%d", argv[0], result);
    else
        rt_snprintf(dst, cap, "err %s rc=%d", argv[0], result);
    dst[cap - 1] = '\0';
    return result >= 0 ? RT_EOK : result;
}

uint8_t vb_peer_advertising_flags(void)
{
    uint8_t flags = 0;
    if (!g_peer.initialized) return 0;
    if (g_peer.enabled) flags |= VB_PEER_ADV_ACTIVE;
    if (g_peer.pairing) flags |= VB_PEER_ADV_PAIRABLE;
    if (g_peer.paired) flags |= VB_PEER_ADV_PAIRED;
    return flags;
}

static void vb_peer_print_line(const char *text)
{
    char chunk[97];
    rt_size_t length;
    rt_size_t offset = 0;

    if (!text)
    {
        rt_kprintf("\n");
        return;
    }

    length = rt_strlen(text);
    while (offset < length)
    {
        rt_size_t count = length - offset;
        if (count >= sizeof(chunk)) count = sizeof(chunk) - 1;
        rt_memcpy(chunk, text + offset, count);
        chunk[count] = '\0';
        rt_kprintf("%s", chunk);
        offset += count;
    }
    rt_kprintf("\n");
}

static int vb_peer_msh_status(int argc, char **argv)
{
    char json[512];
    (void)argc;
    (void)argv;
    vb_peer_status_json(json, sizeof(json));
    vb_peer_print_line(json);
    return RT_EOK;
}
MSH_CMD_EXPORT_ALIAS(vb_peer_msh_status, peer_status, show VibeBoard peer status);

static int vb_peer_msh_pair(int argc, char **argv)
{
    char status[96];
    int result = vb_peer_command(argc, argv, status, sizeof(status));
    rt_kprintf("%s\n", status);
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_peer_msh_pair, peer_pair, pair confirm cancel or forget a peer);

static int vb_peer_msh_send(int argc, char **argv)
{
    char status[96];
    int result = vb_peer_command(argc, argv, status, sizeof(status));
    rt_kprintf("%s\n", status);
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_peer_msh_send, peer_send, send hex UTF-8 text to paired peer);

static int vb_peer_msh_send_fill(int argc, char **argv)
{
    char status[96];
    int result = vb_peer_command(argc, argv, status, sizeof(status));
    rt_kprintf("%s\n", status);
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_peer_msh_send_fill, peer_send_fill, send repeated-byte peer test payload);

static int vb_peer_msh_messages(int argc, char **argv)
{
    char json[768];
    int result = vb_peer_command(argc, argv, json, sizeof(json));
    vb_peer_print_line(json);
    return result;
}
MSH_CMD_EXPORT_ALIAS(vb_peer_msh_messages, peer_messages_page, show peer message history page);

#else

__attribute__((weak)) void vb_peer_advertising_changed(uint8_t flags)
{
    (void)flags;
}

int vb_peer_init(void) { return -RT_ENOSYS; }
int vb_peer_enable(int enabled) { (void)enabled; return -RT_ENOSYS; }
void vb_peer_release_host_connection(uint8_t conn_idx) { (void)conn_idx; }
int vb_peer_pair_start(void) { return -RT_ENOSYS; }
int vb_peer_pair_confirm(void) { return -RT_ENOSYS; }
int vb_peer_pair_cancel(void) { return -RT_ENOSYS; }
int vb_peer_forget(void) { return -RT_ENOSYS; }
int vb_peer_send_text(const char *text) { (void)text; return -RT_ENOSYS; }
int vb_peer_send_hex(const char *hex_text) { (void)hex_text; return -RT_ENOSYS; }
int vb_peer_mark_read(void) { return -RT_ENOSYS; }
int vb_peer_get_status(vb_peer_status_t *status)
{
    if (!status) return -RT_EINVAL;
    rt_memset(status, 0, sizeof(*status));
    status->role = -1;
    return -RT_ENOSYS;
}
int vb_peer_get_message(int logical_index, vb_peer_message_t *message)
{ (void)logical_index; (void)message; return -RT_ENOSYS; }
int vb_peer_get_message_count(void) { return 0; }
int vb_peer_status_json(char *dst, rt_size_t cap)
{
    if (!dst || !cap) return -RT_EINVAL;
    rt_snprintf(dst, cap, "{\"api\":\"%s\",\"available\":0}", VB_PEER_API_VERSION);
    return -RT_ENOSYS;
}
int vb_peer_messages_page_json(char *dst, rt_size_t cap, int offset, int limit)
{ (void)offset; (void)limit; return vb_peer_status_json(dst, cap); }
int vb_peer_format_text(const char *selector, char *dst, rt_size_t cap)
{ (void)selector; if (dst && cap) rt_snprintf(dst, cap, "unavailable"); return 0; }
int vb_peer_command(int argc, char **argv, char *dst, rt_size_t cap)
{ (void)argc; (void)argv; return vb_peer_status_json(dst, cap); }
uint8_t vb_peer_advertising_flags(void) { return 0; }

#endif
