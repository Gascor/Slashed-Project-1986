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

typedef struct AudioDeviceInfo {
    uint32_t id;
    char name[128];
    bool is_default;
    bool is_input;
} AudioDeviceInfo;

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
void audio_set_effects_volume(float volume);
float audio_effects_volume(void);
void audio_set_voice_volume(float volume);
float audio_voice_volume(void);
void audio_set_microphone_volume(float volume);
float audio_microphone_volume(void);

bool audio_effect_play_file(const char *path, float volume);

bool audio_voice_submit(uint8_t speaker_id, const AudioVoiceFrame *frame);
void audio_voice_stop(uint8_t speaker_id);
void audio_voice_stop_all(void);

bool audio_microphone_start(void);
void audio_microphone_stop(void);
bool audio_microphone_active(void);
size_t audio_microphone_read(int16_t *out_samples, size_t max_samples);
uint32_t audio_microphone_sample_rate(void);
uint8_t audio_microphone_channels(void);
float audio_microphone_level(void);
float audio_microphone_level_db(void);

bool audio_select_output_device(uint32_t device_id);
uint32_t audio_current_output_device(void);
size_t audio_enumerate_output_devices(AudioDeviceInfo *out_devices, size_t max_devices);

bool audio_select_input_device(uint32_t device_id);
uint32_t audio_current_input_device(void);
size_t audio_enumerate_input_devices(AudioDeviceInfo *out_devices, size_t max_devices);
