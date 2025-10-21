#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "engine/platform.h"

typedef enum InputAction {
    INPUT_ACTION_MOVE_FORWARD = 0,
    INPUT_ACTION_MOVE_BACKWARD,
    INPUT_ACTION_MOVE_LEFT,
    INPUT_ACTION_MOVE_RIGHT,
    INPUT_ACTION_JUMP,
    INPUT_ACTION_CROUCH,
    INPUT_ACTION_SPRINT,
    INPUT_ACTION_RELOAD,
    INPUT_ACTION_INTERACT,
    INPUT_ACTION_MENU,
    INPUT_ACTION_DROP_WEAPON,
    INPUT_ACTION_PUSH_TO_TALK,
    INPUT_ACTION_COUNT
} InputAction;

typedef struct InputState {
    float move_forward;
    float move_right;
    float move_vertical;
    float look_delta_x;
    float look_delta_y;
    float mouse_wheel;
    bool jump_pressed;
    bool sprinting;
    bool escape_pressed;
    bool fire_pressed;
    bool fire_down;
    bool reload_pressed;
    bool interact_pressed;
    bool drop_pressed;
    bool drop_down;
    bool voice_talk_pressed;
    bool voice_talk_down;
    int mouse_x;
    int mouse_y;
    bool mouse_left_down;
    bool mouse_left_pressed;
    bool mouse_left_released;
    bool mouse_right_down;
    bool mouse_right_pressed;
    bool mouse_right_released;
    bool key_down[PLATFORM_KEY_COUNT];
    bool key_pressed[PLATFORM_KEY_COUNT];
    PlatformKey last_pressed_key;
} InputState;

void input_reset(InputState *state);
void input_update(InputState *state, const PlatformWindow *window, float dt);

void input_bindings_reset_defaults(void);
size_t input_action_count(void);
InputAction input_action_by_index(size_t index);
const char *input_action_display_name(InputAction action);
const char *input_action_token(InputAction action);
InputAction input_action_from_token(const char *token);
PlatformKey input_action_default_key(InputAction action);
PlatformKey input_binding_get(InputAction action);
bool input_binding_set(InputAction action, PlatformKey key);
void input_bindings_export(PlatformKey out_bindings[INPUT_ACTION_COUNT]);
bool input_bindings_import(const PlatformKey bindings[INPUT_ACTION_COUNT]);
const char *input_key_display_name(PlatformKey key);
const char *input_key_token(PlatformKey key);
PlatformKey input_key_from_token(const char *token);
PlatformKey input_first_pressed_key(const InputState *state);
