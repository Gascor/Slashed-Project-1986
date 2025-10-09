#include "engine/menu.h"
#include "engine/core.h"
#include "engine/input.h"
#include "engine/renderer.h"
#include "engine/camera.h"
#include "engine/master_protocol.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

typedef struct AppState {
    AppScreen screen;
    AppScreen next_screen;
    bool show_fps_overlay;
    bool request_shutdown;
    
    MasterServerEntry pending_entry;
    bool pending_join;
    
    Camera menu_camera;
    bool menu_camera_ready;
    
    // Menu navigation
    int main_menu_selection;
    int options_selection;
    
    // Options
    float master_volume;
    float mouse_sensitivity;
    bool fullscreen;
} AppState;

AppState *menu_create(void)
{
    AppState *app = malloc(sizeof(AppState));
    if (!app) return NULL;
    
    memset(app, 0, sizeof(*app));
    app->screen = APP_SCREEN_MAIN_MENU;
    app->next_screen = APP_SCREEN_MAIN_MENU;
    app->show_fps_overlay = false;
    app->request_shutdown = false;
    app->pending_join = false;
    app->menu_camera_ready = false;
    
    // Default options
    app->master_volume = 1.0f;
    app->mouse_sensitivity = 1.0f;
    app->fullscreen = false;
    
    // Initialize menu camera
    vec3 position = {0.0f, 2.0f, 5.0f};
    app->menu_camera = camera_create(position, 0.0f, 0.0f, 
                                   CAMERA_DEFAULT_FOV_DEG * M_PI / 180.0f, 
                                   16.0f / 9.0f, 
                                   CAMERA_DEFAULT_NEAR, 
                                   CAMERA_DEFAULT_FAR);
    app->menu_camera_ready = true;
    
    return app;
}

void menu_destroy(AppState *app)
{
    if (app) {
        free(app);
    }
}

void menu_handle_input(AppState *app, const InputState *input, float dt)
{
    (void)dt; // Suppress unused parameter warning
    
    switch (app->screen) {
        case APP_SCREEN_MAIN_MENU:
            // For now, use simple mouse/keyboard navigation
            // TODO: Implement proper menu navigation with the available InputState
            if (input->escape_pressed) {
                app->request_shutdown = true;
            }
            break;
            
        case APP_SCREEN_SERVER_BROWSER:
            if (input->escape_pressed) {
                app->next_screen = APP_SCREEN_MAIN_MENU;
            }
            // TODO: Handle server browser input
            break;
            
        case APP_SCREEN_OPTIONS:
            if (input->escape_pressed) {
                app->next_screen = APP_SCREEN_MAIN_MENU;
            }
            // TODO: Handle options input
            break;
            
        case APP_SCREEN_ABOUT:
            if (input->escape_pressed) {
                app->next_screen = APP_SCREEN_MAIN_MENU;
            }
            break;
            
        case APP_SCREEN_IN_GAME:
            // Game input is handled elsewhere
            break;
    }
}

void menu_update(AppState *app, float dt)
{
    // Update screen transition
    if (app->next_screen != app->screen) {
        app->screen = app->next_screen;
    }
    
    // Update menu camera (simple rotation)
    if (app->menu_camera_ready && app->screen != APP_SCREEN_IN_GAME) {
        float rotation_speed = 0.1f;
        vec3 pos = app->menu_camera.position;
        float angle = atan2f(pos.z, pos.x) + rotation_speed * dt;
        float radius = sqrtf(pos.x * pos.x + pos.z * pos.z);
        
        pos.x = cosf(angle) * radius;
        pos.z = sinf(angle) * radius;
        
        app->menu_camera.position = pos;
        // Update camera yaw to look at center
        app->menu_camera.yaw = angle + M_PI;
    }
}

void menu_render(AppState *app, Renderer *renderer)
{
    // This would contain the actual menu rendering code
    (void)app;
    (void)renderer;
    
    // TODO: Implement menu rendering
    // - Main menu
    // - Server browser
    // - Options screen
    // - About screen
}

void menu_set_screen(AppState *app, AppScreen screen)
{
    app->next_screen = screen;
}

AppScreen menu_get_current_screen(const AppState *app)
{
    return app->screen;
}

bool menu_should_shutdown(const AppState *app)
{
    return app->request_shutdown;
}