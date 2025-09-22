#include "engine/core.h"
#include "engine/platform.h"
#include "engine/renderer.h"
#include "engine/input.h"
#include "engine/game.h"
#include "engine/physics.h"
#include "engine/ecs.h"
#include "engine/resources.h"
#include "engine/math.h"
#include "engine/network.h"
#include "engine/network_server.h"
#include "engine/master_protocol.h"\r\n#include "engine/network_master.h"

#define APP_MASTER_DEFAULT_HOST "127.0.0.1"
#define APP_MASTER_DEFAULT_PORT 27050
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

    MasterServerEntry pending_entry;
    bool pending_join;


    ServerBrowserState browser;

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

static void sleep_milliseconds(unsigned int ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}
static const vec3 MENU_CAMERA_MAIN_POS = {0.0f, 4.6f, 9.2f};
static const float MENU_CAMERA_MAIN_YAW = (float)M_PI;
static const float MENU_CAMERA_MAIN_PITCH = -0.35f;

static const vec3 MENU_CAMERA_OPTIONS_POS = {2.4f, 4.1f, 8.6f};
static const float MENU_CAMERA_OPTIONS_YAW = (float)(M_PI * 0.82f);
static const float MENU_CAMERA_OPTIONS_PITCH = -0.30f;

static const vec3 MENU_CAMERA_SERVER_POS = {-3.2f, 2.4f, 2.9f};
static const float MENU_CAMERA_SERVER_YAW = (float)(-M_PI * 0.36f);
static const float MENU_CAMERA_SERVER_PITCH = -0.18f;

static vec3 vec3_lerp(vec3 a, vec3 b, float t)
{
    vec3 result;
    result.x = a.x + (b.x - a.x) * t;
    result.y = a.y + (b.y - a.y) * t;
    result.z = a.z + (b.z - a.z) * t;
    return result;
}

static float wrap_angle(float angle)
{
    while (angle > (float)M_PI) {
        angle -= (float)(2.0 * M_PI);
    }
    while (angle < (float)-M_PI) {
        angle += (float)(2.0 * M_PI);
    }
    return angle;
}

static void app_set_camera_target(AppState *app, vec3 target_pos, float target_yaw, float target_pitch, float duration)
{
    if (!app) {
        return;
    }

    if (!app->menu_camera_ready || duration <= 0.0f) {
        app->camera_target_pos = target_pos;
        app->camera_start_pos = target_pos;
        app->camera_target_yaw = target_yaw;
        app->camera_start_yaw = target_yaw;
        app->camera_target_pitch = target_pitch;
        app->camera_start_pitch = target_pitch;
        app->camera_anim_time = 0.0f;
        app->camera_anim_duration = 0.0f;
        app->camera_animating = false;
        if (app->menu_camera_ready) {
            app->menu_camera.position = target_pos;
            app->menu_camera.yaw = target_yaw;
            app->menu_camera.pitch = target_pitch;
        }
        return;
    }

    app->camera_start_pos = app->menu_camera.position;
    app->camera_start_yaw = app->menu_camera.yaw;
    app->camera_start_pitch = app->menu_camera.pitch;
    app->camera_target_pos = target_pos;
    app->camera_target_yaw = target_yaw;
    app->camera_target_pitch = target_pitch;
    app->camera_anim_time = 0.0f;
    app->camera_anim_duration = duration;
    app->camera_animating = true;
}

static void app_move_camera_to_screen(AppState *app, AppScreen screen)
{
    if (!app) {
        return;
    }

    const float to_server_time = 2.4f;
    const float to_menu_time = 2.0f;

    switch (screen) {
    case APP_SCREEN_SERVER_BROWSER:
        app_set_camera_target(app, MENU_CAMERA_SERVER_POS, MENU_CAMERA_SERVER_YAW, MENU_CAMERA_SERVER_PITCH, to_server_time);
        break;
    case APP_SCREEN_OPTIONS:
    case APP_SCREEN_ABOUT:
        app_set_camera_target(app, MENU_CAMERA_OPTIONS_POS, MENU_CAMERA_OPTIONS_YAW, MENU_CAMERA_OPTIONS_PITCH, to_menu_time);
        break;
    case APP_SCREEN_MAIN_MENU:
    default:
        app_set_camera_target(app, MENU_CAMERA_MAIN_POS, MENU_CAMERA_MAIN_YAW, MENU_CAMERA_MAIN_PITCH, to_menu_time);
        break;
    }
}

static void app_update_menu_camera(AppState *app, float dt)
{
    if (!app || !app->menu_camera_ready) {
        return;
    }

    if (app->camera_animating) {
        app->camera_anim_time += dt;
        float duration = app->camera_anim_duration > 0.0001f ? app->camera_anim_duration : 0.0001f;
        float t = app->camera_anim_time / duration;
        if (t >= 1.0f) {
            app->camera_animating = false;
            app->menu_camera.position = app->camera_target_pos;
            app->menu_camera.yaw = app->camera_target_yaw;
            app->menu_camera.pitch = app->camera_target_pitch;
        } else {
            if (t < 0.0f) {
                t = 0.0f;
            }
            float smooth = t * t * (3.0f - 2.0f * t);
            vec3 pos = vec3_lerp(app->camera_start_pos, app->camera_target_pos, smooth);
            float yaw_delta = wrap_angle(app->camera_target_yaw - app->camera_start_yaw);
            float pitch_delta = app->camera_target_pitch - app->camera_start_pitch;
            float yaw = wrap_angle(app->camera_start_yaw + yaw_delta * smooth);
            float pitch = app->camera_start_pitch + pitch_delta * smooth;

            app->menu_camera.position = pos;
            app->menu_camera.yaw = yaw;
            app->menu_camera.pitch = pitch;
        }
    } else {
        vec3 pos = app->camera_target_pos;
        pos.y += 0.08f * sinf((float)app->menu_time * 0.8f);
        pos.x += 0.04f * sinf((float)app->menu_time * 0.6f);
        app->menu_camera.position = pos;
        app->menu_camera.yaw = wrap_angle(app->camera_target_yaw + 0.012f * sinf((float)app->menu_time * 0.5f));
        app->menu_camera.pitch = app->camera_target_pitch + 0.008f * sinf((float)app->menu_time * 0.7f);
    }
}

static void app_prepare_menu_camera(AppState *app, uint32_t width, uint32_t height)
{
    if (!app) {
        return;
    }

    const float aspect = (height != 0U) ? ((float)width / (float)height) : (16.0f / 9.0f);

    if (!app->menu_camera_ready) {
        vec3 initial_pos = app->camera_target_pos;
        float initial_yaw = app->camera_target_yaw;
        float initial_pitch = app->camera_target_pitch;
        app->menu_camera = camera_create(initial_pos, initial_yaw, initial_pitch, (float)M_PI / 3.0f, aspect, 0.1f, 50.0f);
        app->menu_camera_ready = true;
        app->menu_camera.position = initial_pos;
        app->menu_camera.yaw = initial_yaw;
        app->menu_camera.pitch = initial_pitch;
    } else {
        camera_set_aspect(&app->menu_camera, aspect);
    }
}
static bool point_in_rect(float px, float py, float rx, float ry, float rw, float rh)
{
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

static bool mode_filter_any_enabled(const ServerBrowserState *browser)
{
    if (!browser) {
        return false;
    }
    for (int i = 0; i < SERVER_MODE_COUNT; ++i) {
        if (browser->mode_filter[i]) {
            return true;
        }
    }
    return false;
}

static const ServerBrowserState *g_sort_browser = NULL;
static ServerSortColumn g_sort_column = SERVER_SORT_PING;
static bool g_sort_descending = false;

static int server_compare_indices(const void *lhs, const void *rhs)
{
    if (!g_sort_browser) {
        return 0;
    }

    int a_index = *(const int *)lhs;
    int b_index = *(const int *)rhs;

    const ServerEntry *a = &g_sort_browser->entries[a_index];
    const ServerEntry *b = &g_sort_browser->entries[b_index];

    int result = 0;
    switch (g_sort_column) {
    case SERVER_SORT_NAME:
        result = _stricmp(a->master.name, b->master.name);
        break;
    case SERVER_SORT_MODE:
        result = (int)a->mode - (int)b->mode;
        break;
    case SERVER_SORT_PING:
        result = a->ping_ms - b->ping_ms;
        break;
    case SERVER_SORT_PLAYERS:
        result = (a->players - b->players);
        if (result == 0) {
            result = a->max_players - b->max_players;
        }
        break;
    default:
        break;
    }

    if (!g_sort_descending) {
        return result;
    }
    return -result;
}
static ServerMode server_mode_from_master(uint8_t mode)
{
    switch (mode) {
    case 0:
        return SERVER_MODE_CTF;
    case 1:
        return SERVER_MODE_TDM;
    case 2:
        return SERVER_MODE_SLENDER;
    default:
        return (ServerMode)(mode % SERVER_MODE_COUNT);
    }
}

static bool server_browser_fetch_master(ServerBrowserState *browser)
{
    if (!browser) {
        return false;
    }

    MasterServerEntry master_entries[32];
    size_t count = 0;
    MasterClientConfig config = {
        .host = APP_MASTER_DEFAULT_HOST,
        .port = APP_MASTER_DEFAULT_PORT,
    };

    bool success = network_fetch_master_list(&config, master_entries, 32, &count);
    if (count > 32) {
        count = 32;
    }

    browser->entry_count = count;
    for (size_t i = 0; i < count; ++i) {
        ServerEntry *dst = &browser->entries[i];
        MasterServerEntry src = master_entries[i];
        src.name[MASTER_SERVER_NAME_MAX - 1] = 0;
        src.address[MASTER_SERVER_ADDR_MAX - 1] = 0;
        dst->master = src;
        dst->mode = server_mode_from_master(src.mode);
        dst->players = (int)src.players;
        dst->max_players = src.max_players > 0 ? (int)src.max_players : (int)src.players;
        if (dst->max_players < dst->players) {
            dst->players = dst->max_players;
        }
        dst->ping_ms = 40 + (rand() % 90);
        dst->official = false;
        dst->password = false;
    }

    for (size_t i = count; i < 32; ++i) {
        memset(&browser->entries[i], 0, sizeof(ServerEntry));
    }

    browser->needs_refresh = false;
    return success;
}

static void server_browser_refresh(ServerBrowserState *browser)
{
    if (!browser) {
        return;
    }

    bool any_filter = mode_filter_any_enabled(browser);

    browser->visible_count = 0;
    for (size_t i = 0; i < browser->entry_count && browser->visible_count < 32; ++i) {
        const ServerEntry *entry = &browser->entries[i];
        if (!any_filter || browser->mode_filter[entry->mode]) {
            browser->visible_indices[browser->visible_count++] = (int)i;
        }
    }

    g_sort_browser = browser;
    g_sort_column = browser->sort_column;
    g_sort_descending = browser->sort_descending;
    qsort(browser->visible_indices, browser->visible_count, sizeof(int), server_compare_indices);

    if (browser->selected_entry >= 0) {
        bool still_visible = false;
        for (size_t i = 0; i < browser->visible_count; ++i) {
            if (browser->visible_indices[i] == browser->selected_entry) {
                still_visible = true;
                break;
            }
        }
        if (!still_visible) {
            browser->selected_entry = browser->visible_count > 0 ? browser->visible_indices[0] : -1;
        }
    }
    if (browser->selected_entry < 0 && browser->visible_count > 0) {
        browser->selected_entry = browser->visible_indices[0];
    }
    if (browser->visible_count == 0) {
        browser->selected_entry = -1;
    }
}

static void server_browser_toggle_sort(ServerBrowserState *browser, ServerSortColumn column)
{
    if (!browser) {
        return;
    }

    if (browser->sort_column == column) {
        browser->sort_descending = !browser->sort_descending;
    } else {
        browser->sort_column = column;
        browser->sort_descending = (column == SERVER_SORT_PING) || (column == SERVER_SORT_PLAYERS);
    }
    server_browser_refresh(browser);
}

static void server_browser_init(ServerBrowserState *browser)
{
    if (!browser) {
        return;
    }

    memset(browser, 0, sizeof(*browser));
    browser->master_config.host = APP_MASTER_DEFAULT_HOST;
    browser->master_config.port = APP_MASTER_DEFAULT_PORT;
    srand((unsigned int)time(NULL));

    for (int i = 0; i < SERVER_MODE_COUNT; ++i) {
        browser->mode_filter[i] = true;
    }

    browser->sort_column = SERVER_SORT_PING;
    browser->sort_descending = false;
    browser->selected_entry = -1;
    browser->hover_entry = -1;
    browser->needs_refresh = false;

    server_browser_fetch_master(browser);
    server_browser_refresh(browser);
}


static bool ui_button(Renderer *renderer,
                      const InputState *input,
                      float x,
                      float y,
                      float width,
                      float height,
                      const char *label,
                      float alpha)
{
    if (!renderer || !label) {
        return false;
    }

    float px = input ? (float)input->mouse_x : -1000.0f;
    float py = input ? (float)input->mouse_y : -1000.0f;
    bool hovered = input && point_in_rect(px, py, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    const float base = hovered ? 0.22f : 0.12f;
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.9f, base * 0.8f, 0.9f * alpha);
    renderer_draw_ui_text(renderer, x + 24.0f, y + height * 0.5f - 8.0f, label, 0.95f, 0.95f, 0.95f, 1.0f * alpha);
    return pressed;
}

static bool ui_toggle(Renderer *renderer,
                      const InputState *input,
                      float x,
                      float y,
                      float width,
                      float height,
                      const char *label,
                      bool *value,
                      float alpha)
{
    if (!renderer || !label || !value) {
        return false;
    }

    float px = input ? (float)input->mouse_x : -1000.0f;
    float py = input ? (float)input->mouse_y : -1000.0f;
    bool hovered = input && point_in_rect(px, py, x, y, width, height);
    bool toggled = hovered && input && input->mouse_left_pressed;

    const float base = *value ? 0.26f : 0.10f;
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.85f, base * 0.7f, 0.9f * alpha);

    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s: %s", label, *value ? "ON" : "OFF");
    renderer_draw_ui_text(renderer, x + 18.0f, y + height * 0.5f - 8.0f, buffer, 0.95f, 0.95f, 0.95f, 1.0f * alpha);

    if (toggled) {
        *value = !*value;
        return true;
    }
    return false;
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

    const float panel_width = 420.0f;
    const float panel_height = 380.0f;
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    renderer_draw_ui_rect(renderer, panel_x - 12.0f, panel_y - 12.0f, panel_width + 24.0f, panel_height + 24.0f, 0.04f, 0.04f, 0.07f, 0.85f);
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 32.0f, "SLASHED PROJECT 1986", 0.95f, 0.95f, 0.98f, 1.0f);

    float button_y = panel_y + 90.0f;
    const float button_height = 52.0f;
    const float button_width = panel_width - 64.0f;
    const float button_x = panel_x + 32.0f;

    if (ui_button(renderer, input, button_x, button_y, button_width, button_height, "Create A Match", 1.0f)) {
        if (start_local_game) {
            *start_local_game = true;
        }
    }
    button_y += button_height + 10.0f;

    if (ui_button(renderer, input, button_x, button_y, button_width, button_height, "Join A Server", 1.0f)) {
        app->next_screen = APP_SCREEN_SERVER_BROWSER;
    }
    button_y += button_height + 10.0f;

    if (ui_button(renderer, input, button_x, button_y, button_width, button_height, "Settings", 1.0f)) {
        app->next_screen = APP_SCREEN_OPTIONS;
    }
    button_y += button_height + 10.0f;

    if (ui_button(renderer, input, button_x, button_y, button_width, button_height, "About", 1.0f)) {
        app->next_screen = APP_SCREEN_ABOUT;
    }
    button_y += button_height + 10.0f;

    if (ui_button(renderer, input, button_x, button_y, button_width, button_height, "Quit", 1.0f)) {
        app->request_shutdown = true;
    }

    renderer_end_ui(renderer);
}

static void app_render_options(AppState *app,
                               Renderer *renderer,
                               const InputState *input)
{
    if (!app || !renderer) {
        return;
    }

    renderer_begin_ui(renderer);

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    const float panel_width = 520.0f;
    const float panel_height = 420.0f;
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    renderer_draw_ui_rect(renderer, panel_x - 16.0f, panel_y - 16.0f, panel_width + 32.0f, panel_height + 32.0f, 0.04f, 0.04f, 0.07f, 0.9f);
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 32.0f, "Settings", 0.95f, 0.95f, 0.98f, 1.0f);

    float option_y = panel_y + 92.0f;
    const float option_width = panel_width - 64.0f;
    const float option_height = 50.0f;
    const float option_x = panel_x + 32.0f;

    if (ui_toggle(renderer, input, option_x, option_y, option_width, option_height, "FPS overlay", &app->show_fps_overlay, 1.0f)) {
        /* updated */
    }
    option_y += option_height + 12.0f;

    renderer_draw_ui_rect(renderer, option_x, option_y, option_width, option_height, 0.10f, 0.10f, 0.14f, 0.6f);
    renderer_draw_ui_text(renderer,
                          option_x + 18.0f,
                          option_y + option_height * 0.5f - 8.0f,
                          "Flight mode toggles are restricted to administrator console commands.",
                          0.85f,
                          0.85f,
                          0.85f,
                          0.8f);
    option_y += option_height + 12.0f;

    renderer_draw_ui_rect(renderer, option_x, option_y, option_width, option_height, 0.10f, 0.10f, 0.14f, 0.6f);
    renderer_draw_ui_text(renderer,
                          option_x + 18.0f,
                          option_y + option_height * 0.5f - 8.0f,
                          "Audio, control and accessibility settings will land in future updates.",
                          0.85f,
                          0.85f,
                          0.85f,
                          0.8f);
    option_y += option_height + 24.0f;

    if (ui_button(renderer, input, option_x, option_y, option_width, 48.0f, "Back", 1.0f)) {
        app->next_screen = APP_SCREEN_MAIN_MENU;
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

    if (ui_button(renderer, input, panel_x + 32.0f, panel_y + panel_height - 64.0f, panel_width - 64.0f, 48.0f, "Back", 1.0f)) {
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

    if (browser->needs_refresh) {
        server_browser_fetch_master(browser);
        server_browser_refresh(browser);
        browser->needs_refresh = false;
    }

    renderer_begin_ui(renderer);

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    const float panel_width = width - 120.0f;
    const float panel_height = height - 140.0f;
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    renderer_draw_ui_rect(renderer, panel_x - 14.0f, panel_y - 14.0f, panel_width + 28.0f, panel_height + 28.0f, 0.04f, 0.04f, 0.07f, 0.92f);
    renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 24.0f, "Server Browser", 0.95f, 0.95f, 0.98f, 1.0f);

    float filter_x = panel_x + 32.0f;
    float filter_y = panel_y + 72.0f;
    const float filter_width = 160.0f;
    const float filter_height = 40.0f;

    for (int mode = 0; mode < SERVER_MODE_COUNT; ++mode) {
        bool changed = ui_toggle(renderer,
                                 input,
                                 filter_x,
                                 filter_y,
                                 filter_width,
                                 filter_height,
                                 server_mode_name((ServerMode)mode),
                                 &browser->mode_filter[mode],
                                 1.0f);
        if (changed) {
            server_browser_refresh(browser);
        }
        filter_x += filter_width + 12.0f;
    }

    if (!mode_filter_any_enabled(browser)) {
        for (int mode = 0; mode < SERVER_MODE_COUNT; ++mode) {
            browser->mode_filter[mode] = true;
        }
        server_browser_refresh(browser);
    }

    const float table_x = panel_x + 32.0f;
    const float table_y = panel_y + 128.0f;
    const float table_width = panel_width - 64.0f;
    const float row_height = 38.0f;

    float column_widths[4] = {
        table_width * 0.45f,
        table_width * 0.20f,
        table_width * 0.15f,
        table_width * 0.20f,
    };

    const char *column_titles[4] = {"Server", "Mode", "Ping", "Players"};

    float header_x = table_x;
    for (int col = 0; col < 4; ++col) {
        float header_width = column_widths[col];
        renderer_draw_ui_rect(renderer, header_x, table_y, header_width, row_height, 0.10f, 0.10f, 0.16f, 0.85f);

        char header_label[64];
        if ((int)browser->sort_column == col) {
            snprintf(header_label, sizeof(header_label), "%s %s", column_titles[col], browser->sort_descending ? "v" : "^");
        } else {
            snprintf(header_label, sizeof(header_label), "%s", column_titles[col]);
        }
        renderer_draw_ui_text(renderer, header_x + 12.0f, table_y + 10.0f, header_label, 0.92f, 0.92f, 0.92f, 1.0f);

        bool header_clicked = input && input->mouse_left_pressed && point_in_rect((float)input->mouse_x, (float)input->mouse_y, header_x, table_y, header_width, row_height);
        if (header_clicked) {
            server_browser_toggle_sort(browser, (ServerSortColumn)col);
        }

        header_x += header_width;
    }

    const size_t max_rows = (size_t)((panel_y + panel_height - 160.0f - (table_y + row_height)) / row_height);
    float row_y = table_y + row_height;
    float mouse_x = input ? (float)input->mouse_x : -1000.0f;
    float mouse_y = input ? (float)input->mouse_y : -1000.0f;

    browser->hover_entry = -1;
    size_t rows_drawn = 0;
    for (size_t i = 0; i < browser->visible_count && rows_drawn < max_rows; ++i) {
        int entry_index = browser->visible_indices[i];
        const ServerEntry *entry = &browser->entries[entry_index];
        bool selected = (entry_index == browser->selected_entry);
        bool hovered = point_in_rect(mouse_x, mouse_y, table_x, row_y, table_width, row_height);
        if (hovered) {
            browser->hover_entry = entry_index;
        }

        const float base = selected ? 0.22f : (hovered ? 0.18f : 0.12f);
        renderer_draw_ui_rect(renderer, table_x, row_y, table_width, row_height, base, base * 0.9f, base * 0.8f, 0.85f);

        char text[128];
        float cell_x = table_x + 12.0f;
        snprintf(text, sizeof(text), "%s%s", entry->master.name, entry->password ? " [lock]" : "");
        renderer_draw_ui_text(renderer, cell_x, row_y + 9.0f, text, 0.96f, 0.96f, 0.96f, selected ? 1.0f : 0.88f);
        cell_x += column_widths[0];

        renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, server_mode_name(entry->mode), 0.92f, 0.92f, 0.96f, 0.9f);
        cell_x += column_widths[1];

        snprintf(text, sizeof(text), "%d", entry->ping_ms);
        renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, text, 0.92f, 0.92f, 0.96f, 0.9f);
        cell_x += column_widths[2];

        snprintf(text, sizeof(text), "%d / %d", entry->players, entry->max_players);
        renderer_draw_ui_text(renderer, cell_x + 12.0f, row_y + 9.0f, text, 0.92f, 0.92f, 0.96f, 0.9f);

        if (hovered && input && input->mouse_left_pressed) {
            browser->selected_entry = entry_index;
        }

        row_y += row_height;
        ++rows_drawn;
    }

    float footer_y = panel_y + panel_height - 72.0f;
    float footer_x = panel_x + 32.0f;
    float footer_button_width = 180.0f;
    float footer_button_height = 46.0f;

    bool join_clicked = ui_button(renderer, input, footer_x, footer_y, footer_button_width, footer_button_height, "Join Selected", 1.0f);
    footer_x += footer_button_width + 16.0f;
    bool refresh_clicked = ui_button(renderer, input, footer_x, footer_y, footer_button_width, footer_button_height, "Refresh", 1.0f);
    footer_x += footer_button_width + 16.0f;
    bool back_clicked = ui_button(renderer, input, footer_x, footer_y, footer_button_width, footer_button_height, "Back", 1.0f);

    if (refresh_clicked) {
        server_browser_fetch_master(browser);
        server_browser_refresh(browser);
    }

    if (back_clicked) {
        browser->needs_refresh = true;
        app->next_screen = APP_SCREEN_MAIN_MENU;
    }

    if (join_clicked && request_join && browser->selected_entry >= 0) {
        app->pending_entry = browser->entries[browser->selected_entry].master;
        app->pending_join = true;
        *request_join = true;
    }
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

    PlatformWindowDesc desc = {
        .width = config->width,
        .height = config->height,
        .title = config->title,
    };

    PlatformWindow *window = platform_create_window(&desc);
    if (!window) {
        fprintf(stderr, "[engine] platform_create_window failed\n");
        resources_shutdown();
        ecs_shutdown();
        platform_shutdown();
        return -3;
    }

    Renderer *renderer = renderer_create();
    if (!renderer) {
        platform_destroy_window(window);
        resources_shutdown();
        ecs_shutdown();
        platform_shutdown();
        return -4;
    }

    uint32_t viewport_width = config->width ? config->width : 1280U;
    uint32_t viewport_height = config->height ? config->height : 720U;
    platform_window_get_size(window, &viewport_width, &viewport_height);
    if (viewport_width == 0U) {
        viewport_width = config->width ? config->width : 1280U;
    }
    if (viewport_height == 0U) {
        viewport_height = config->height ? config->height : 720U;
    }
    renderer_set_viewport(renderer, viewport_width, viewport_height);

    PhysicsWorldDesc physics_desc = {
        .gravity_y = -9.81f,
    };
    PhysicsWorld *physics_world = physics_world_create(&physics_desc);
    if (!physics_world) {
        renderer_destroy(renderer);
        platform_destroy_window(window);
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

    AppState app = {0};
    app.screen = APP_SCREEN_MAIN_MENU;
    app.next_screen = APP_SCREEN_MAIN_MENU;
    app.show_fps_overlay = config->show_fps;
    server_browser_init(&app.browser);
    app_set_camera_target(&app, MENU_CAMERA_MAIN_POS, MENU_CAMERA_MAIN_YAW, MENU_CAMERA_MAIN_PITCH, 0.0f);
    app_prepare_menu_camera(&app, viewport_width, viewport_height);
    app_update_menu_camera(&app, 0.0f);


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
                game_clear_quit_request(game);
                app_stop_game(&game);
                app.screen = APP_SCREEN_MAIN_MENU;
                app.next_screen = APP_SCREEN_MAIN_MENU;
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
                break;
            case APP_SCREEN_SERVER_BROWSER:
                app_render_server_browser(&app, renderer, &input_state, &join_from_browser);
                break;
            case APP_SCREEN_OPTIONS:
                app_render_options(&app, renderer, &input_state);
                break;
            case APP_SCREEN_ABOUT:
                app_render_about(&app, renderer, &input_state);
                break;
            default:
                break;
            }

            if (start_local_game) {
                if (app_start_game(&game, &game_config, renderer, physics_world, viewport_width, viewport_height)) {
                    app.screen = APP_SCREEN_IN_GAME;
                    app.next_screen = APP_SCREEN_IN_GAME;
                }
            } else if (join_from_browser && app.browser.selected_entry >= 0) {
                app.pending_entry = app.browser.entries[app.browser.selected_entry].master;
                app.pending_join = true;

                if (app_start_game(&game, &game_config, renderer, physics_world, viewport_width, viewport_height)) {
                    bool connected = false;
                    if (game) {
                        connected = game_connect_to_master_entry(game, &app.pending_entry);
                    }

                    if (connected) {
                        app.screen = APP_SCREEN_IN_GAME;
                        app.next_screen = APP_SCREEN_IN_GAME;
                    } else {
                        if (game) {
                            app_stop_game(&game);
                        }
                        app.screen = APP_SCREEN_SERVER_BROWSER;
                        app.next_screen = APP_SCREEN_SERVER_BROWSER;
                    }
                }

                app.pending_join = false;
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
            app.screen = app.next_screen;
            if (app.screen != APP_SCREEN_IN_GAME) {
                app_move_camera_to_screen(&app, app.screen);
            }
        }

        ++frame_index;
        if (frame_limit && frame_index >= frame_limit) {
            break;
        }
    }

    app_stop_game(&game);
    physics_world_destroy(physics_world);
    renderer_destroy(renderer);
    platform_destroy_window(window);
    resources_shutdown();
    ecs_shutdown();
    platform_shutdown();

    printf("\n");

    return exit_code;
}

















