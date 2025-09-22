#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct PlatformWindow PlatformWindow;

typedef struct PlatformWindowDesc {
    uint32_t width;
    uint32_t height;
    const char *title;
} PlatformWindowDesc;

typedef enum PlatformKey {
    PLATFORM_KEY_UNKNOWN = 0,
    PLATFORM_KEY_ESCAPE,
    PLATFORM_KEY_SPACE,
    PLATFORM_KEY_ENTER,
    PLATFORM_KEY_TAB,
    PLATFORM_KEY_SHIFT,
    PLATFORM_KEY_CTRL,
    PLATFORM_KEY_ALT,
    PLATFORM_KEY_UP,
    PLATFORM_KEY_DOWN,
    PLATFORM_KEY_LEFT,
    PLATFORM_KEY_RIGHT,
    PLATFORM_KEY_W,
    PLATFORM_KEY_A,
    PLATFORM_KEY_S,
    PLATFORM_KEY_D,
    PLATFORM_KEY_Q,
    PLATFORM_KEY_E,
    PLATFORM_KEY_R,
    PLATFORM_KEY_F,
    PLATFORM_KEY_1,
    PLATFORM_KEY_2,
    PLATFORM_KEY_3,
    PLATFORM_KEY_COUNT
} PlatformKey;

typedef enum PlatformMouseButton {
    PLATFORM_MOUSE_BUTTON_LEFT = 0,
    PLATFORM_MOUSE_BUTTON_RIGHT,
    PLATFORM_MOUSE_BUTTON_MIDDLE,
    PLATFORM_MOUSE_BUTTON_COUNT
} PlatformMouseButton;

bool platform_init(void);
void platform_shutdown(void);

PlatformWindow *platform_create_window(const PlatformWindowDesc *desc);
void platform_destroy_window(PlatformWindow *window);

void platform_begin_frame(PlatformWindow *window);
void platform_poll_events(PlatformWindow *window);
bool platform_window_should_close(const PlatformWindow *window);
void platform_swap_buffers(PlatformWindow *window);
void platform_window_request_close(PlatformWindow *window);

void platform_window_get_size(const PlatformWindow *window, uint32_t *out_width, uint32_t *out_height);

bool platform_key_down(const PlatformWindow *window, PlatformKey key);
bool platform_key_pressed(const PlatformWindow *window, PlatformKey key);
bool platform_key_released(const PlatformWindow *window, PlatformKey key);

bool platform_mouse_button_down(const PlatformWindow *window, PlatformMouseButton button);
bool platform_mouse_button_pressed(const PlatformWindow *window, PlatformMouseButton button);
bool platform_mouse_button_released(const PlatformWindow *window, PlatformMouseButton button);
void platform_mouse_position(const PlatformWindow *window, int *out_x, int *out_y);
void platform_mouse_delta(const PlatformWindow *window, int *out_dx, int *out_dy);
float platform_mouse_wheel_delta(const PlatformWindow *window);

double platform_get_time(void);
