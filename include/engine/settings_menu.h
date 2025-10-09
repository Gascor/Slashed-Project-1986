#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "engine/input.h"
#include "engine/preferences.h"

typedef struct Renderer Renderer;
typedef struct InputState InputState;

typedef enum SettingsMenuCategory {
    SETTINGS_MENU_CATEGORY_GRAPHICS = 0,
    SETTINGS_MENU_CATEGORY_CONTROLS,
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
} SettingsMenuResult;

void settings_menu_init(SettingsMenuState *state);
void settings_menu_cancel_rebind(SettingsMenuState *state);
SettingsMenuResult settings_menu_render(SettingsMenuState *state,
                                        const SettingsMenuContext *context,
                                        Renderer *renderer,
                                        const InputState *input);

