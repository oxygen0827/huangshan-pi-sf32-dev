#ifndef VB_RUNTIME_AUDIO_H
#define VB_RUNTIME_AUDIO_H

#include <rtthread.h>

#define VB_RUNTIME_AUDIO_API_VERSION "vibeboard-huangshan-audio-playback/v1"

int vb_runtime_audio_available(void);
int vb_runtime_audio_is_playing(void);
int vb_runtime_audio_prepare_capture(void);
void vb_runtime_audio_finish_capture(void);
int vb_runtime_audio_play_wav(const char *path);
int vb_runtime_audio_play_tone(int continuous);
int vb_runtime_audio_stop(void);
int vb_runtime_audio_set_volume(int volume);
int vb_runtime_audio_read_json(char *dst, rt_size_t cap);
int vb_runtime_audio_format_text(const char *selector, char *dst, rt_size_t cap);
void vb_runtime_audio_shutdown(void);

#endif
