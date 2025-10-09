#pragma once

#include <stdbool.h>

typedef enum AppScreen {
    APP_SCREEN_MAIN_MENU = 0,
    APP_SCREEN_SERVER_BROWSER,
    APP_SCREEN_OPTIONS,
    APP_SCREEN_ABOUT,
    APP_SCREEN_IN_GAME
} AppScreen;

typedef struct AppState AppState;
typedef struct InputState InputState;
typedef struct Renderer Renderer;

// Menu system functions
AppState *menu_create(void);
void menu_destroy(AppState *app);
void menu_handle_input(AppState *app, const InputState *input, float dt);
void menu_update(AppState *app, float dt);
void menu_render(AppState *app, Renderer *renderer);
void menu_set_screen(AppState *app, AppScreen screen);
AppScreen menu_get_current_screen(const AppState *app);
bool menu_should_shutdown(const AppState *app);