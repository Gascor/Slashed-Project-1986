#include "engine/input.h"

#include <string.h>

static float clamp_axis(float value)
{
    if (value > 1.0f) {
        return 1.0f;
    }
    if (value < -1.0f) {
        return -1.0f;
    }
    return value;
}

void input_reset(InputState *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->mouse_x = 0;
    state->mouse_y = 0;
}

void input_update(InputState *state, const PlatformWindow *window, float dt)
{
    (void)dt;

    if (!state) {
        return;
    }

    float forward = 0.0f;
    float right = 0.0f;
    float vertical = 0.0f;

    if (window) {
        if (platform_key_down(window, PLATFORM_KEY_W)) {
            forward += 1.0f;
        }
        if (platform_key_down(window, PLATFORM_KEY_S)) {
            forward -= 1.0f;
        }
        if (platform_key_down(window, PLATFORM_KEY_D)) {
            right += 1.0f;
        }
        if (platform_key_down(window, PLATFORM_KEY_A)) {
            right -= 1.0f;
        }
        if (platform_key_down(window, PLATFORM_KEY_SPACE)) {
            vertical += 1.0f;
        }
        if (platform_key_down(window, PLATFORM_KEY_CTRL)) {
            vertical -= 1.0f;
        }

        int mouse_dx = 0;
        int mouse_dy = 0;
        platform_mouse_delta(window, &mouse_dx, &mouse_dy);

        const float sensitivity = 0.0025f; /* radians per pixel */
        state->look_delta_x = (float)mouse_dx * sensitivity;
        state->look_delta_y = (float)mouse_dy * sensitivity;

        state->mouse_wheel = platform_mouse_wheel_delta(window);
        state->jump_pressed = platform_key_pressed(window, PLATFORM_KEY_SPACE);
        state->sprinting = platform_key_down(window, PLATFORM_KEY_SHIFT);
        state->escape_pressed = platform_key_pressed(window, PLATFORM_KEY_ESCAPE);
        state->fire_pressed = platform_mouse_button_pressed(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->fire_down = platform_mouse_button_down(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->reload_pressed = platform_key_pressed(window, PLATFORM_KEY_R);
        state->interact_pressed = platform_key_pressed(window, PLATFORM_KEY_F);
        state->mouse_left_down = platform_mouse_button_down(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->mouse_left_pressed = platform_mouse_button_pressed(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->mouse_left_released = platform_mouse_button_released(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->mouse_right_down = platform_mouse_button_down(window, PLATFORM_MOUSE_BUTTON_RIGHT);
        state->mouse_right_pressed = platform_mouse_button_pressed(window, PLATFORM_MOUSE_BUTTON_RIGHT);
        state->mouse_right_released = platform_mouse_button_released(window, PLATFORM_MOUSE_BUTTON_RIGHT);
        platform_mouse_position(window, &state->mouse_x, &state->mouse_y);
    } else {
        state->look_delta_x = 0.0f;
        state->look_delta_y = 0.0f;
        state->mouse_wheel = 0.0f;
        state->jump_pressed = false;
        state->sprinting = false;
        state->escape_pressed = false;
        state->fire_pressed = false;
        state->fire_down = false;
        state->reload_pressed = false;
        state->interact_pressed = false;
        state->mouse_left_down = false;
        state->mouse_left_pressed = false;
        state->mouse_left_released = false;
        state->mouse_right_down = false;
        state->mouse_right_pressed = false;
        state->mouse_right_released = false;
        state->mouse_x = 0;
        state->mouse_y = 0;
    }

    state->move_forward = clamp_axis(forward);
    state->move_right = clamp_axis(right);
    state->move_vertical = clamp_axis(vertical);
}



