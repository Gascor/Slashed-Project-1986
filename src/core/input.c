#include "engine/input.h"

#include <ctype.h>
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

typedef struct InputActionInfo {
    InputAction action;
    const char *name;
    const char *token;
    PlatformKey default_key;
} InputActionInfo;

static const InputActionInfo g_action_info[INPUT_ACTION_COUNT] = {
    {INPUT_ACTION_MOVE_FORWARD,  "Move Forward",        "move_forward",  PLATFORM_KEY_W},
    {INPUT_ACTION_MOVE_BACKWARD, "Move Backward",       "move_backward", PLATFORM_KEY_S},
    {INPUT_ACTION_MOVE_LEFT,     "Move Left",           "move_left",     PLATFORM_KEY_A},
    {INPUT_ACTION_MOVE_RIGHT,    "Move Right",          "move_right",    PLATFORM_KEY_D},
    {INPUT_ACTION_JUMP,          "Jump / Ascend",       "jump",          PLATFORM_KEY_SPACE},
    {INPUT_ACTION_CROUCH,        "Crouch / Descend",    "crouch",        PLATFORM_KEY_CTRL},
    {INPUT_ACTION_SPRINT,        "Sprint",              "sprint",        PLATFORM_KEY_SHIFT},
    {INPUT_ACTION_RELOAD,        "Reload",              "reload",        PLATFORM_KEY_R},
    {INPUT_ACTION_INTERACT,      "Interact",            "interact",      PLATFORM_KEY_F},
    {INPUT_ACTION_MENU,          "Pause / Menu",        "menu",          PLATFORM_KEY_ESCAPE},
    {INPUT_ACTION_DROP_WEAPON,   "Drop Weapon",         "drop_weapon",   PLATFORM_KEY_C},
};

static PlatformKey g_action_bindings[INPUT_ACTION_COUNT];
static bool g_bindings_initialized = false;

typedef struct InputKeyName {
    PlatformKey key;
    const char *display;
    const char *token;
} InputKeyName;

static const InputKeyName g_special_key_names[] = {
    {PLATFORM_KEY_UNKNOWN,      "Unassigned",  "unassigned"},
    {PLATFORM_KEY_ESCAPE,       "Escape",      "escape"},
    {PLATFORM_KEY_SPACE,        "Space",       "space"},
    {PLATFORM_KEY_ENTER,        "Enter",       "enter"},
    {PLATFORM_KEY_TAB,          "Tab",         "tab"},
    {PLATFORM_KEY_SHIFT,        "Shift",       "shift"},
    {PLATFORM_KEY_CTRL,         "Ctrl",        "ctrl"},
    {PLATFORM_KEY_ALT,          "Alt",         "alt"},
    {PLATFORM_KEY_UP,           "Arrow Up",    "arrow_up"},
    {PLATFORM_KEY_DOWN,         "Arrow Down",  "arrow_down"},
    {PLATFORM_KEY_LEFT,         "Arrow Left",  "arrow_left"},
    {PLATFORM_KEY_RIGHT,        "Arrow Right", "arrow_right"},
    {PLATFORM_KEY_GRAVE,        "`",           "grave"},
    {PLATFORM_KEY_MINUS,        "-",           "minus"},
    {PLATFORM_KEY_EQUALS,       "=",           "equals"},
    {PLATFORM_KEY_LEFT_BRACKET, "[",           "lbracket"},
    {PLATFORM_KEY_RIGHT_BRACKET,"]",           "rbracket"},
    {PLATFORM_KEY_BACKSLASH,    "\\",          "backslash"},
    {PLATFORM_KEY_SEMICOLON,    ";",           "semicolon"},
    {PLATFORM_KEY_APOSTROPHE,   "'",           "apostrophe"},
    {PLATFORM_KEY_COMMA,        ",",           "comma"},
    {PLATFORM_KEY_PERIOD,       ".",           "period"},
    {PLATFORM_KEY_SLASH,        "/",           "slash"},
    {PLATFORM_KEY_BACKSPACE,    "Backspace",   "backspace"},
    {PLATFORM_KEY_DELETE,       "Delete",      "delete"},
    {PLATFORM_KEY_HOME,         "Home",        "home"},
    {PLATFORM_KEY_END,          "End",         "end"},
    {PLATFORM_KEY_PAGE_UP,      "Page Up",     "page_up"},
    {PLATFORM_KEY_PAGE_DOWN,    "Page Down",   "page_down"},
    {PLATFORM_KEY_INSERT,       "Insert",      "insert"},
};

static const char *const g_key_letters[] = {
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
};

static const char *const g_key_digits[] = {"0","1","2","3","4","5","6","7","8","9"};

static const char *const g_key_function_keys[] = {
    "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"
};

static int input_stricmp(const char *a, const char *b)
{
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }

    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }

    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void input_set_default_bindings(void)
{
    for (size_t i = 0; i < INPUT_ACTION_COUNT; ++i) {
        g_action_bindings[i] = g_action_info[i].default_key;
    }
    g_bindings_initialized = true;
}

static void input_ensure_bindings(void)
{
    if (!g_bindings_initialized) {
        input_set_default_bindings();
    }
}

size_t input_action_count(void)
{
    return INPUT_ACTION_COUNT;
}

InputAction input_action_by_index(size_t index)
{
    if (index >= INPUT_ACTION_COUNT) {
        return INPUT_ACTION_COUNT;
    }
    return (InputAction)index;
}

const char *input_action_display_name(InputAction action)
{
    if (action < 0 || action >= INPUT_ACTION_COUNT) {
        return "Unknown";
    }
    return g_action_info[action].name;
}

const char *input_action_token(InputAction action)
{
    if (action < 0 || action >= INPUT_ACTION_COUNT) {
        return NULL;
    }
    return g_action_info[action].token;
}

InputAction input_action_from_token(const char *token)
{
    if (!token || !token[0]) {
        return INPUT_ACTION_COUNT;
    }

    for (size_t i = 0; i < INPUT_ACTION_COUNT; ++i) {
        if (g_action_info[i].token && input_stricmp(g_action_info[i].token, token) == 0) {
            return g_action_info[i].action;
        }
    }

    return INPUT_ACTION_COUNT;
}

PlatformKey input_action_default_key(InputAction action)
{
    if (action < 0 || action >= INPUT_ACTION_COUNT) {
        return PLATFORM_KEY_UNKNOWN;
    }
    return g_action_info[action].default_key;
}

PlatformKey input_binding_get(InputAction action)
{
    input_ensure_bindings();
    if (action < 0 || action >= INPUT_ACTION_COUNT) {
        return PLATFORM_KEY_UNKNOWN;
    }
    return g_action_bindings[action];
}

bool input_binding_set(InputAction action, PlatformKey key)
{
    input_ensure_bindings();
    if (action < 0 || action >= INPUT_ACTION_COUNT) {
        return false;
    }

    if (key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
        g_action_bindings[action] = PLATFORM_KEY_UNKNOWN;
        return true;
    }

    for (size_t i = 0; i < INPUT_ACTION_COUNT; ++i) {
        if (i == (size_t)action) {
            continue;
        }
        if (g_action_bindings[i] == key) {
            g_action_bindings[i] = PLATFORM_KEY_UNKNOWN;
        }
    }

    g_action_bindings[action] = key;
    return true;
}

void input_bindings_reset_defaults(void)
{
    input_set_default_bindings();
}

const char *input_key_display_name(PlatformKey key)
{
    if (key >= PLATFORM_KEY_A && key <= PLATFORM_KEY_Z) {
        return g_key_letters[key - PLATFORM_KEY_A];
    }
    if (key >= PLATFORM_KEY_0 && key <= PLATFORM_KEY_9) {
        return g_key_digits[key - PLATFORM_KEY_0];
    }
    if (key >= PLATFORM_KEY_F1 && key <= PLATFORM_KEY_F12) {
        return g_key_function_keys[key - PLATFORM_KEY_F1];
    }

    for (size_t i = 0; i < sizeof(g_special_key_names) / sizeof(g_special_key_names[0]); ++i) {
        if (g_special_key_names[i].key == key) {
            return g_special_key_names[i].display;
        }
    }

    return "Unknown";
}

const char *input_key_token(PlatformKey key)
{
    if (key >= PLATFORM_KEY_A && key <= PLATFORM_KEY_Z) {
        return g_key_letters[key - PLATFORM_KEY_A];
    }
    if (key >= PLATFORM_KEY_0 && key <= PLATFORM_KEY_9) {
        return g_key_digits[key - PLATFORM_KEY_0];
    }
    if (key >= PLATFORM_KEY_F1 && key <= PLATFORM_KEY_F12) {
        return g_key_function_keys[key - PLATFORM_KEY_F1];
    }

    for (size_t i = 0; i < sizeof(g_special_key_names) / sizeof(g_special_key_names[0]); ++i) {
        if (g_special_key_names[i].key == key) {
            return g_special_key_names[i].token;
        }
    }

    return "unknown";
}

PlatformKey input_key_from_token(const char *token)
{
    if (!token || !token[0]) {
        return PLATFORM_KEY_UNKNOWN;
    }

    size_t len = strlen(token);
    if (len == 1) {
        char c = token[0];
        if (c >= 'A' && c <= 'Z') {
            return (PlatformKey)(PLATFORM_KEY_A + (c - 'A'));
        }
        if (c >= 'a' && c <= 'z') {
            return (PlatformKey)(PLATFORM_KEY_A + (c - 'a'));
        }
        if (c >= '0' && c <= '9') {
            return (PlatformKey)(PLATFORM_KEY_0 + (c - '0'));
        }
    }

    if ((token[0] == 'F' || token[0] == 'f') && len >= 2 && len <= 3) {
        int number = 0;
        for (size_t i = 1; i < len; ++i) {
            if (token[i] < '0' || token[i] > '9') {
                number = 0;
                break;
            }
            number = number * 10 + (token[i] - '0');
        }
        if (number >= 1 && number <= 12) {
            return (PlatformKey)(PLATFORM_KEY_F1 + (number - 1));
        }
    }

    for (size_t i = 0; i < sizeof(g_special_key_names) / sizeof(g_special_key_names[0]); ++i) {
        if (input_stricmp(g_special_key_names[i].token, token) == 0) {
            return g_special_key_names[i].key;
        }
    }

    return PLATFORM_KEY_UNKNOWN;
}

PlatformKey input_first_pressed_key(const InputState *state)
{
    if (!state) {
        return PLATFORM_KEY_UNKNOWN;
    }
    return state->last_pressed_key;
}

void input_bindings_export(PlatformKey out_bindings[INPUT_ACTION_COUNT])
{
    if (!out_bindings) {
        return;
    }

    input_ensure_bindings();
    for (size_t i = 0; i < INPUT_ACTION_COUNT; ++i) {
        out_bindings[i] = g_action_bindings[i];
    }
}

bool input_bindings_import(const PlatformKey bindings[INPUT_ACTION_COUNT])
{
    if (!bindings) {
        return false;
    }

    for (size_t i = 0; i < INPUT_ACTION_COUNT; ++i) {
        PlatformKey key = bindings[i];
        if (key <= PLATFORM_KEY_UNKNOWN || key >= PLATFORM_KEY_COUNT) {
            g_action_bindings[i] = PLATFORM_KEY_UNKNOWN;
            continue;
        }

        /* Remove duplicates encountered earlier in the list. */
        for (size_t j = 0; j < i; ++j) {
            if (g_action_bindings[j] == key) {
                g_action_bindings[j] = PLATFORM_KEY_UNKNOWN;
            }
        }
        g_action_bindings[i] = key;
    }

    g_bindings_initialized = true;
    return true;
}

static bool binding_down(const InputState *state, InputAction action)
{
    if (!state) {
        return false;
    }

    PlatformKey key = input_binding_get(action);
    int index = (int)key;
    if (index <= PLATFORM_KEY_UNKNOWN || index >= PLATFORM_KEY_COUNT) {
        return false;
    }
    return state->key_down[index];
}

static bool binding_pressed(const InputState *state, InputAction action)
{
    if (!state) {
        return false;
    }

    PlatformKey key = input_binding_get(action);
    int index = (int)key;
    if (index <= PLATFORM_KEY_UNKNOWN || index >= PLATFORM_KEY_COUNT) {
        return false;
    }
    return state->key_pressed[index];
}

void input_reset(InputState *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->last_pressed_key = PLATFORM_KEY_UNKNOWN;
}

void input_update(InputState *state, const PlatformWindow *window, float dt)
{
    (void)dt;

    if (!state) {
        return;
    }

    input_ensure_bindings();
    state->last_pressed_key = PLATFORM_KEY_UNKNOWN;

    if (window) {
        for (int k = 0; k < PLATFORM_KEY_COUNT; ++k) {
            PlatformKey key = (PlatformKey)k;
            bool down = platform_key_down(window, key);
            bool pressed = platform_key_pressed(window, key);
            state->key_down[k] = down;
            state->key_pressed[k] = pressed;
            if (pressed && state->last_pressed_key == PLATFORM_KEY_UNKNOWN) {
                state->last_pressed_key = key;
            }
        }

        int mouse_dx = 0;
        int mouse_dy = 0;
        platform_mouse_delta(window, &mouse_dx, &mouse_dy);

        const float sensitivity = 0.0025f;
        state->look_delta_x = (float)mouse_dx * sensitivity;
        state->look_delta_y = (float)mouse_dy * sensitivity;

        state->mouse_wheel = platform_mouse_wheel_delta(window);
        state->fire_pressed = platform_mouse_button_pressed(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->fire_down = platform_mouse_button_down(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->mouse_left_down = state->fire_down;
        state->mouse_left_pressed = state->fire_pressed;
        state->mouse_left_released = platform_mouse_button_released(window, PLATFORM_MOUSE_BUTTON_LEFT);
        state->mouse_right_down = platform_mouse_button_down(window, PLATFORM_MOUSE_BUTTON_RIGHT);
        state->mouse_right_pressed = platform_mouse_button_pressed(window, PLATFORM_MOUSE_BUTTON_RIGHT);
        state->mouse_right_released = platform_mouse_button_released(window, PLATFORM_MOUSE_BUTTON_RIGHT);
        platform_mouse_position(window, &state->mouse_x, &state->mouse_y);
    } else {
        for (int k = 0; k < PLATFORM_KEY_COUNT; ++k) {
            state->key_down[k] = false;
            state->key_pressed[k] = false;
        }
        state->look_delta_x = 0.0f;
        state->look_delta_y = 0.0f;
        state->mouse_wheel = 0.0f;
        state->fire_pressed = false;
        state->fire_down = false;
        state->mouse_left_down = false;
        state->mouse_left_pressed = false;
        state->mouse_left_released = false;
        state->mouse_right_down = false;
        state->mouse_right_pressed = false;
        state->mouse_right_released = false;
        state->mouse_x = 0;
        state->mouse_y = 0;
    }

    float forward = 0.0f;
    float right = 0.0f;
    float vertical = 0.0f;

    if (binding_down(state, INPUT_ACTION_MOVE_FORWARD)) {
        forward += 1.0f;
    }
    if (binding_down(state, INPUT_ACTION_MOVE_BACKWARD)) {
        forward -= 1.0f;
    }
    if (binding_down(state, INPUT_ACTION_MOVE_RIGHT)) {
        right += 1.0f;
    }
    if (binding_down(state, INPUT_ACTION_MOVE_LEFT)) {
        right -= 1.0f;
    }
    if (binding_down(state, INPUT_ACTION_JUMP)) {
        vertical += 1.0f;
    }
    if (binding_down(state, INPUT_ACTION_CROUCH)) {
        vertical -= 1.0f;
    }

    state->move_forward = clamp_axis(forward);
    state->move_right = clamp_axis(right);
    state->move_vertical = clamp_axis(vertical);

    state->jump_pressed = binding_pressed(state, INPUT_ACTION_JUMP);
    state->sprinting = binding_down(state, INPUT_ACTION_SPRINT);
    state->escape_pressed = binding_pressed(state, INPUT_ACTION_MENU);
    state->reload_pressed = binding_pressed(state, INPUT_ACTION_RELOAD);
    state->interact_pressed = binding_pressed(state, INPUT_ACTION_INTERACT);
    state->drop_pressed = binding_pressed(state, INPUT_ACTION_DROP_WEAPON);
    state->drop_down = binding_down(state, INPUT_ACTION_DROP_WEAPON);
}

