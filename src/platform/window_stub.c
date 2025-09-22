#include "engine/platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct PlatformInputState {
    bool key_down[PLATFORM_KEY_COUNT];
    bool key_pressed[PLATFORM_KEY_COUNT];
    bool key_released[PLATFORM_KEY_COUNT];
    bool mouse_down[PLATFORM_MOUSE_BUTTON_COUNT];
    bool mouse_pressed[PLATFORM_MOUSE_BUTTON_COUNT];
    bool mouse_released[PLATFORM_MOUSE_BUTTON_COUNT];
    int mouse_x;
    int mouse_y;
    int mouse_dx;
    int mouse_dy;
    float mouse_wheel;
    bool raw_input_enabled;
} PlatformInputState;

struct PlatformWindow {
    PlatformWindowDesc desc;
    bool should_close;
    PlatformInputState input;
};

bool platform_init(void)
{
    return true;
}

void platform_shutdown(void)
{
}

PlatformWindow *platform_create_window(const PlatformWindowDesc *desc)
{
    if (!desc) {
        return NULL;
    }

    PlatformWindow *window = (PlatformWindow *)malloc(sizeof(PlatformWindow));
    if (!window) {
        return NULL;
    }

    window->desc = *desc;
    window->should_close = false;
    memset(&window->input, 0, sizeof(window->input));
    return window;
}

void platform_destroy_window(PlatformWindow *window)
{
    free(window);
}

void platform_poll_events(PlatformWindow *window)
{
    (void)window;
}

bool platform_window_should_close(const PlatformWindow *window)
{
    return window ? window->should_close : true;
}

void platform_swap_buffers(PlatformWindow *window)
{
    (void)window;
}

void platform_window_request_close(PlatformWindow *window)
{
    if (window) {
        window->should_close = true;
    }
}

void platform_window_get_size(const PlatformWindow *window, uint32_t *out_width, uint32_t *out_height)
{
    if (out_width) {
        *out_width = window ? window->desc.width : 0U;
    }
    if (out_height) {
        *out_height = window ? window->desc.height : 0U;
    }
}

void platform_begin_frame(PlatformWindow *window)
{
    if (!window) {
        return;
    }

    memset(window->input.key_pressed, 0, sizeof(window->input.key_pressed));
    memset(window->input.key_released, 0, sizeof(window->input.key_released));
    memset(window->input.mouse_pressed, 0, sizeof(window->input.mouse_pressed));
    memset(window->input.mouse_released, 0, sizeof(window->input.mouse_released));
    window->input.mouse_dx = 0;
    window->input.mouse_dy = 0;
    window->input.mouse_wheel = 0.0f;
}

bool platform_key_down(const PlatformWindow *window, PlatformKey key)
{
    if (!window || key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return false;
    }

    return window->input.key_down[key];
}

bool platform_key_pressed(const PlatformWindow *window, PlatformKey key)
{
    if (!window || key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return false;
    }

    return window->input.key_pressed[key];
}

bool platform_key_released(const PlatformWindow *window, PlatformKey key)
{
    if (!window || key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return false;
    }

    return window->input.key_released[key];
}

bool platform_mouse_button_down(const PlatformWindow *window, PlatformMouseButton button)
{
    if (!window || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return false;
    }

    return window->input.mouse_down[button];
}

bool platform_mouse_button_pressed(const PlatformWindow *window, PlatformMouseButton button)
{
    if (!window || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return false;
    }

    return window->input.mouse_pressed[button];
}

bool platform_mouse_button_released(const PlatformWindow *window, PlatformMouseButton button)
{
    if (!window || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return false;
    }

    return window->input.mouse_released[button];
}

void platform_mouse_position(const PlatformWindow *window, int *out_x, int *out_y)
{
    if (out_x) {
        *out_x = window ? window->input.mouse_x : 0;
    }
    if (out_y) {
        *out_y = window ? window->input.mouse_y : 0;
    }
}

void platform_mouse_delta(const PlatformWindow *window, int *out_dx, int *out_dy)
{
    if (out_dx) {
        *out_dx = window ? window->input.mouse_dx : 0;
    }
    if (out_dy) {
        *out_dy = window ? window->input.mouse_dy : 0;
    }
}

float platform_mouse_wheel_delta(const PlatformWindow *window)
{
    return window ? window->input.mouse_wheel : 0.0f;
}

double platform_get_time(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}





