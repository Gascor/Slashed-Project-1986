#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct PlatformWindow PlatformWindow;

typedef enum PlatformWindowMode {
    PLATFORM_WINDOW_MODE_FULLSCREEN = 0,
    PLATFORM_WINDOW_MODE_WINDOWED,
    PLATFORM_WINDOW_MODE_BORDERLESS,
    PLATFORM_WINDOW_MODE_COUNT
} PlatformWindowMode;

typedef struct PlatformWindowDesc {
    uint32_t width;
    uint32_t height;
    const char *title;
    PlatformWindowMode mode;
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
    PLATFORM_KEY_A,
    PLATFORM_KEY_B,
    PLATFORM_KEY_C,
    PLATFORM_KEY_D,
    PLATFORM_KEY_E,
    PLATFORM_KEY_F,
    PLATFORM_KEY_G,
    PLATFORM_KEY_H,
    PLATFORM_KEY_I,
    PLATFORM_KEY_J,
    PLATFORM_KEY_K,
    PLATFORM_KEY_L,
    PLATFORM_KEY_M,
    PLATFORM_KEY_N,
    PLATFORM_KEY_O,
    PLATFORM_KEY_P,
    PLATFORM_KEY_Q,
    PLATFORM_KEY_R,
    PLATFORM_KEY_S,
    PLATFORM_KEY_T,
    PLATFORM_KEY_U,
    PLATFORM_KEY_V,
    PLATFORM_KEY_W,
    PLATFORM_KEY_X,
    PLATFORM_KEY_Y,
    PLATFORM_KEY_Z,
    PLATFORM_KEY_0,
    PLATFORM_KEY_1,
    PLATFORM_KEY_2,
    PLATFORM_KEY_3,
    PLATFORM_KEY_4,
    PLATFORM_KEY_5,
    PLATFORM_KEY_6,
    PLATFORM_KEY_7,
    PLATFORM_KEY_8,
    PLATFORM_KEY_9,
    PLATFORM_KEY_F1,
    PLATFORM_KEY_F2,
    PLATFORM_KEY_F3,
    PLATFORM_KEY_F4,
    PLATFORM_KEY_F5,
    PLATFORM_KEY_F6,
    PLATFORM_KEY_F7,
    PLATFORM_KEY_F8,
    PLATFORM_KEY_F9,
    PLATFORM_KEY_F10,
    PLATFORM_KEY_F11,
    PLATFORM_KEY_F12,
    PLATFORM_KEY_GRAVE,
    PLATFORM_KEY_MINUS,
    PLATFORM_KEY_EQUALS,
    PLATFORM_KEY_LEFT_BRACKET,
    PLATFORM_KEY_RIGHT_BRACKET,
    PLATFORM_KEY_BACKSLASH,
    PLATFORM_KEY_SEMICOLON,
    PLATFORM_KEY_APOSTROPHE,
    PLATFORM_KEY_COMMA,
    PLATFORM_KEY_PERIOD,
    PLATFORM_KEY_SLASH,
    PLATFORM_KEY_BACKSPACE,
    PLATFORM_KEY_DELETE,
    PLATFORM_KEY_HOME,
    PLATFORM_KEY_END,
    PLATFORM_KEY_PAGE_UP,
    PLATFORM_KEY_PAGE_DOWN,
    PLATFORM_KEY_INSERT,
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
PlatformWindowMode platform_window_mode(const PlatformWindow *window);
bool platform_window_set_mode(PlatformWindow *window,
                              PlatformWindowMode mode,
                              uint32_t width,
                              uint32_t height);
bool platform_window_resize(PlatformWindow *window, uint32_t width, uint32_t height);

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
