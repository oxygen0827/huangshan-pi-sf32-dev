#ifndef VB_RUNTIME_CODEX_PET_H
#define VB_RUNTIME_CODEX_PET_H

#include <rtthread.h>
#include "lvgl.h"

typedef struct
{
    int recording;
    int ready;
    int error;
    uint32_t sequence;
    uint32_t bytes;
} vb_codex_pet_voice_snapshot_t;

typedef struct
{
    int (*voice_start)(const char *context);
    int (*voice_stop)(void);
    void (*voice_clear)(void);
    void (*voice_snapshot)(vb_codex_pet_voice_snapshot_t *snapshot);
    int (*key2_pressed)(void);
    int (*rgb_set)(const char *color);
    int (*send_action)(const char *action, const char *request_id);
    int (*cue_play)(const char *cue);
    void (*cue_stop)(void);
} vb_codex_pet_ops_t;

int vb_codex_pet_start(lv_obj_t *root, const vb_codex_pet_ops_t *ops,
                       const char *project);
void vb_codex_pet_stop(void);
void vb_codex_pet_tick(uint32_t now);
void vb_codex_pet_receive_flow(const char *channel, uint32_t sequence,
                               const char *payload);
int vb_codex_pet_active(void);

#endif
