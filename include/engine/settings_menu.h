#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "engine/input.h"
#include "engine/preferences.h"
#include "engine/audio.h"

#define SETTINGS_MENU_MAX_AUDIO_DEVICES 16

typedef struct Renderer Renderer;
typedef struct InputState InputState;

typedef enum SettingsMenuCategory {
    SETTINGS_MENU_CATEGORY_GRAPHICS = 0,
    SETTINGS_MENU_CATEGORY_CONTROLS,
    SETTINGS_MENU_CATEGORY_AUDIO,
    SETTINGS_MENU_CATEGORY_ACCESSIBILITY,
    SETTINGS_MENU_CATEGORY_COUNT
} SettingsMenuCategory;

typedef struct SettingsMenuState {
    SettingsMenuCategory active_category;
    bool waiting_for_rebind;
    InputAction pending_action;
    bool last_initialized;
    bool last_show_fps_overlay;
    bool last_view_bobbing;
    bool last_double_jump;
    int feedback_frames;
    InputAction feedback_action;
    PlatformKey feedback_key;
    bool feedback_has_message;
    char feedback_message[96];
    PlatformWindowMode graphics_mode;
    size_t graphics_resolution_index;
    bool graphics_initialized;
    bool graphics_mode_dropdown_open;
    bool graphics_resolution_dropdown_open;
    size_t graphics_resolution_scroll_offset;
    bool audio_initialized;
    size_t audio_output_index;
    size_t audio_input_index;
    bool audio_output_dropdown_open;
    bool audio_input_dropdown_open;
    size_t audio_output_scroll_offset;
    size_t audio_input_scroll_offset;
    AudioDeviceInfo audio_output_devices[SETTINGS_MENU_MAX_AUDIO_DEVICES];
    size_t audio_output_device_count;
    AudioDeviceInfo audio_input_devices[SETTINGS_MENU_MAX_AUDIO_DEVICES];
    size_t audio_input_device_count;
    bool interaction_locked;
    float interaction_lock_x;
    float interaction_lock_y;
    float interaction_lock_w;
    float interaction_lock_h;
    bool interaction_consumed;
} SettingsMenuState;

typedef struct SettingsMenuContext {
    bool in_game;
    bool *show_fps_overlay;
    bool *view_bobbing;
    bool *double_jump;
    PlatformWindowMode *window_mode;
    uint32_t *resolution_width;
    uint32_t *resolution_height;
    const PreferencesResolution *resolutions;
    size_t resolution_count;
    float *master_volume;
    float *music_volume;
    float *effects_volume;
    float *voice_volume;
    float *microphone_volume;
    uint32_t *audio_output_device;
    uint32_t *audio_input_device;
    PreferencesVoiceActivationMode *voice_activation_mode;
    float *voice_activation_threshold_db;
} SettingsMenuContext;

typedef struct SettingsMenuResult {
    bool back_requested;
    bool show_fps_overlay_changed;
    bool view_bobbing_changed;
    bool double_jump_changed;
    bool binding_changed;
    InputAction binding_changed_action;
    PlatformKey binding_new_key;
    bool binding_reset;
    InputAction binding_reset_action;
    bool reset_all_bindings;
    bool graphics_changed;
    PlatformWindowMode graphics_mode;
    uint32_t graphics_width;
    uint32_t graphics_height;
    bool master_volume_changed;
    bool music_volume_changed;
    bool effects_volume_changed;
    bool voice_volume_changed;
    bool microphone_volume_changed;
    bool output_device_changed;
    bool input_device_changed;
    bool voice_mode_changed;
    bool voice_threshold_changed;
    float master_volume;
    float music_volume;
    float effects_volume;
    float voice_volume;
    float microphone_volume;
    uint32_t output_device;
    uint32_t input_device;
    PreferencesVoiceActivationMode voice_mode;
    float voice_activation_threshold_db;
} SettingsMenuResult;

void settings_menu_init(SettingsMenuState *state);
void settings_menu_cancel_rebind(SettingsMenuState *state);
SettingsMenuResult settings_menu_render(SettingsMenuState *state,
                                        const SettingsMenuContext *context,
                                        Renderer *renderer,
                                        const InputState *input);

