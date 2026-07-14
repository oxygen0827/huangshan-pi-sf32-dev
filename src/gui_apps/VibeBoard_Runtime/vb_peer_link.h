#ifndef VB_PEER_LINK_H
#define VB_PEER_LINK_H

#include <rtthread.h>
#include <stdint.h>

#define VB_PEER_API_VERSION "vibeboard-huangshan-peer/v1"
#define VB_PEER_MAX_TEXT 192
#define VB_PEER_HISTORY_CAPACITY 20

typedef enum
{
    VB_PEER_STATE_DISABLED = 0,
    VB_PEER_STATE_ADVERTISING_SCANNING,
    VB_PEER_STATE_CONNECTING,
    VB_PEER_STATE_PAIRING,
    VB_PEER_STATE_READY,
    VB_PEER_STATE_RECONNECTING
} vb_peer_state_t;

typedef enum
{
    VB_PEER_MESSAGE_PENDING = 0,
    VB_PEER_MESSAGE_DELIVERED,
    VB_PEER_MESSAGE_FAILED,
    VB_PEER_MESSAGE_RECEIVED
} vb_peer_message_status_t;

typedef struct
{
    uint32_t id;
    uint16_t length;
    uint8_t outgoing;
    uint8_t status;
    char text[VB_PEER_MAX_TEXT + 1];
} vb_peer_message_t;

typedef struct
{
    vb_peer_state_t state;
    int available;
    int enabled;
    int paired;
    int pairing;
    int connected;
    int role;
    int pending;
    int unread;
    int last_error;
    uint32_t pairing_code;
    uint32_t tx_sequence;
    uint32_t rx_sequence;
    uint32_t revision;
    char peer_id[13];
} vb_peer_status_t;

int vb_peer_init(void);
int vb_peer_enable(int enabled);
void vb_peer_release_host_connection(uint8_t conn_idx);
int vb_peer_pair_start(void);
int vb_peer_pair_confirm(void);
int vb_peer_pair_cancel(void);
int vb_peer_forget(void);
int vb_peer_send_text(const char *text);
int vb_peer_send_hex(const char *hex_text);
int vb_peer_mark_read(void);
int vb_peer_get_status(vb_peer_status_t *status);
int vb_peer_get_message(int logical_index, vb_peer_message_t *message);
int vb_peer_get_message_count(void);
int vb_peer_status_json(char *dst, rt_size_t cap);
int vb_peer_messages_page_json(char *dst, rt_size_t cap, int offset, int limit);
int vb_peer_format_text(const char *selector, char *dst, rt_size_t cap);
int vb_peer_command(int argc, char **argv, char *dst, rt_size_t cap);
uint8_t vb_peer_advertising_flags(void);

/* Implemented by the Runtime BLE advertiser; weak/no-op outside the product Runtime. */
void vb_peer_advertising_changed(uint8_t flags);

#endif
