#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AudioVoiceFrame {
    const int16_t *samples;
    size_t sample_count;   /* total samples per channel */
    uint32_t sample_rate;  /* Hz */
    uint8_t channels;      /* 1 = mono, 2 = stereo */
    float volume;          /* 0.0 - 1.0 */
} AudioVoiceFrame;

bool audio_init(void);
void audio_shutdown(void);

void audio_set_master_volume(float volume);
float audio_master_volume(void);

bool audio_music_set_track(const char *path);
bool audio_music_play(float volume, bool loop);
void audio_music_stop(void);
bool audio_music_is_playing(void);
void audio_music_set_volume(float volume);
float audio_music_volume(void);

bool audio_effect_play_file(const char *path, float volume);

bool audio_voice_submit(uint8_t speaker_id, const AudioVoiceFrame *frame);
void audio_voice_stop(uint8_t speaker_id);
void audio_voice_stop_all(void);
