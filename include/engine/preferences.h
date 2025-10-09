#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "engine/platform.h"
#include "engine/input.h"

typedef struct PreferencesResolution {
    uint32_t width;
    uint32_t height;
    const char *label;
} PreferencesResolution;

typedef struct EnginePreferences {
    PlatformWindowMode window_mode;
    uint32_t resolution_width;
    uint32_t resolution_height;
    PlatformKey bindings[INPUT_ACTION_COUNT];
} EnginePreferences;

void preferences_init(void);
void preferences_shutdown(void);

EnginePreferences *preferences_data(void);
const EnginePreferences *preferences_get(void);

void preferences_capture_bindings(void);
void preferences_apply_bindings(void);
bool preferences_set_graphics(PlatformWindowMode mode, uint32_t width, uint32_t height);
bool preferences_save(void);

const char *preferences_config_path(void);
const PreferencesResolution *preferences_resolutions(size_t *out_count);
size_t preferences_find_resolution_index(uint32_t width, uint32_t height);

