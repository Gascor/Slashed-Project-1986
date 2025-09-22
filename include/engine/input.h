#pragma once

#include <stdbool.h>

#include "engine/platform.h"

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
    int mouse_x;
    int mouse_y;
    bool mouse_left_down;
    bool mouse_left_pressed;
    bool mouse_left_released;
    bool mouse_right_down;
    bool mouse_right_pressed;
    bool mouse_right_released;
} InputState;

void input_reset(InputState *state);
void input_update(InputState *state, const PlatformWindow *window, float dt);


