#include "engine/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <hidusage.h>
#include <GL/gl.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    bool mouse_initialized;
    bool raw_input_enabled;
} PlatformInputState;

typedef struct PlatformWindow {
    HWND hwnd;
    HDC hdc;
    HGLRC glrc;
    uint32_t width;
    uint32_t height;
    bool should_close;
    PlatformInputState input;
} PlatformWindow;

static HINSTANCE g_instance = NULL;
static ATOM g_window_class = 0;
static double g_qpc_frequency = 0.0;
static PlatformWindow *g_active_window = NULL;

static PlatformKey platform_translate_key(WPARAM wparam)
{
    switch (wparam) {
    case VK_ESCAPE:
        return PLATFORM_KEY_ESCAPE;
    case VK_SPACE:
        return PLATFORM_KEY_SPACE;
    case VK_RETURN:
        return PLATFORM_KEY_ENTER;
    case VK_TAB:
        return PLATFORM_KEY_TAB;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return PLATFORM_KEY_SHIFT;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return PLATFORM_KEY_CTRL;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return PLATFORM_KEY_ALT;
    case VK_UP:
        return PLATFORM_KEY_UP;
    case VK_DOWN:
        return PLATFORM_KEY_DOWN;
    case VK_LEFT:
        return PLATFORM_KEY_LEFT;
    case VK_RIGHT:
        return PLATFORM_KEY_RIGHT;
    case 'W':
        return PLATFORM_KEY_W;
    case 'A':
        return PLATFORM_KEY_A;
    case 'S':
        return PLATFORM_KEY_S;
    case 'D':
        return PLATFORM_KEY_D;
    case 'Q':
        return PLATFORM_KEY_Q;
    case 'E':
        return PLATFORM_KEY_E;
    case 'R':
        return PLATFORM_KEY_R;
    case 'F':
        return PLATFORM_KEY_F;
    case '1':
        return PLATFORM_KEY_1;
    case '2':
        return PLATFORM_KEY_2;
    case '3':
        return PLATFORM_KEY_3;
    default:
        break;
    }

    return PLATFORM_KEY_UNKNOWN;
}

static void platform_handle_key_event(PlatformWindow *window, WPARAM wparam, LPARAM lparam, bool down)
{
    if (!window) {
        return;
    }

    PlatformKey key = platform_translate_key(wparam);
    if (key == PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return;
    }

    bool was_down = window->input.key_down[key];
    bool is_repeat = ((lparam >> 30) & 1) != 0;

    if (down) {
        if (!was_down) {
            window->input.key_pressed[key] = true;
        } else if (!is_repeat) {
            window->input.key_pressed[key] = false;
        }
        window->input.key_down[key] = true;
    } else {
        if (was_down) {
            window->input.key_released[key] = true;
        }
        window->input.key_down[key] = false;
    }
}

static void platform_handle_mouse_button(PlatformWindow *window, PlatformMouseButton button, bool down)
{
    if (!window || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return;
    }

    bool was_down = window->input.mouse_down[button];
    if (down) {
        if (!was_down) {
            window->input.mouse_pressed[button] = true;
        }
        window->input.mouse_down[button] = true;
    } else {
        if (was_down) {
            window->input.mouse_released[button] = true;
        }
        window->input.mouse_down[button] = false;
    }
}

static LRESULT CALLBACK platform_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    PlatformWindow *window = (PlatformWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCT *create_params = (CREATESTRUCT *)lparam;
        PlatformWindow *created_window = (PlatformWindow *)create_params->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)created_window);
        return TRUE;
    }
    case WM_CLOSE:
        if (window) {
            window->should_close = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (window) {
            window->width = (uint32_t)LOWORD(lparam);
            window->height = (uint32_t)HIWORD(lparam);
            if (window->glrc) {
                glViewport(0, 0, (GLsizei)window->width, (GLsizei)window->height);
            }
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        platform_handle_key_event(window, wparam, lparam, true);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        platform_handle_key_event(window, wparam, lparam, false);
        return 0;
    case WM_MOUSEMOVE:
        if (window) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (window->input.mouse_initialized) {
                if (!window->input.raw_input_enabled) {
                    window->input.mouse_dx += x - window->input.mouse_x;
                    window->input.mouse_dy += y - window->input.mouse_y;
                }
            } else {
                window->input.mouse_initialized = true;
            }
            window->input.mouse_x = x;
            window->input.mouse_y = y;
        }
        return 0;
    case WM_LBUTTONDOWN:
        platform_handle_mouse_button(window, PLATFORM_MOUSE_BUTTON_LEFT, true);
        return 0;
    case WM_LBUTTONUP:
        platform_handle_mouse_button(window, PLATFORM_MOUSE_BUTTON_LEFT, false);
        return 0;
    case WM_RBUTTONDOWN:
        platform_handle_mouse_button(window, PLATFORM_MOUSE_BUTTON_RIGHT, true);
        return 0;
    case WM_RBUTTONUP:
        platform_handle_mouse_button(window, PLATFORM_MOUSE_BUTTON_RIGHT, false);
        return 0;
    case WM_MBUTTONDOWN:
        platform_handle_mouse_button(window, PLATFORM_MOUSE_BUTTON_MIDDLE, true);
        return 0;
    case WM_MBUTTONUP:
        platform_handle_mouse_button(window, PLATFORM_MOUSE_BUTTON_MIDDLE, false);
        return 0;
    case WM_MOUSEWHEEL:
        if (window) {
            const SHORT delta = GET_WHEEL_DELTA_WPARAM(wparam);
            window->input.mouse_wheel += (float)delta / (float)WHEEL_DELTA;
        }
        return 0;
    case WM_INPUT:
        if (window) {
            UINT size = 0;
            if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) == 0 && size > 0) {
                RAWINPUT *raw = (RAWINPUT *)malloc(size);
                if (raw) {
                    if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == size) {
                        if (raw->header.dwType == RIM_TYPEMOUSE) {
                            window->input.mouse_dx += raw->data.mouse.lLastX;
                            window->input.mouse_dy += raw->data.mouse.lLastY;
                            window->input.raw_input_enabled = true;
                        }
                    }
                    free(raw);
                }
            }
        }
        return 0;
    default:
        break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

bool platform_init(void)
{
    if (g_instance) {
        return true;
    }

    g_instance = GetModuleHandle(NULL);
    if (!g_instance) {
        return false;
    }

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        BOOL(WINAPI *set_process_dpi_aware)(void) = (BOOL(WINAPI *)(void))GetProcAddress(user32, "SetProcessDPIAware");
        if (set_process_dpi_aware) {
            set_process_dpi_aware();
        }
    }

    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq)) {
        return false;
    }
    g_qpc_frequency = (double)freq.QuadPart;

    WNDCLASSEX window_class = {0};
    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = platform_window_proc;
    window_class.hInstance = g_instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.lpszClassName = "SP1986Window";

    g_window_class = RegisterClassEx(&window_class);
    if (!g_window_class) {
        return false;
    }

    return true;
}

void platform_shutdown(void)
{
    if (g_window_class) {
        UnregisterClass(MAKEINTATOM(g_window_class), g_instance);
        g_window_class = 0;
    }
    g_instance = NULL;
    g_active_window = NULL;
    g_qpc_frequency = 0.0;
}

static bool platform_setup_opengl(PlatformWindow *window)
{
    const PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,
        32,
        0, 0, 0, 0, 0, 0,
        0,
        0,
        0,
        0, 0, 0, 0,
        24,
        8,
        0,
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };

    int pixel_format = ChoosePixelFormat(window->hdc, &pfd);
    if (pixel_format == 0) {
        return false;
    }

    if (!SetPixelFormat(window->hdc, pixel_format, &pfd)) {
        return false;
    }

    HGLRC glrc = wglCreateContext(window->hdc);
    if (!glrc) {
        return false;
    }

    if (!wglMakeCurrent(window->hdc, glrc)) {
        wglDeleteContext(glrc);
        return false;
    }

    window->glrc = glrc;
    return true;
}

PlatformWindow *platform_create_window(const PlatformWindowDesc *desc)
{
    if (!desc || !g_window_class) {
        return NULL;
    }

    PlatformWindow *window = (PlatformWindow *)calloc(1, sizeof(PlatformWindow));
    if (!window) {
        return NULL;
    }

    window->width = desc->width;
    window->height = desc->height;
    memset(&window->input, 0, sizeof(window->input));

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect = {0, 0, (LONG)desc->width, (LONG)desc->height};
    AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        MAKEINTATOM(g_window_class),
        desc->title ? desc->title : "Slashed Project 1986",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        g_instance,
        window);

    if (!hwnd) {
        free(window);
        return NULL;
    }

    window->hwnd = hwnd;
    window->hdc = GetDC(hwnd);
    if (!window->hdc) {
        DestroyWindow(hwnd);
        free(window);
        return NULL;
    }

    if (!platform_setup_opengl(window)) {
        ReleaseDC(hwnd, window->hdc);
        DestroyWindow(hwnd);
        free(window);
        return NULL;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    RAWINPUTDEVICE rid = {
        .usUsagePage = HID_USAGE_PAGE_GENERIC,
        .usUsage = HID_USAGE_GENERIC_MOUSE,
        .dwFlags = RIDEV_INPUTSINK,
        .hwndTarget = hwnd,
    };
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        /* If registration fails we fall back to WM_MOUSEMOVE deltas. */
    }

    g_active_window = window;

    return window;
}

void platform_destroy_window(PlatformWindow *window)
{
    if (!window) {
        return;
    }

    if (window->glrc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(window->glrc);
        window->glrc = NULL;
    }

    if (window->hwnd && window->hdc) {
        ReleaseDC(window->hwnd, window->hdc);
    }

    if (window->hwnd) {
        DestroyWindow(window->hwnd);
    }

    if (g_active_window == window) {
        g_active_window = NULL;
    }

    free(window);
}

void platform_poll_events(PlatformWindow *window)
{
    (void)window;

    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            if (g_active_window) {
                g_active_window->should_close = true;
            }
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool platform_window_should_close(const PlatformWindow *window)
{
    return window ? window->should_close : true;
}

void platform_swap_buffers(PlatformWindow *window)
{
    if (!window || !window->hdc) {
        return;
    }

    SwapBuffers(window->hdc);
}

void platform_window_request_close(PlatformWindow *window)
{
    if (!window) {
        return;
    }

    window->should_close = true;
    if (window->hwnd) {
        PostMessage(window->hwnd, WM_CLOSE, 0, 0);
    }
}

void platform_window_get_size(const PlatformWindow *window, uint32_t *out_width, uint32_t *out_height)
{
    if (out_width) {
        *out_width = window ? window->width : 0U;
    }
    if (out_height) {
        *out_height = window ? window->height : 0U;
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

static const PlatformInputState *platform_input_state(const PlatformWindow *window)
{
    return window ? &window->input : NULL;
}

bool platform_key_down(const PlatformWindow *window, PlatformKey key)
{
    const PlatformInputState *input = platform_input_state(window);
    if (!input || key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return false;
    }
    return input->key_down[key];
}

bool platform_key_pressed(const PlatformWindow *window, PlatformKey key)
{
    const PlatformInputState *input = platform_input_state(window);
    if (!input || key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return false;
    }
    return input->key_pressed[key];
}

bool platform_key_released(const PlatformWindow *window, PlatformKey key)
{
    const PlatformInputState *input = platform_input_state(window);
    if (!input || key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        return false;
    }
    return input->key_released[key];
}

bool platform_mouse_button_down(const PlatformWindow *window, PlatformMouseButton button)
{
    const PlatformInputState *input = platform_input_state(window);
    if (!input || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return false;
    }
    return input->mouse_down[button];
}

bool platform_mouse_button_pressed(const PlatformWindow *window, PlatformMouseButton button)
{
    const PlatformInputState *input = platform_input_state(window);
    if (!input || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return false;
    }
    return input->mouse_pressed[button];
}

bool platform_mouse_button_released(const PlatformWindow *window, PlatformMouseButton button)
{
    const PlatformInputState *input = platform_input_state(window);
    if (!input || button < 0 || button >= PLATFORM_MOUSE_BUTTON_COUNT) {
        return false;
    }
    return input->mouse_released[button];
}

void platform_mouse_position(const PlatformWindow *window, int *out_x, int *out_y)
{
    const PlatformInputState *input = platform_input_state(window);
    if (out_x) {
        *out_x = input ? input->mouse_x : 0;
    }
    if (out_y) {
        *out_y = input ? input->mouse_y : 0;
    }
}

void platform_mouse_delta(const PlatformWindow *window, int *out_dx, int *out_dy)
{
    const PlatformInputState *input = platform_input_state(window);
    if (out_dx) {
        *out_dx = input ? input->mouse_dx : 0;
    }
    if (out_dy) {
        *out_dy = input ? input->mouse_dy : 0;
    }
}

float platform_mouse_wheel_delta(const PlatformWindow *window)
{
    const PlatformInputState *input = platform_input_state(window);
    return input ? input->mouse_wheel : 0.0f;
}

double platform_get_time(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    if (g_qpc_frequency <= 0.0) {
        return 0.0;
    }

    return (double)counter.QuadPart / g_qpc_frequency;
}


