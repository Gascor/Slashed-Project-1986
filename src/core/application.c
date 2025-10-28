#include "engine/core.h"
#include "engine/platform.h"
#include "engine/renderer.h"
#include "engine/input.h"
#include "engine/game.h"
#include "engine/physics.h"
#include "engine/ecs.h"
#include "engine/resources.h"
#include "engine/math.h"
#include "engine/camera.h"
#include "engine/network.h"
#include "engine/preferences.h"
#include "engine/network_server.h"
#include "engine/master_protocol.h"
#include "engine/network_master.h"
#include "engine/settings_menu.h"
#include "engine/server_browser.h"
#include "engine/audio.h"

#define APP_MASTER_DEFAULT_HOST "127.0.0.1"
#define APP_MASTER_DEFAULT_PORT 27050

#define MENU_CAMERA_DEFAULT_ASPECT (16.0f / 9.0f)
#define MENU_CAMERA_MAIN_POS vec3_make(0.0f, 1.7f, 6.0f)
#define MENU_CAMERA_MAIN_YAW ((float)M_PI)
#define MENU_CAMERA_MAIN_PITCH (-0.08f)
#define MENU_CAMERA_BROWSER_POS vec3_make(-2.8f, 1.9f, 5.2f)
#define MENU_CAMERA_BROWSER_YAW ((float)(M_PI * 0.82))
#define MENU_CAMERA_BROWSER_PITCH (-0.12f)
#define MENU_CAMERA_OPTIONS_POS vec3_make(2.6f, 1.75f, 4.8f)
#define MENU_CAMERA_OPTIONS_YAW ((float)(M_PI * 1.12))
#define MENU_CAMERA_OPTIONS_PITCH (-0.05f)
#define MENU_CAMERA_ABOUT_POS vec3_make(0.6f, 2.2f, 6.4f)
#define MENU_CAMERA_ABOUT_YAW ((float)(M_PI * 0.95))
#define MENU_CAMERA_ABOUT_PITCH (-0.2f)
#define MENU_CAMERA_ANIM_DURATION 0.75f
#define MENU_MUSIC_DEFAULT_PATH "assets/audio/menu_theme.mp3"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <time.h>
#endif

static void sleep_milliseconds(unsigned int ms)
{
    if (ms == 0U) {
        return;
    }

#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts = {0};
    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

typedef enum AppScreen {
    APP_SCREEN_MAIN_MENU = 0,
    APP_SCREEN_SERVER_BROWSER,
    APP_SCREEN_OPTIONS,
    APP_SCREEN_ABOUT,
    APP_SCREEN_IN_GAME
} AppScreen;

typedef struct AppState {
    AppScreen screen;
    AppScreen next_screen;
    bool show_fps_overlay;
    bool request_shutdown;
    float master_volume;
    float music_volume;
    float effects_volume;
    float voice_volume;
    float microphone_volume;
    bool music_playing;
    bool audio_available;
    uint32_t audio_output_device;
    uint32_t audio_input_device;
    PreferencesVoiceActivationMode voice_activation_mode;
    float voice_activation_threshold_db;

    MasterServerEntry pending_entry;
    bool pending_join;

    ServerBrowserState browser;
    SettingsMenuState settings_menu;

    PlatformWindow *window;
    PlatformWindowMode window_mode;
    uint32_t resolution_width;
    uint32_t resolution_height;

    char master_server_host[MASTER_SERVER_ADDR_MAX];
    MasterClientConfig master_config;
    bool server_browser_pending_refresh;

    Camera menu_camera;
    bool menu_camera_ready;

    vec3 camera_target_pos;
    vec3 camera_start_pos;
    float camera_target_yaw;
    float camera_start_yaw;
    float camera_target_pitch;
    float camera_start_pitch;
    float camera_anim_time;
    float camera_anim_duration;
    bool camera_animating;

    double menu_time;
} AppState;

static bool point_in_rect(float px, float py, float rx, float ry, float rw, float rh)
{
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

static bool app_apply_graphics(AppState *app,
                               PlatformWindow *window,
                               Renderer *renderer,
                               PlatformWindowMode mode,
                               uint32_t width,
                               uint32_t height,
                               uint32_t *viewport_width,
                               uint32_t *viewport_height)
{
    if (!app || !window || !renderer || !viewport_width || !viewport_height) {
        return false;
    }

    if (!platform_window_set_mode(window, mode, width, height)) {
        return false;
    }

    uint32_t actual_width = width;
    uint32_t actual_height = height;
    platform_window_get_size(window, &actual_width, &actual_height);

    renderer_set_viewport(renderer, actual_width, actual_height);

    app->window_mode = mode;
    app->resolution_width = actual_width;
    app->resolution_height = actual_height;
    *viewport_width = actual_width;
    *viewport_height = actual_height;
    return true;
}

static void app_prepare_menu_camera(AppState *app, uint32_t width, uint32_t height)
{
    if (!app) {
        return;
    }

    float aspect = MENU_CAMERA_DEFAULT_ASPECT;
    if (width > 0U && height > 0U) {
        aspect = (float)width / (float)height;
    }

    if (!app->menu_camera_ready) {
        app->menu_camera = camera_create(MENU_CAMERA_MAIN_POS,
                                         MENU_CAMERA_MAIN_YAW,
                                         MENU_CAMERA_MAIN_PITCH,
                                         CAMERA_DEFAULT_FOV_DEG * (float)M_PI / 180.0f,
                                         aspect,
                                         CAMERA_DEFAULT_NEAR,
                                         CAMERA_DEFAULT_FAR);
        camera_set_pitch_limits(&app->menu_camera, -0.9f, 0.45f);
        app->menu_camera_ready = true;

        app->camera_start_pos = app->menu_camera.position;
        app->camera_target_pos = app->menu_camera.position;
        app->camera_start_yaw = app->menu_camera.yaw;
        app->camera_target_yaw = app->menu_camera.yaw;
        app->camera_start_pitch = app->menu_camera.pitch;
        app->camera_target_pitch = app->menu_camera.pitch;
        app->camera_anim_time = 0.0f;
        app->camera_anim_duration = 0.0f;
        app->camera_animating = false;
    } else {
        camera_set_aspect(&app->menu_camera, aspect);
    }
}

static void app_set_camera_target(AppState *app,
                                  vec3 target_pos,
                                  float target_yaw,
                                  float target_pitch,
                                  float duration)
{
    if (!app) {
        return;
    }

    if (!app->menu_camera_ready) {
        app_prepare_menu_camera(app, 1280U, 720U);
    }

    app->camera_start_pos = app->menu_camera.position;
    app->camera_target_pos = target_pos;
    app->camera_start_yaw = app->menu_camera.yaw;
    app->camera_target_yaw = target_yaw;
    app->camera_start_pitch = app->menu_camera.pitch;
    app->camera_target_pitch = target_pitch;
    app->camera_anim_time = 0.0f;

    if (duration <= 0.0f) {
        app->camera_anim_duration = 0.0f;
        app->camera_animating = false;
        app->menu_camera.position = target_pos;
        app->menu_camera.yaw = target_yaw;
        app->menu_camera.pitch = target_pitch;
    } else {
        app->camera_anim_duration = duration;
        app->camera_animating = true;
    }
}

static void app_update_menu_camera(AppState *app, float dt)
{
    if (!app || !app->menu_camera_ready) {
        return;
    }

    if (!app->camera_animating) {
        return;
    }

    if (app->camera_anim_duration <= 0.0f) {
        app->menu_camera.position = app->camera_target_pos;
        app->menu_camera.yaw = app->camera_target_yaw;
        app->menu_camera.pitch = app->camera_target_pitch;
        app->camera_animating = false;
        return;
    }

    app->camera_anim_time += dt;
    float t = app->camera_anim_time / app->camera_anim_duration;
    if (t >= 1.0f) {
        t = 1.0f;
        app->camera_animating = false;
    } else if (t < 0.0f) {
        t = 0.0f;
    }

    float smooth = t * t * (3.0f - 2.0f * t);
    vec3 delta = vec3_sub(app->camera_target_pos, app->camera_start_pos);
    vec3 offset = vec3_scale(delta, smooth);
    app->menu_camera.position = vec3_add(app->camera_start_pos, offset);
    app->menu_camera.yaw = app->camera_start_yaw + (app->camera_target_yaw - app->camera_start_yaw) * smooth;
    app->menu_camera.pitch = app->camera_start_pitch + (app->camera_target_pitch - app->camera_start_pitch) * smooth;
}

static float app_music_target_volume(const AppState *app)
{
    if (!app) {
        return 0.0f;
    }

    float volume = app->master_volume * app->music_volume;
    if (volume < 0.0f) {
        volume = 0.0f;
    } else if (volume > 1.0f) {
        volume = 1.0f;
    }
    return volume;
}

static void app_update_music(AppState *app, AppScreen previous_screen)
{
    if (!app || !app->audio_available) {
        return;
    }

    audio_set_master_volume(app->master_volume);

    if (previous_screen == APP_SCREEN_MAIN_MENU && app->screen != APP_SCREEN_MAIN_MENU) {
        if (app->music_playing) {
            audio_music_stop();
            app->music_playing = false;
        }
        return;
    }

    if (app->screen == APP_SCREEN_MAIN_MENU) {
        float target_volume = app_music_target_volume(app);
        bool currently_playing = audio_music_is_playing();
        if (!currently_playing) {
            app->music_playing = false;
        }

        if (currently_playing) {
            audio_music_set_volume(target_volume);
            app->music_playing = true;
        } else if (!app->music_playing) {
            app->music_playing = audio_music_play(target_volume, true);
            if (!app->music_playing) {
                app->audio_available = false;
            }
        }
    }
}

static void app_move_camera_to_screen(AppState *app, AppScreen screen)
{
    if (!app) {
        return;
    }

    float duration = MENU_CAMERA_ANIM_DURATION;
    switch (screen) {
    case APP_SCREEN_MAIN_MENU:
        app_set_camera_target(app, MENU_CAMERA_MAIN_POS, MENU_CAMERA_MAIN_YAW, MENU_CAMERA_MAIN_PITCH, duration * 0.6f);
        break;
    case APP_SCREEN_SERVER_BROWSER:
        app_set_camera_target(app, MENU_CAMERA_BROWSER_POS, MENU_CAMERA_BROWSER_YAW, MENU_CAMERA_BROWSER_PITCH, duration);
        break;
    case APP_SCREEN_OPTIONS:
        app_set_camera_target(app, MENU_CAMERA_OPTIONS_POS, MENU_CAMERA_OPTIONS_YAW, MENU_CAMERA_OPTIONS_PITCH, duration);
        break;
    case APP_SCREEN_ABOUT:
        app_set_camera_target(app, MENU_CAMERA_ABOUT_POS, MENU_CAMERA_ABOUT_YAW, MENU_CAMERA_ABOUT_PITCH, duration);
        break;
    case APP_SCREEN_IN_GAME:
        app->camera_animating = false;
        break;
    default:
        app_set_camera_target(app, MENU_CAMERA_MAIN_POS, MENU_CAMERA_MAIN_YAW, MENU_CAMERA_MAIN_PITCH, duration);
        break;
    }
}
static void app_server_browser_request_refresh(AppState *app)
{
    if (app) {
        app->server_browser_pending_refresh = true;
    }
}

static void app_server_browser_refresh(AppState *app)
{
    if (!app) {
        return;
    }

    ServerBrowserState *browser = &app->browser;
        server_browser_refresh(browser, &app->master_config, app->menu_time);
    app->server_browser_pending_refresh = false;
}

static const char *app_server_mode_label(uint8_t mode)
{
    switch (mode) {
    case 0:
        return "Capture the Flag";
    case 1:
        return "Team Deathmatch";
    case 2:
        return "Slender Hunt";
    default:
        break;
    }
    return NULL;
}

static float ui_hover_mix(double time_seconds)
{
    float pulse = sinf((float)(time_seconds * 2.0 * (float)M_PI));
    return (pulse * 0.5f + 0.5f) * 0.6f;
}

static void ui_apply_interaction_tint(float *r,
                                      float *g,
                                      float *b,
                                      bool hovered,
                                      bool pressed,
                                      double time_seconds)
{
    if (!r || !g || !b) {
        return;
    }

    if (hovered && !pressed) {
        float mix = ui_hover_mix(time_seconds);
        *r += (1.0f - *r) * mix;
        *g += (1.0f - *g) * mix;
        *b += (1.0f - *b) * mix;
    }

    if (pressed) {
        const float darken = 0.7f;
        const float scale = 1.0f - darken;
        *r *= scale;
        *g *= scale;
        *b *= scale;
    }
}

static bool ui_button(Renderer *renderer,
                      const InputState *input,
                      float x,
                      float y,
                      float width,
                      float height,
                      const char *label,
                      double time_seconds,
                      float alpha)
{
    if (!renderer || !label) {
        return false;
    }

    float px = input ? (float)input->mouse_x : -1000.0f;
    float py = input ? (float)input->mouse_y : -1000.0f;
    bool hovered = input && point_in_rect(px, py, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = hovered ? 0.22f : 0.12f;
    float r = base;
    float g = base * 0.9f;
    float b = base * 0.8f;
    ui_apply_interaction_tint(&r, &g, &b, hovered, pressed, time_seconds);

    float rect_alpha = 0.9f * alpha;
    if (hovered && !pressed) {
        float mix = ui_hover_mix(time_seconds);
        rect_alpha += (1.0f - rect_alpha) * (mix * 0.5f);
    }
    if (rect_alpha > 1.0f) {
        rect_alpha = 1.0f;
    }

    renderer_draw_ui_rect(renderer, x, y, width, height, r, g, b, rect_alpha);
    renderer_draw_ui_text(renderer, x + 24.0f, y + height * 0.5f - 8.0f, label, 0.97f, 0.97f, 0.98f, 1.0f * alpha);
    return pressed;
}

static void app_render_main_menu(AppState *app,
                                 Renderer *renderer,
                                 const InputState *input,
                                 bool *start_local_game)
{
    if (!app || !renderer) {
        return;
    }

    renderer_begin_ui(renderer);

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    const float frame_margin = 48.0f;
    const bool compact_layout = width >= 1600.0f && height >= 900.0f;
    const float target_panel_width = compact_layout ? (width * 0.20f) : 840.0f;
    const float target_panel_height = compact_layout ? (height * 0.70f) : 760.0f;
    float panel_width = target_panel_width;
    float panel_height = target_panel_height;

    const float max_panel_width = width - frame_margin * 2.0f;
    const float max_panel_height = height - frame_margin * 2.0f;
    if (panel_width > max_panel_width) {
        panel_width = max_panel_width;
    }
    if (panel_height > max_panel_height) {
        panel_height = max_panel_height;
    }

    if (!compact_layout) {
        if (panel_width < 480.0f) {
            panel_width = width * 0.9f;
        }
        if (panel_height < 420.0f) {
            panel_height = height * 0.85f;
        }
    }

    float panel_x = compact_layout ? frame_margin : (width - panel_width) * 0.5f;
    float panel_y = compact_layout ? (height - frame_margin - panel_height) : ((height - panel_height) * 0.5f);
    if (panel_y < frame_margin) {
        panel_y = frame_margin;
    }
    float logo_center_x = panel_x + panel_width * 0.5f;
    float logo_center_y = height * 0.15f;
    float logo_max_width = panel_width * 0.9f;
    float logo_max_height = height * 0.18f;
    renderer_draw_ui_logo(renderer, logo_center_x, logo_center_y, logo_max_width, logo_max_height);

    renderer_draw_ui_rect(renderer,
                          panel_x - 18.0f,
                          panel_y - 18.0f,
                          panel_width + 36.0f,
                          panel_height + 36.0f,
                          0.025f,
                          0.025f,
                          0.045f,
                          0.86f);
    renderer_draw_ui_rect(renderer,
                          panel_x,
                          panel_y,
                          panel_width,
                          panel_height,
                          0.055f,
                          0.055f,
                          0.085f,
                          0.94f);

    const float header_height = panel_height * 0.22f;
    renderer_draw_ui_rect(renderer,
                          panel_x,
                          panel_y,
                          panel_width,
                          header_height,
                          0.08f,
                          0.08f,
                          0.12f,
                          0.88f);

    const float header_text_x = compact_layout ? (panel_x + 20.0f) : (panel_x + 64.0f);
    const char *menu_subtitle = compact_layout ? "Prototype extraction FPS." : "A tactical extraction shooter prototype set in a collapsed 1980s parallel city.";
    renderer_draw_ui_text(renderer, header_text_x, panel_y + 64.0f, "SLASHED PROJECT 1986", 0.95f, 0.95f, 0.98f, 1.0f);
    renderer_draw_ui_text(renderer,
                          header_text_x,
                          panel_y + (compact_layout ? 96.0f : 108.0f),
                          menu_subtitle,
                          0.78f,
                          0.78f,
                          0.86f,
                          0.92f);

    const float button_column_x = compact_layout ? (panel_x + 16.0f) : (panel_x + 72.0f);
    float button_y = panel_y + header_height + (compact_layout ? 32.0f : 48.0f);
    const float button_height = compact_layout ? 52.0f : 62.0f;
    const float button_spacing = compact_layout ? 18.0f : 22.0f;
    float button_width = compact_layout ? (panel_width - 32.0f) : (panel_width - (button_column_x - panel_x) - 92.0f);
    if (button_width < 80.0f) {
        button_width = 80.0f;
    }

    if (ui_button(renderer,
                  input,
                  button_column_x,
                  button_y,
                  button_width,
                  button_height,
                  "Create A Match",
                  app ? app->menu_time : 0.0,
                  1.0f)) {
        if (start_local_game) {
            *start_local_game = true;
        }
    }
    button_y += button_height + button_spacing;

    if (ui_button(renderer,
                  input,
                  button_column_x,
                  button_y,
                  button_width,
                  button_height,
                  "Join A Server",
                  app ? app->menu_time : 0.0,
                  1.0f)) {
        app->next_screen = APP_SCREEN_SERVER_BROWSER;
    }
    button_y += button_height + button_spacing;

    if (ui_button(renderer,
                  input,
                  button_column_x,
                  button_y,
                  button_width,
                  button_height,
                  "Settings",
                  app ? app->menu_time : 0.0,
                  1.0f)) {
        app->next_screen = APP_SCREEN_OPTIONS;
    }
    button_y += button_height + button_spacing;

    if (ui_button(renderer,
                  input,
                  button_column_x,
                  button_y,
                  button_width,
                  button_height,
                  "About",
                  app ? app->menu_time : 0.0,
                  1.0f)) {
        app->next_screen = APP_SCREEN_ABOUT;
    }
    button_y += button_height + button_spacing;

    if (ui_button(renderer,
                  input,
                  button_column_x,
                  button_y,
                  button_width,
                  button_height,
                  "Quit",
                  app ? app->menu_time : 0.0,
                  1.0f)) {
        app->request_shutdown = true;
    }

    const char *footer_line1 = "Powered by Slashed Engine 1";
    const char *footer_line2 = "Slashed Project 1986 - Build 0000008";
    const float footer_margin = 28.0f;
    const float line_spacing = 20.0f;
    const float line_height = 18.0f;
    const float align_padding = 12.0f;
    const float char_width = 8.0f;

    float footer_bottom = height - footer_margin;
    float min_footer_bottom = panel_y + panel_height + line_spacing + line_height;
    if (footer_bottom < min_footer_bottom) {
        footer_bottom = min_footer_bottom;
    }

    float line2_width = (float)strlen(footer_line2) * char_width;
    float line1_width = (float)strlen(footer_line1) * char_width;

    float line2_x = width - footer_margin - align_padding - line2_width;
    float line1_x = width - footer_margin - align_padding - line1_width;
    if (line2_x < footer_margin) {
        line2_x = footer_margin;
    }
    if (line1_x < footer_margin) {
        line1_x = footer_margin;
    }

    float line2_y = footer_bottom - line_height;
    float line1_y = line2_y - line_spacing;

    renderer_draw_ui_text(renderer,
                          line1_x,
                          line1_y,
                          footer_line1,
                          0.72f,
                          0.82f,
                          0.94f,
                          0.95f);
    renderer_draw_ui_text(renderer,
                          line2_x,
                          line2_y,
                          footer_line2,
                          0.65f,
                          0.75f,
                          0.88f,
                          0.9f);

    renderer_end_ui(renderer);
}

static void app_render_options(AppState *app,
                               Renderer *renderer,
                               const InputState *input,
                               uint32_t *viewport_width,
                               uint32_t *viewport_height)
{
    if (!app || !renderer || !viewport_width || !viewport_height) {
        return;
    }

    renderer_begin_ui(renderer);

    SettingsMenuContext context = {0};
    context.in_game = false;
    context.show_fps_overlay = &app->show_fps_overlay;
    context.window_mode = &app->window_mode;
    context.resolution_width = &app->resolution_width;
    context.resolution_height = &app->resolution_height;
    size_t resolution_count = 0;
    context.resolutions = preferences_resolutions(&resolution_count);
    context.resolution_count = resolution_count;
    EnginePreferences *prefs_data = preferences_data();
    context.master_volume = prefs_data ? &prefs_data->volume_master : NULL;
    context.music_volume = prefs_data ? &prefs_data->volume_music : NULL;
    context.effects_volume = prefs_data ? &prefs_data->volume_effects : NULL;
    context.voice_volume = prefs_data ? &prefs_data->volume_voice : NULL;
    context.microphone_volume = prefs_data ? &prefs_data->volume_microphone : NULL;
    context.audio_output_device = prefs_data ? &prefs_data->audio_output_device : NULL;
    context.audio_input_device = prefs_data ? &prefs_data->audio_input_device : NULL;
    context.voice_activation_mode = prefs_data ? &prefs_data->voice_activation_mode : NULL;
    context.voice_activation_threshold_db = prefs_data ? &prefs_data->voice_activation_threshold_db : NULL;

    SettingsMenuResult result = settings_menu_render(&app->settings_menu,
                                                     &context,
                                                     renderer,
                                                     input,
                                                     app ? app->menu_time : 0.0);
    if (result.back_requested) {
        app->next_screen = APP_SCREEN_MAIN_MENU;
        settings_menu_cancel_rebind(&app->settings_menu);
    }

    if (result.binding_changed || result.binding_reset || result.reset_all_bindings) {
        preferences_capture_bindings();
        preferences_save();
    }

    if (result.graphics_changed && result.graphics_width > 0U && result.graphics_height > 0U) {
        PlatformWindowMode new_mode = result.graphics_mode;
        uint32_t new_width = result.graphics_width;
        uint32_t new_height = result.graphics_height;

        bool applied = false;
        if (app->window) {
            applied = app_apply_graphics(app,
                                       app->window,
                                       renderer,
                                       new_mode,
                                       new_width,
                                       new_height,
                                       viewport_width,
                                       viewport_height);
        }

        if (!applied) {
            app->window_mode = new_mode;
            app->resolution_width = new_width;
            app->resolution_height = new_height;
        }

        if (preferences_set_graphics(app->window_mode, app->resolution_width, app->resolution_height)) {
            preferences_save();
        }
    }

    bool audio_prefs_changed = false;
    if (result.master_volume_changed && prefs_data) {
        app->master_volume = prefs_data->volume_master;
        audio_set_master_volume(app->master_volume);
        audio_music_set_volume(app_music_target_volume(app));
        audio_prefs_changed = true;
    }
    if (result.music_volume_changed && prefs_data) {
        app->music_volume = prefs_data->volume_music;
        audio_music_set_volume(app_music_target_volume(app));
        audio_prefs_changed = true;
    }
    if (result.effects_volume_changed && prefs_data) {
        app->effects_volume = prefs_data->volume_effects;
        audio_set_effects_volume(app->effects_volume);
        audio_prefs_changed = true;
    }
    if (result.voice_volume_changed && prefs_data) {
        app->voice_volume = prefs_data->volume_voice;
        audio_set_voice_volume(app->voice_volume);
        audio_prefs_changed = true;
    }
    if (result.microphone_volume_changed && prefs_data) {
        app->microphone_volume = prefs_data->volume_microphone;
        audio_set_microphone_volume(app->microphone_volume);
        audio_prefs_changed = true;
    }
    if (result.output_device_changed && prefs_data) {
        app->audio_output_device = prefs_data->audio_output_device;
        audio_select_output_device(app->audio_output_device);
        audio_prefs_changed = true;
    }
    if (result.input_device_changed && prefs_data) {
        app->audio_input_device = prefs_data->audio_input_device;
        audio_select_input_device(app->audio_input_device);
        audio_prefs_changed = true;
    }
    if (result.voice_mode_changed && prefs_data) {
        app->voice_activation_mode = prefs_data->voice_activation_mode;
        audio_prefs_changed = true;
    }
    if (result.voice_threshold_changed && prefs_data) {
        app->voice_activation_threshold_db = prefs_data->voice_activation_threshold_db;
        audio_prefs_changed = true;
    }
    if (audio_prefs_changed) {
        preferences_save();
    }

    renderer_end_ui(renderer);
}


static void app_render_about(AppState *app,
                             Renderer *renderer,
                             const InputState *input)
{
    (void)app;

    renderer_begin_ui(renderer);

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    const float panel_width = 540.0f;
    const float panel_height = 360.0f;
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    renderer_draw_ui_rect(renderer, panel_x - 12.0f, panel_y - 12.0f, panel_width + 24.0f, panel_height + 24.0f, 0.04f, 0.04f, 0.07f, 0.9f);
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 32.0f, "About", 0.95f, 0.95f, 0.98f, 1.0f);

    renderer_draw_ui_text(renderer,
                          panel_x + 32.0f,
                          panel_y + 84.0f,
                          "Slashed Project 1986 is a prototype retro FPS sandbox.\n"
                          "This build runs a native renderer, placeholder physics and a mock networking layer.\n"
                          "Use the main menu to host or browse matches.\n\n"
                          "Prototype crafted by Lucas. Game modes are placeholders awaiting content and backend.",
                          0.88f,
                          0.88f,
                          0.9f,
                          0.95f);

    if (ui_button(renderer,
                  input,
                  panel_x + 32.0f,
                  panel_y + panel_height - 64.0f,
                  panel_width - 64.0f,
                  48.0f,
                  "Back",
                  app ? app->menu_time : 0.0,
                  1.0f)) {
        if (app) {
            app->next_screen = APP_SCREEN_MAIN_MENU;
        }
    }

    renderer_end_ui(renderer);
}
static void app_render_server_browser(AppState *app,
                                      Renderer *renderer,
                                      const InputState *input,
                                      bool *request_join)
{
    if (!app || !renderer) {
        return;
    }

    ServerBrowserState *browser = &app->browser;

    if (!browser->open) {
        if (server_browser_open(browser, &app->master_config, app->menu_time)) {
            app->server_browser_pending_refresh = false;
        }
    } else if (app->server_browser_pending_refresh) {
        app_server_browser_refresh(app);
    }

    if (input) {
        if (input->mouse_wheel > 0.1f) {
            server_browser_move_selection(browser, -1);
        } else if (input->mouse_wheel < -0.1f) {
            server_browser_move_selection(browser, 1);
        }

        if (input->key_pressed[PLATFORM_KEY_UP]) {
            server_browser_move_selection(browser, -1);
        } else if (input->key_pressed[PLATFORM_KEY_DOWN]) {
            server_browser_move_selection(browser, 1);
        }

        if (input->key_pressed[PLATFORM_KEY_ENTER] && request_join && server_browser_has_entries(browser)) {
            *request_join = true;
        }

        if (input->escape_pressed) {
            app->next_screen = APP_SCREEN_MAIN_MENU;
        }
    }

    renderer_begin_ui(renderer);

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    float panel_width = width - 120.0f;
    float panel_height = height - 140.0f;
    if (width >= 1920.0f) {
        if (panel_width > 1280.0f) {
            panel_width = 1280.0f;
        }
        if (panel_height > 820.0f) {
            panel_height = 820.0f;
        }
    }
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    renderer_draw_ui_rect(renderer, panel_x - 14.0f, panel_y - 14.0f, panel_width + 28.0f, panel_height + 28.0f, 0.04f, 0.04f, 0.07f, 0.92f);
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 24.0f, "Server Browser", 0.95f, 0.95f, 0.98f, 1.0f);

    char info_line[160];
    snprintf(info_line,
             sizeof(info_line),
             "Master: %s:%u",
             app->master_config.host ? app->master_config.host : APP_MASTER_DEFAULT_HOST,
             app->master_config.port ? app->master_config.port : APP_MASTER_DEFAULT_PORT);
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 64.0f, info_line, 0.78f, 0.78f, 0.84f, 0.95f);

    const char *status = browser->status[0] ? browser->status : "Requesting server list...";
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 92.0f, status, 0.85f, 0.85f, 0.92f, 0.95f);

    if (browser->last_refresh_time > 0.0) {
        double elapsed = app->menu_time - browser->last_refresh_time;
        if (elapsed < 0.0) {
            elapsed = 0.0;
        }
        snprintf(info_line, sizeof(info_line), "Updated %.1f seconds ago", elapsed);
        renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 116.0f, info_line, 0.65f, 0.65f, 0.72f, 0.9f);
    }

    const float table_x = panel_x + 32.0f;
    const float table_y = panel_y + 148.0f;
    const float table_width = panel_width - 64.0f;
    const float row_height = 38.0f;

    float column_widths[4] = {
        table_width * 0.40f,
        table_width * 0.26f,
        table_width * 0.14f,
        table_width * 0.20f,
    };

    const char *column_titles[4] = {"Server", "Address", "Mode", "Players"};

    float header_x = table_x;
    for (int col = 0; col < 4; ++col) {
        float header_width = column_widths[col];
        renderer_draw_ui_rect(renderer, header_x, table_y, header_width, row_height, 0.10f, 0.10f, 0.16f, 0.85f);
        renderer_draw_ui_text(renderer, header_x + 12.0f, table_y + 10.0f, column_titles[col], 0.92f, 0.92f, 0.92f, 1.0f);
        header_x += header_width;
    }

    const float list_area_height = panel_y + panel_height - 200.0f - (table_y + row_height);
    size_t max_rows = list_area_height > 0.0f ? (size_t)(list_area_height / row_height) : (size_t)0;
    if (max_rows == 0) {
        max_rows = 1;
    }

    size_t total_entries = browser->entry_count;
    size_t start_index = 0;
    if (total_entries > max_rows) {
        int selection = browser->selection;
        if (selection < 0) {
            selection = 0;
        }
        if (selection >= (int)total_entries) {
            selection = (int)total_entries - 1;
        }
        if (selection >= 0) {
            if ((size_t)selection >= max_rows) {
                start_index = (size_t)selection + 1 - max_rows;
            }
        }
    }

    float row_y = table_y + row_height;
    float mouse_x = input ? (float)input->mouse_x : -1000.0f;
    float mouse_y = input ? (float)input->mouse_y : -1000.0f;

    bool have_entries = total_entries > 0;

    if (!have_entries) {
        renderer_draw_ui_text(renderer,
                              table_x,
                              row_y + 8.0f,
                              "No servers available.",
                              0.75f,
                              0.75f,
                              0.82f,
                              0.9f);
    } else {
        size_t rows_drawn = 0;
        for (size_t i = start_index; i < total_entries && rows_drawn < max_rows; ++i) {
            const MasterServerEntry *entry = &browser->entries[i];
            bool selected = have_entries && (browser->selection == (int)i);
            bool hovered = point_in_rect(mouse_x, mouse_y, table_x, row_y, table_width, row_height);

            const float base = selected ? 0.24f : (hovered ? 0.18f : 0.12f);
            float row_r = base;
            float row_g = base * 0.9f;
            float row_b = base * 0.8f;
            bool row_pressed = hovered && input && input->mouse_left_down;
            ui_apply_interaction_tint(&row_r, &row_g, &row_b, hovered, row_pressed, app ? app->menu_time : 0.0);
            if (selected) {
                if (row_r < 0.28f) {
                    row_r = 0.28f;
                }
                if (row_g < 0.25f) {
                    row_g = 0.25f;
                }
                if (row_b < 0.22f) {
                    row_b = 0.22f;
                }
            }
            renderer_draw_ui_rect(renderer, table_x, row_y, table_width, row_height, row_r, row_g, row_b, 0.88f);

            float cell_x = table_x + 12.0f;
            renderer_draw_ui_text(renderer,
                                  cell_x,
                                  row_y + 9.0f,
                                  entry->name,
                                  0.96f,
                                  0.96f,
                                  0.96f,
                                  selected ? 1.0f : 0.88f);
            cell_x += column_widths[0];

            char text[128];
            snprintf(text, sizeof(text), "%s:%u", entry->address, entry->port);
            renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, text, 0.92f, 0.92f, 0.96f, 0.9f);
            cell_x += column_widths[1];

            const char *mode_label = app_server_mode_label(entry->mode);
            if (mode_label) {
                renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, mode_label, 0.92f, 0.92f, 0.96f, 0.9f);
            } else {
                snprintf(text, sizeof(text), "Mode %u", (unsigned int)entry->mode);
                renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, text, 0.92f, 0.92f, 0.96f, 0.9f);
            }
            cell_x += column_widths[2];

            snprintf(text, sizeof(text), "%u / %u", (unsigned int)entry->players, (unsigned int)entry->max_players);
            renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, text, 0.92f, 0.92f, 0.96f, 0.9f);

            if (hovered && input && input->mouse_left_pressed) {
                server_browser_set_selection(browser, (int)i);
            }

            row_y += row_height;
            ++rows_drawn;
        }
    }

    float footer_y = panel_y + panel_height - 72.0f;
    float footer_x = panel_x + 32.0f;
    const float footer_button_width = 180.0f;
    const float footer_button_height = 46.0f;

    bool join_clicked = ui_button(renderer,
                                  input,
                                  footer_x,
                                  footer_y,
                                  footer_button_width,
                                  footer_button_height,
                                  "Join Selected",
                                  app ? app->menu_time : 0.0,
                                  1.0f);
    footer_x += footer_button_width + 16.0f;

    bool refresh_clicked = ui_button(renderer,
                                     input,
                                     footer_x,
                                     footer_y,
                                     footer_button_width,
                                     footer_button_height,
                                     "Refresh",
                                     app ? app->menu_time : 0.0,
                                     1.0f);
    footer_x += footer_button_width + 16.0f;

    bool back_clicked = ui_button(renderer,
                                  input,
                                  footer_x,
                                  footer_y,
                                  footer_button_width,
                                  footer_button_height,
                                  "Back",
                                  app ? app->menu_time : 0.0,
                                  1.0f);

    if (refresh_clicked) {
        app_server_browser_request_refresh(app);
        app_server_browser_refresh(app);
    }

    if (back_clicked) {
        app->next_screen = APP_SCREEN_MAIN_MENU;
    }

    if ((join_clicked || (input && input->key_pressed[PLATFORM_KEY_ENTER])) && request_join && server_browser_has_entries(browser)) {
        *request_join = true;
    }

    renderer_end_ui(renderer);
}

static void app_render_menu_background(AppState *app, Renderer *renderer)
{
    if (!app || !renderer) {
        return;
    }

    const float t = (float)app->menu_time;

    float sky_r = 0.06f + 0.04f * sinf(t * 0.31f);
    float sky_g = 0.07f + 0.04f * sinf(t * 0.27f + 1.1f);
    float sky_b = 0.10f + 0.05f * sinf(t * 0.23f + 2.3f);
    renderer_set_clear_color(renderer, sky_r, sky_g, sky_b, 1.0f);

    renderer_begin_scene(renderer, &app->menu_camera);

    renderer_draw_grid(renderer, 24.0f, 1.0f, -0.6f);

    renderer_draw_box(renderer, vec3_make(0.0f, -0.6f, -4.0f), vec3_make(1.6f, 0.12f, 12.0f), vec3_make(0.08f, 0.08f, 0.09f));
    renderer_draw_box(renderer, vec3_make(-2.35f, -0.58f, -4.0f), vec3_make(0.9f, 0.08f, 12.0f), vec3_make(0.14f, 0.14f, 0.16f));
    renderer_draw_box(renderer, vec3_make(2.35f, -0.58f, -4.0f), vec3_make(0.9f, 0.08f, 12.0f), vec3_make(0.14f, 0.14f, 0.16f));

    renderer_draw_box(renderer, vec3_make(0.0f, -0.55f, -1.5f), vec3_make(0.12f, 0.02f, 2.0f), vec3_make(0.85f, 0.65f, 0.12f));
    renderer_draw_box(renderer, vec3_make(0.0f, -0.55f, -7.5f), vec3_make(0.12f, 0.02f, 2.0f), vec3_make(0.85f, 0.65f, 0.12f));

    for (int i = 0; i < 6; ++i) {
        float z = -2.5f - (float)i * 3.4f;
        float height = 2.6f + 0.4f * (float)(i % 3);
        vec3 left_center = vec3_make(-4.2f, height, z);
        vec3 left_half = vec3_make(1.6f, height, 1.6f);
        vec3 left_color = vec3_make(0.18f + 0.02f * (float)(i % 2), 0.22f, 0.26f);
        renderer_draw_box(renderer, left_center, left_half, left_color);

        vec3 right_center = vec3_make(4.2f, height * 0.92f, z - 0.6f);
        vec3 right_half = vec3_make(1.5f, height * 0.92f, 1.8f);
        vec3 right_color = vec3_make(0.20f, 0.24f + 0.02f * (float)(i % 2), 0.28f);
        renderer_draw_box(renderer, right_center, right_half, right_color);
    }

    vec3 entrance_pos = vec3_make(-3.0f, 1.6f, 1.4f);
    vec3 entrance_half = vec3_make(1.4f, 1.6f, 1.8f);
    renderer_draw_box(renderer, entrance_pos, entrance_half, vec3_make(0.18f, 0.22f, 0.27f));

    vec3 doorway_pos = vec3_make(-3.9f, 0.9f, 0.8f);
    vec3 doorway_half = vec3_make(0.3f, 0.9f, 0.9f);
    renderer_draw_box(renderer, doorway_pos, doorway_half, vec3_make(0.05f, 0.06f, 0.07f));

    float sign_pulse = 0.65f + 0.25f * sinf(t * 3.0f);
    vec3 sign_pos = vec3_make(-2.8f, 2.4f, 0.6f);
    vec3 sign_half = vec3_make(0.6f, 0.2f, 1.1f);
    renderer_draw_box(renderer, sign_pos, sign_half, vec3_make(0.10f * sign_pulse, 0.4f * sign_pulse, 0.7f * sign_pulse));

    for (int i = 0; i < 4; ++i) {
        float offset = (float)i * 3.2f;
        vec3 lamp_base = vec3_make(0.9f, -0.2f, -2.0f - offset);
        vec3 lamp_half = vec3_make(0.2f, 0.2f, 0.2f);
        renderer_draw_box(renderer, lamp_base, lamp_half, vec3_make(0.25f, 0.25f, 0.28f));
        vec3 lamp_head = vec3_make(0.9f, 1.4f, -1.9f - offset);
        vec3 lamp_head_half = vec3_make(0.12f, 0.4f, 0.12f);
        float glow = 0.4f + 0.2f * sinf(t * 2.2f + offset * 0.3f);
        renderer_draw_box(renderer, lamp_head, lamp_head_half, vec3_make(0.6f * glow, 0.7f * glow, 0.9f * glow));
    }
}

static void app_draw_fps_overlay(Renderer *renderer,
                                 double fps_value,
                                 const char *line2,
                                 const char *line3)
{
    if (!renderer) {
        return;
    }

    char buffer[128];
    renderer_begin_ui(renderer);

    snprintf(buffer, sizeof(buffer), "FPS: %.1f", fps_value);
    renderer_draw_ui_text(renderer, 16.0f, 20.0f, buffer, 0.95f, 0.95f, 0.95f, 1.0f);
    if (line2 && line2[0]) {
        renderer_draw_ui_text(renderer, 16.0f, 40.0f, line2, 0.85f, 0.85f, 0.85f, 0.95f);
    }
    if (line3 && line3[0]) {
        renderer_draw_ui_text(renderer, 16.0f, 60.0f, line3, 0.85f, 0.85f, 0.85f, 0.95f);
    }

    renderer_end_ui(renderer);
}

static bool app_start_game(GameState **game_ptr,
                           const GameConfig *game_config,
                           Renderer *renderer,
                           PhysicsWorld *physics_world,
                           uint32_t viewport_width,
                           uint32_t viewport_height)
{
    if (!game_ptr || !renderer || !physics_world || !game_config) {
        return false;
    }

    if (*game_ptr) {
        return true;
    }

    GameState *new_game = game_create(game_config, renderer, physics_world);
    if (!new_game) {
        return false;
    }

    game_resize(new_game, viewport_width, viewport_height);
    *game_ptr = new_game;
    return true;
}

static void app_stop_game(GameState **game_ptr)
{
    if (!game_ptr || !*game_ptr) {
        return;
    }

    game_destroy(*game_ptr);
    *game_ptr = NULL;
}
int engine_run(const EngineConfig *config)
{
    if (!config) {
        return -1;
    }

    if (!platform_init()) {
        fprintf(stderr, "[engine] platform_init failed\n");
        return -2;
    }

    ecs_init();
    resources_init("assets");

    preferences_init();
    const EnginePreferences *prefs = preferences_get();

    bool audio_initialized = audio_init();
    bool menu_music_ready = false;
    if (!audio_initialized) {
        fprintf(stderr, "[audio] audio_init failed, menu music disabled\n");
    } else {
        if (!audio_music_set_track(MENU_MUSIC_DEFAULT_PATH)) {
            fprintf(stderr, "[audio] failed to configure menu music track: %s\n", MENU_MUSIC_DEFAULT_PATH);
        } else {
            menu_music_ready = true;
        }
    }

    uint32_t preferred_width = (prefs && prefs->resolution_width) ? prefs->resolution_width
                                                                   : (config->width ? config->width : 1920U);
    uint32_t preferred_height = (prefs && prefs->resolution_height) ? prefs->resolution_height
                                                                     : (config->height ? config->height : 1080U);
    PlatformWindowMode preferred_mode = prefs ? prefs->window_mode : PLATFORM_WINDOW_MODE_FULLSCREEN;

    PlatformWindowDesc desc = {
        .width = preferred_width,
        .height = preferred_height,
        .title = config->title,
        .mode = preferred_mode,
    };

    PlatformWindow *window = platform_create_window(&desc);
    if (!window) {
        fprintf(stderr, "[engine] platform_create_window failed\n");
        if (audio_initialized) {
            audio_shutdown();
        }
        resources_shutdown();
        ecs_shutdown();
        platform_shutdown();
        return -3;
    }

    Renderer *renderer = renderer_create();
    if (!renderer) {
        platform_destroy_window(window);
        if (audio_initialized) {
            audio_shutdown();
        }
        resources_shutdown();
        ecs_shutdown();
        platform_shutdown();
        return -4;
    }

    AppState app = (AppState){0};
    app.master_volume = prefs ? prefs->volume_master : 1.0f;
    app.music_volume = prefs ? prefs->volume_music : 0.7f;
    app.effects_volume = prefs ? prefs->volume_effects : 1.0f;
    app.voice_volume = prefs ? prefs->volume_voice : 1.0f;
    app.microphone_volume = prefs ? prefs->volume_microphone : 1.0f;
    app.audio_output_device = prefs ? prefs->audio_output_device : UINT32_MAX;
    app.audio_input_device = prefs ? prefs->audio_input_device : UINT32_MAX;
    app.voice_activation_mode = prefs ? prefs->voice_activation_mode : PREFERENCES_VOICE_PUSH_TO_TALK;
    app.voice_activation_threshold_db = prefs ? prefs->voice_activation_threshold_db : -45.0f;

    uint32_t viewport_width = preferred_width;
    uint32_t viewport_height = preferred_height;
    platform_window_get_size(window, &viewport_width, &viewport_height);
    if (viewport_width == 0U) {
        viewport_width = preferred_width;
    }
    if (viewport_height == 0U) {
        viewport_height = preferred_height;
    }
    renderer_set_viewport(renderer, viewport_width, viewport_height);

    PlatformWindowMode actual_mode = platform_window_mode(window);
    if (prefs &&
        (prefs->window_mode != actual_mode ||
         prefs->resolution_width != viewport_width ||
         prefs->resolution_height != viewport_height)) {
        preferences_set_graphics(actual_mode, viewport_width, viewport_height);
        preferences_save();
        prefs = preferences_get();
    }
    app.resolution_width = viewport_width;
    app.resolution_height = viewport_height;

    PhysicsWorldDesc physics_desc = {
        .gravity_y = -9.81f,
    };
    PhysicsWorld *physics_world = physics_world_create(&physics_desc);
    if (!physics_world) {
        renderer_destroy(renderer);
        platform_destroy_window(window);
        if (audio_initialized) {
            audio_shutdown();
        }
        resources_shutdown();
        ecs_shutdown();
        platform_shutdown();
        return -5;
    }

    GameConfig game_config = {
        .mouse_sensitivity = 1.0f,
        .move_speed = 5.5f,
        .sprint_multiplier = 1.6f,
        .jump_velocity = 6.2f,
        .gravity = 9.81f,
        .player_height = 1.7f,
        .ground_acceleration = 30.0f,
        .ground_friction = 4.0f,
        .air_control = 6.0f,
        .enable_double_jump = true,
        .double_jump_window = 1.0f,
        .allow_flight = false,
        .enable_view_bobbing = true,
        .view_bobbing_amplitude = 0.035f,
        .view_bobbing_frequency = 9.0f,
    };

    GameState *game = NULL;

    settings_menu_init(&app.settings_menu);
    app.screen = APP_SCREEN_MAIN_MENU;
    app.next_screen = APP_SCREEN_MAIN_MENU;
    app.show_fps_overlay = config->show_fps;
    app.audio_available = menu_music_ready;
    app.music_playing = false;
    app.window = window;
    app.window_mode = prefs ? prefs->window_mode : actual_mode;
    app.resolution_width = prefs ? prefs->resolution_width : viewport_width;
    app.resolution_height = prefs ? prefs->resolution_height : viewport_height;

    snprintf(app.master_server_host, sizeof(app.master_server_host), "%s", APP_MASTER_DEFAULT_HOST);
    app.master_config.host = app.master_server_host;
    app.master_config.port = APP_MASTER_DEFAULT_PORT;

    server_browser_init(&app.browser);
    app_prepare_menu_camera(&app, viewport_width, viewport_height);
    app_set_camera_target(&app, MENU_CAMERA_MAIN_POS, MENU_CAMERA_MAIN_YAW, MENU_CAMERA_MAIN_PITCH, 0.0f);
    app_update_menu_camera(&app, 0.0f);

    audio_set_master_volume(app.master_volume);
    audio_set_effects_volume(app.effects_volume);
    audio_set_voice_volume(app.voice_volume);
    audio_set_microphone_volume(app.microphone_volume);
    audio_select_output_device(app.audio_output_device);
    audio_select_input_device(app.audio_input_device);
    if (app.audio_available) {
        float target_volume = app_music_target_volume(&app);
        if (audio_music_play(target_volume, true)) {
            app.music_playing = true;
        } else {
            app.music_playing = false;
            app.audio_available = false;
        }
    }


    InputState input_state;
    input_reset(&input_state);

    const double target_dt = config->target_fps ? 1.0 / (double)config->target_fps : 0.0;
    double last_time = platform_get_time();
    double stats_timer = 0.0;
    uint32_t stats_frame_counter = 0;
    double displayed_fps = 0.0;

    uint64_t frame_index = 0;
    const uint64_t frame_limit = config->max_frames;

    int exit_code = 0;

    while (!platform_window_should_close(window)) {
        if (app.request_shutdown) {
            platform_window_request_close(window);
            break;
        }

        double frame_start = platform_get_time();
        double dt_seconds = frame_start - last_time;

        if (target_dt > 0.0 && dt_seconds < target_dt) {
            double remaining = target_dt - dt_seconds;
            unsigned int sleep_ms = (unsigned int)(remaining * 1000.0);
            if (sleep_ms > 0U) {
                sleep_milliseconds(sleep_ms);
            }
            do {
                frame_start = platform_get_time();
                dt_seconds = frame_start - last_time;
            } while (dt_seconds < target_dt);
        }

        last_time = frame_start;
        const float dt = (float)dt_seconds;

        platform_begin_frame(window);
        platform_poll_events(window);

        input_update(&input_state, window, dt);

        if (app.show_fps_overlay) {
            stats_timer += dt_seconds;
            ++stats_frame_counter;
        }

        uint32_t new_width = viewport_width;
        uint32_t new_height = viewport_height;
        platform_window_get_size(window, &new_width, &new_height);
        if (new_width == 0U) {
            new_width = 1U;
        }
        if (new_height == 0U) {
            new_height = 1U;
        }
        if (new_width != viewport_width || new_height != viewport_height) {
            viewport_width = new_width;
            viewport_height = new_height;
            renderer_set_viewport(renderer, viewport_width, viewport_height);
            app_prepare_menu_camera(&app, viewport_width, viewport_height);
            if (game) {
                game_resize(game, viewport_width, viewport_height);
            }
        }

        app.menu_time += dt_seconds;
        app_update_menu_camera(&app, dt);
        app.next_screen = app.screen;

        if (app.screen == APP_SCREEN_IN_GAME && game) {
            game_handle_input(game, &input_state, dt);
            game_update(game, dt);

            if (game_should_quit(game)) {
                AppScreen previous_screen = app.screen;
                game_clear_quit_request(game);
                app_stop_game(&game);
                app.screen = APP_SCREEN_MAIN_MENU;
                app.next_screen = APP_SCREEN_MAIN_MENU;
                app_update_music(&app, previous_screen);
            } else {
                game_render(game);

                if (app.show_fps_overlay) {
                    const Camera *cam = game_camera(game);
                    char line2[128];
                    char line3[128];
                    if (cam) {
                        snprintf(line2, sizeof(line2), "Pos: %.2f, %.2f, %.2f", cam->position.x, cam->position.y, cam->position.z);
                        snprintf(line3, sizeof(line3), "Yaw: %.2f  Pitch: %.2f", cam->yaw, cam->pitch);
                    } else {
                        line2[0] = '\0';
                        line3[0] = '\0';
                    }
                    app_draw_fps_overlay(renderer, displayed_fps, cam ? line2 : NULL, cam ? line3 : NULL);
                }
            }
        } else {
            if (game) {
                app_stop_game(&game);
            }

            app_prepare_menu_camera(&app, viewport_width, viewport_height);
            app_render_menu_background(&app, renderer);

            bool start_local_game = false;
            bool join_from_browser = false;

        switch (app.screen) {
        case APP_SCREEN_MAIN_MENU:
            app_render_main_menu(&app, renderer, &input_state, &start_local_game);
            app_update_music(&app, app.screen);
            break;
            case APP_SCREEN_SERVER_BROWSER:
                app_render_server_browser(&app, renderer, &input_state, &join_from_browser);
                break;
            case APP_SCREEN_OPTIONS:
                app_render_options(&app, renderer, &input_state, &viewport_width, &viewport_height);
                break;
            case APP_SCREEN_ABOUT:
                app_render_about(&app, renderer, &input_state);
                break;
            default:
                break;
            }

            if (start_local_game) {
                if (app_start_game(&game, &game_config, renderer, physics_world, viewport_width, viewport_height)) {
                    AppScreen previous_screen = app.screen;
                    app.screen = APP_SCREEN_IN_GAME;
                    app.next_screen = APP_SCREEN_IN_GAME;
                    app_update_music(&app, previous_screen);
                }
            } else if (join_from_browser) {
                const MasterServerEntry *selected_entry = server_browser_selected(&app.browser);
                if (selected_entry) {
                    app.pending_entry = *selected_entry;
                    app.pending_join = true;

                    if (app_start_game(&game, &game_config, renderer, physics_world, viewport_width, viewport_height)) {
                        bool connected = false;
                        if (game) {
                            connected = game_connect_to_master_entry(game, &app.pending_entry);
                        }

                        if (connected) {
                            AppScreen previous_screen = app.screen;
                            app.screen = APP_SCREEN_IN_GAME;
                            app.next_screen = APP_SCREEN_IN_GAME;
                            app_update_music(&app, previous_screen);
                        } else {
                            if (game) {
                                app_stop_game(&game);
                            }
                            AppScreen previous_screen = app.screen;
                            app.screen = APP_SCREEN_SERVER_BROWSER;
                            app.next_screen = APP_SCREEN_SERVER_BROWSER;
                            app_update_music(&app, previous_screen);
                        }
                    }

                    app.pending_join = false;
                }
            }

            if (app.show_fps_overlay) {
                app_draw_fps_overlay(renderer, displayed_fps, NULL, NULL);
            }
        }

        if (app.show_fps_overlay && stats_timer >= 0.5) {
            double fps = stats_timer > 0.0 ? (double)stats_frame_counter / stats_timer : 0.0;
            displayed_fps = fps;
            stats_timer = 0.0;
            stats_frame_counter = 0;
        }

        renderer_draw_frame(renderer);
        platform_swap_buffers(window);

        if (app.next_screen != app.screen) {
            AppScreen previous_screen = app.screen;
            app.screen = app.next_screen;

            app_update_music(&app, previous_screen);

            if (previous_screen == APP_SCREEN_IN_GAME && app.screen != APP_SCREEN_IN_GAME) {
                EnginePreferences *prefs_sync = preferences_data();
                if (prefs_sync) {
                    app.master_volume = prefs_sync->volume_master;
                    app.music_volume = prefs_sync->volume_music;
                    app.effects_volume = prefs_sync->volume_effects;
                    app.voice_volume = prefs_sync->volume_voice;
                    app.microphone_volume = prefs_sync->volume_microphone;
                    app.audio_output_device = prefs_sync->audio_output_device;
                    app.audio_input_device = prefs_sync->audio_input_device;
                    app.voice_activation_mode = prefs_sync->voice_activation_mode;
                    app.voice_activation_threshold_db = prefs_sync->voice_activation_threshold_db;
                    audio_set_master_volume(app.master_volume);
                    audio_set_effects_volume(app.effects_volume);
                    audio_set_voice_volume(app.voice_volume);
                    audio_set_microphone_volume(app.microphone_volume);
                    audio_select_output_device(app.audio_output_device);
                    audio_select_input_device(app.audio_input_device);
                    audio_music_set_volume(app_music_target_volume(&app));
                }
            }

            if (previous_screen == APP_SCREEN_SERVER_BROWSER && app.screen != APP_SCREEN_SERVER_BROWSER) {
                server_browser_close(&app.browser);
                app.server_browser_pending_refresh = false;
            }

            if (app.screen == APP_SCREEN_SERVER_BROWSER && previous_screen != APP_SCREEN_SERVER_BROWSER) {
                server_browser_open(&app.browser, &app.master_config, app.menu_time);
                app.server_browser_pending_refresh = false;
            }

            if (app.screen != APP_SCREEN_IN_GAME) {
                app_move_camera_to_screen(&app, app.screen);
            }
        }

        ++frame_index;
        if (frame_limit && frame_index >= frame_limit) {
            break;
        }
    }

    preferences_set_graphics(app.window_mode, app.resolution_width, app.resolution_height);
    preferences_capture_bindings();
    preferences_save();

    app_stop_game(&game);
    physics_world_destroy(physics_world);
    renderer_destroy(renderer);
    platform_destroy_window(window);
    if (audio_initialized) {
        audio_shutdown();
    }
    resources_shutdown();
    ecs_shutdown();
    platform_shutdown();

    printf("\n");

    return exit_code;
}
























