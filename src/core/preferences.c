#include "engine/preferences.h"

#include "engine/input.h"

#include <ctype.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFERENCES_DIRECTORY "config"
#define PREFERENCES_FILENAME  "config/settings.cfg"

static EnginePreferences g_preferences;

static const PreferencesResolution g_resolution_options[] = {
    {3840, 2160, "3840 x 2160 (16:9)"},
    {3440, 1440, "3440 x 1440 (21:9)"},
    {3840, 1600, "3840 x 1600 (21:9)"},
    {2560, 1440, "2560 x 1440 (16:9)"},
    {2560, 1080, "2560 x 1080 (21:9)"},
    {2560, 1600, "2560 x 1600 (16:10)"},
    {2048, 1536, "2048 x 1536 (4:3)"},
    {1920, 1200, "1920 x 1200 (16:10)"},
    {1920, 1080, "1920 x 1080 (16:9)"},
    {1680, 1050, "1680 x 1050 (16:10)"},
    {1600, 900,  "1600 x 900 (16:9)"},
    {1440, 900,  "1440 x 900 (16:10)"},
    {1366, 768,  "1366 x 768 (16:9)"},
    {1280, 1024, "1280 x 1024 (5:4)"},
    {1280, 800,  "1280 x 800 (16:10)"},
    {1280, 720,  "1280 x 720 (16:9)"},
    {1024, 768,  "1024 x 768 (4:3)"},
};

static int preferences_stricmp(const char *a, const char *b)
{
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }

    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }

    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static const char *preferences_mode_token(PlatformWindowMode mode)
{
    switch (mode) {
    case PLATFORM_WINDOW_MODE_FULLSCREEN:
        return "fullscreen";
    case PLATFORM_WINDOW_MODE_WINDOWED:
        return "windowed";
    case PLATFORM_WINDOW_MODE_BORDERLESS:
        return "borderless";
    default:
        break;
    }
    return "windowed";
}

static PlatformWindowMode preferences_mode_from_token(const char *token)
{
    if (!token || !token[0]) {
        return PLATFORM_WINDOW_MODE_FULLSCREEN;
    }
    if (preferences_stricmp(token, "fullscreen") == 0) {
        return PLATFORM_WINDOW_MODE_FULLSCREEN;
    }
    if (preferences_stricmp(token, "borderless") == 0 ||
        preferences_stricmp(token, "borderless_fullscreen") == 0 ||
        preferences_stricmp(token, "borderless_fullscreen_windowed") == 0) {
        return PLATFORM_WINDOW_MODE_BORDERLESS;
    }
    return PLATFORM_WINDOW_MODE_WINDOWED;
}

static const char *preferences_voice_mode_token(PreferencesVoiceActivationMode mode)
{
    switch (mode) {
    case PREFERENCES_VOICE_PUSH_TO_TALK:
        return "push_to_talk";
    case PREFERENCES_VOICE_VOICE_DETECTION:
        return "voice_detection";
    default:
        break;
    }
    return "push_to_talk";
}

static PreferencesVoiceActivationMode preferences_voice_mode_from_token(const char *token)
{
    if (!token || !token[0]) {
        return PREFERENCES_VOICE_PUSH_TO_TALK;
    }
    if (preferences_stricmp(token, "voice_detection") == 0 ||
        preferences_stricmp(token, "voice_activity") == 0 ||
        preferences_stricmp(token, "voice_activation") == 0) {
        return PREFERENCES_VOICE_VOICE_DETECTION;
    }
    return PREFERENCES_VOICE_PUSH_TO_TALK;
}

static float preferences_clamp_volume(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static uint32_t preferences_parse_device_id(const char *value, uint32_t fallback)
{
    if (!value || !value[0] || preferences_stricmp(value, "default") == 0 ||
        preferences_stricmp(value, "system") == 0) {
        return UINT32_MAX;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed > UINT32_MAX) {
        return fallback;
    }
    return (uint32_t)parsed;
}

static void preferences_defaults(EnginePreferences *prefs)
{
    if (!prefs) {
        return;
    }
    prefs->window_mode = PLATFORM_WINDOW_MODE_FULLSCREEN;
    prefs->resolution_width = 1920;
    prefs->resolution_height = 1080;
    prefs->volume_master = 1.0f;
    prefs->volume_music = 0.7f;
    prefs->volume_effects = 1.0f;
    prefs->volume_voice = 1.0f;
    prefs->volume_microphone = 1.0f;
    prefs->audio_output_device = UINT32_MAX;
    prefs->audio_input_device = UINT32_MAX;
    prefs->voice_activation_mode = PREFERENCES_VOICE_PUSH_TO_TALK;
    prefs->voice_activation_threshold_db = -45.0f;
    input_bindings_reset_defaults();
    input_bindings_export(prefs->bindings);
}

static void preferences_load_file(EnginePreferences *prefs)
{
    if (!prefs) {
        return;
    }

    FILE *fp = fopen(PREFERENCES_FILENAME, "r");
    if (!fp) {
        return;
    }

    PlatformKey loaded_bindings[INPUT_ACTION_COUNT];
    input_bindings_export(loaded_bindings);

    PlatformWindowMode mode = prefs->window_mode;
    uint32_t width = prefs->resolution_width;
    uint32_t height = prefs->resolution_height;
    float volume_master = prefs->volume_master;
    float volume_music = prefs->volume_music;
    float volume_effects = prefs->volume_effects;
    float volume_voice = prefs->volume_voice;
    float volume_microphone = prefs->volume_microphone;
    uint32_t output_device = prefs->audio_output_device;
    uint32_t input_device = prefs->audio_input_device;
    PreferencesVoiceActivationMode voice_mode = prefs->voice_activation_mode;
    float voice_threshold_db = prefs->voice_activation_threshold_db;

    char section[32] = {0};
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        char *start = line;
        while (isspace((unsigned char)*start)) {
            ++start;
        }
        if (*start == '\0' || *start == '#' || *start == ';') {
            continue;
        }

        char *end = start + strlen(start);
        while (end > start && (end[-1] == '\n' || end[-1] == '\r' || isspace((unsigned char)end[-1]))) {
            --end;
        }
        *end = '\0';

        if (start[0] == '[' && end > start && end[-1] == ']') {
            size_t len = (size_t)(end - start - 2);
            if (len >= sizeof(section)) {
                len = sizeof(section) - 1;
            }
            memcpy(section, start + 1, len);
            section[len] = '\0';
            continue;
        }

        char *equals = strchr(start, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char *key = start;
        char *value = equals + 1;

        char *key_end = key + strlen(key);
        while (key_end > key && isspace((unsigned char)key_end[-1])) {
            --key_end;
        }
        *key_end = '\0';

        while (isspace((unsigned char)*value)) {
            ++value;
        }
        char *value_end = value + strlen(value);
        while (value_end > value && isspace((unsigned char)value_end[-1])) {
            --value_end;
        }
        *value_end = '\0';

        if (preferences_stricmp(section, "graphics") == 0) {
            if (preferences_stricmp(key, "mode") == 0) {
                mode = preferences_mode_from_token(value);
            } else if (preferences_stricmp(key, "width") == 0) {
                uint32_t parsed = (uint32_t)strtoul(value, NULL, 10);
                if (parsed >= 320) {
                    width = parsed;
                }
            } else if (preferences_stricmp(key, "height") == 0) {
                uint32_t parsed = (uint32_t)strtoul(value, NULL, 10);
                if (parsed >= 240) {
                    height = parsed;
                }
            }
        } else if (preferences_stricmp(section, "controls") == 0) {
            InputAction action = input_action_from_token(key);
            if (action < INPUT_ACTION_COUNT) {
                PlatformKey mapped = input_key_from_token(value);
                loaded_bindings[action] = mapped;
            }
        } else if (preferences_stricmp(section, "audio") == 0) {
            if (preferences_stricmp(key, "master") == 0) {
                char *endptr = NULL;
                float parsed = strtof(value, &endptr);
                if (endptr != value) {
                    volume_master = preferences_clamp_volume(parsed);
                }
            } else if (preferences_stricmp(key, "music") == 0) {
                char *endptr = NULL;
                float parsed = strtof(value, &endptr);
                if (endptr != value) {
                    volume_music = preferences_clamp_volume(parsed);
                }
            } else if (preferences_stricmp(key, "effects") == 0 ||
                       preferences_stricmp(key, "sfx") == 0) {
                char *endptr = NULL;
                float parsed = strtof(value, &endptr);
                if (endptr != value) {
                    volume_effects = preferences_clamp_volume(parsed);
                }
            } else if (preferences_stricmp(key, "voice") == 0 ||
                       preferences_stricmp(key, "voice_playback") == 0) {
                char *endptr = NULL;
                float parsed = strtof(value, &endptr);
                if (endptr != value) {
                    volume_voice = preferences_clamp_volume(parsed);
                }
            } else if (preferences_stricmp(key, "microphone") == 0 ||
                       preferences_stricmp(key, "mic") == 0) {
                char *endptr = NULL;
                float parsed = strtof(value, &endptr);
                if (endptr != value) {
                    volume_microphone = preferences_clamp_volume(parsed);
                }
            } else if (preferences_stricmp(key, "output_device") == 0) {
                output_device = preferences_parse_device_id(value, output_device);
            } else if (preferences_stricmp(key, "input_device") == 0) {
                input_device = preferences_parse_device_id(value, input_device);
            } else if (preferences_stricmp(key, "voice_mode") == 0 ||
                       preferences_stricmp(key, "voice_activation_mode") == 0) {
                voice_mode = preferences_voice_mode_from_token(value);
            } else if (preferences_stricmp(key, "voice_threshold_db") == 0 ||
                       preferences_stricmp(key, "voice_threshold") == 0) {
                char *endptr = NULL;
                float parsed = strtof(value, &endptr);
                if (endptr != value) {
                    voice_threshold_db = parsed;
                }
            }
        }
    }

    fclose(fp);

    prefs->window_mode = mode;
    prefs->resolution_width = width;
    prefs->resolution_height = height;
    prefs->volume_master = preferences_clamp_volume(volume_master);
    prefs->volume_music = preferences_clamp_volume(volume_music);
    prefs->volume_effects = preferences_clamp_volume(volume_effects);
    prefs->volume_voice = preferences_clamp_volume(volume_voice);
    prefs->volume_microphone = preferences_clamp_volume(volume_microphone);
    prefs->audio_output_device = output_device;
    prefs->audio_input_device = input_device;
    if (voice_threshold_db > 0.0f) {
        voice_threshold_db = 0.0f;
    }
    if (voice_threshold_db < -120.0f) {
        voice_threshold_db = -120.0f;
    }
    prefs->voice_activation_threshold_db = voice_threshold_db;
    prefs->voice_activation_mode = voice_mode;
    input_bindings_import(loaded_bindings);
    input_bindings_export(prefs->bindings);
}

void preferences_init(void)
{
    preferences_defaults(&g_preferences);
    preferences_load_file(&g_preferences);
    preferences_apply_bindings();
}

void preferences_shutdown(void)
{
}

EnginePreferences *preferences_data(void)
{
    return &g_preferences;
}

const EnginePreferences *preferences_get(void)
{
    return &g_preferences;
}

void preferences_capture_bindings(void)
{
    input_bindings_export(g_preferences.bindings);
}

void preferences_apply_bindings(void)
{
    input_bindings_import(g_preferences.bindings);
}

bool preferences_set_graphics(PlatformWindowMode mode, uint32_t width, uint32_t height)
{
    if (mode >= PLATFORM_WINDOW_MODE_COUNT || width == 0U || height == 0U) {
        return false;
    }
    g_preferences.window_mode = mode;
    g_preferences.resolution_width = width;
    g_preferences.resolution_height = height;
    return true;
}

bool preferences_save(void)
{
    preferences_capture_bindings();

    _mkdir(PREFERENCES_DIRECTORY);

    FILE *fp = fopen(PREFERENCES_FILENAME, "w");
    if (!fp) {
        return false;
    }

    fprintf(fp, "# Slashed Project 1986 user settings\n");
    fprintf(fp, "[graphics]\n");
    fprintf(fp, "mode=%s\n", preferences_mode_token(g_preferences.window_mode));
    fprintf(fp, "width=%u\n", g_preferences.resolution_width);
    fprintf(fp, "height=%u\n\n", g_preferences.resolution_height);

    fprintf(fp, "[audio]\n");
    fprintf(fp, "master=%.3f\n", preferences_clamp_volume(g_preferences.volume_master));
    fprintf(fp, "music=%.3f\n", preferences_clamp_volume(g_preferences.volume_music));
    fprintf(fp, "effects=%.3f\n", preferences_clamp_volume(g_preferences.volume_effects));
    fprintf(fp, "voice=%.3f\n", preferences_clamp_volume(g_preferences.volume_voice));
    fprintf(fp, "microphone=%.3f\n", preferences_clamp_volume(g_preferences.volume_microphone));
    if (g_preferences.audio_output_device == UINT32_MAX) {
        fprintf(fp, "output_device=default\n");
    } else {
        fprintf(fp, "output_device=%u\n", g_preferences.audio_output_device);
    }
    if (g_preferences.audio_input_device == UINT32_MAX) {
        fprintf(fp, "input_device=default\n");
    } else {
        fprintf(fp, "input_device=%u\n", g_preferences.audio_input_device);
    }
    fprintf(fp, "voice_mode=%s\n", preferences_voice_mode_token(g_preferences.voice_activation_mode));
    fprintf(fp, "voice_threshold_db=%.2f\n\n", g_preferences.voice_activation_threshold_db);

    fprintf(fp, "[controls]\n");
    size_t action_count = input_action_count();
    for (size_t i = 0; i < action_count; ++i) {
        InputAction action = input_action_by_index(i);
        const char *action_token = input_action_token(action);
        if (!action_token || action_token[0] == '\0') {
            continue;
        }

        PlatformKey key = g_preferences.bindings[i];
        const char *key_token = input_key_token(key);
        if (!key_token || key_token[0] == '\0') {
            key_token = "unassigned";
        }

        fprintf(fp, "%s=%s\n", action_token, key_token);
    }

    fclose(fp);
    return true;
}

const char *preferences_config_path(void)
{
    return PREFERENCES_FILENAME;
}

const PreferencesResolution *preferences_resolutions(size_t *out_count)
{
    if (out_count) {
        *out_count = sizeof(g_resolution_options) / sizeof(g_resolution_options[0]);
    }
    return g_resolution_options;
}

size_t preferences_find_resolution_index(uint32_t width, uint32_t height)
{
    size_t count = sizeof(g_resolution_options) / sizeof(g_resolution_options[0]);
    for (size_t i = 0; i < count; ++i) {
        if (g_resolution_options[i].width == width && g_resolution_options[i].height == height) {
            return i;
        }
    }
    return 0;
}
