#include "engine/settings_menu.h"
#include "engine/renderer.h"
#include "engine/audio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

#define SETTINGS_PANEL_MARGIN            36.0f
#define SETTINGS_TABS_HEIGHT             42.0f
#define SETTINGS_TABS_SPACING            12.0f
#define SETTINGS_CONTENT_PADDING         32.0f
#define SETTINGS_ROW_HEIGHT_WIDE         48.0f
#define SETTINGS_ROW_HEIGHT_NARROW       76.0f
#define SETTINGS_LIST_HEADER_HEIGHT      30.0f
#define SETTINGS_LIST_SPACING            10.0f
#define SETTINGS_DEFAULT_FEEDBACK_FRAMES 180

static double g_settings_ui_time = 0.0;

static float settings_hover_mix(void)
{
    float pulse = sinf((float)(g_settings_ui_time * 2.0 * (float)M_PI));
    return (pulse * 0.5f + 0.5f) * 0.6f;
}

static void settings_apply_interaction_tint(float *r,
                                            float *g,
                                            float *b,
                                            bool hovered,
                                            bool pressed)
{
    if (!r || !g || !b) {
        return;
    }

    if (hovered && !pressed) {
        float mix = settings_hover_mix();
        *r += (1.0f - *r) * mix;
        *g += (1.0f - *g) * mix;
        *b += (1.0f - *b) * mix;
    }

    if (pressed) {
        const float darken = 0.7f;
        const float scale = 1.0f - darken;
        *r *= scale;
        *g *= scale;
        *b *= scale;
    }
}

static uint32_t settings_gcd_uint(uint32_t a, uint32_t b)
{
    while (b != 0U) {
        uint32_t r = a % b;
        a = b;
        b = r;
    }
    return a ? a : 1U;
}

static void settings_format_resolution(char *buffer,
                                       size_t buffer_size,
                                       uint32_t width,
                                       uint32_t height)
{
    if (!buffer || buffer_size == 0U) {
        return;
    }

    if (width == 0U || height == 0U) {
        buffer[0] = '\0';
        return;
    }

    uint32_t g = settings_gcd_uint(width, height);
    uint32_t aspect_x = width / g;
    uint32_t aspect_y = height / g;
    snprintf(buffer, buffer_size, "%u x %u (%u:%u)", width, height, aspect_x, aspect_y);
}

static bool point_in_rect(float px, float py, float rx, float ry, float rw, float rh)
{
    return px >= rx && px <= (rx + rw) && py >= ry && py <= (ry + rh);
}

static void settings_record_feedback(SettingsMenuState *state,
                                     const char *message,
                                     InputAction action,
                                     PlatformKey key)
{
    if (!state) {
        return;
    }

    state->feedback_frames = SETTINGS_DEFAULT_FEEDBACK_FRAMES;
    state->feedback_action = action;
    state->feedback_key = key;

    if (message && message[0] != '\0') {
        state->feedback_has_message = true;
        size_t copy_len = strlen(message);
        if (copy_len >= sizeof(state->feedback_message)) {
            copy_len = sizeof(state->feedback_message) - 1U;
        }
        memcpy(state->feedback_message, message, copy_len);
        state->feedback_message[copy_len] = '\0';
    } else {
        state->feedback_has_message = false;
        state->feedback_message[0] = '\0';
    }
}

static void settings_interaction_begin_frame(SettingsMenuState *state)
{
    if (!state) {
        return;
    }
    state->interaction_consumed = false;
    state->interaction_locked = false;
    state->interaction_lock_x = 0.0f;
    state->interaction_lock_y = 0.0f;
    state->interaction_lock_w = 0.0f;
    state->interaction_lock_h = 0.0f;
}

static void settings_interaction_capture(SettingsMenuState *state,
                                         float x,
                                         float y,
                                         float width,
                                         float height)
{
    if (!state) {
        return;
    }
    state->interaction_locked = true;
    state->interaction_lock_x = x;
    state->interaction_lock_y = y;
    state->interaction_lock_w = width;
    state->interaction_lock_h = height;
}

static bool settings_interaction_pointer_inside(const SettingsMenuState *state,
                                                float mx,
                                                float my)
{
    if (!state || !state->interaction_locked) {
        return false;
    }
    return point_in_rect(mx, my,
                         state->interaction_lock_x,
                         state->interaction_lock_y,
                         state->interaction_lock_w,
                         state->interaction_lock_h);
}

static void settings_interaction_consume(SettingsMenuState *state)
{
    if (state) {
        state->interaction_consumed = true;
    }
}

static bool settings_interaction_blocked(const SettingsMenuState *state,
                                         const InputState *input,
                                         float x,
                                         float y,
                                         float width,
                                         float height,
                                         bool overlay_control)
{
    if (!state || !input) {
        return false;
    }
    if (state->interaction_consumed) {
        return true;
    }
    if (!state->interaction_locked) {
        return false;
    }

    float mx = (float)input->mouse_x;
    float my = (float)input->mouse_y;

    if (overlay_control) {
        return !settings_interaction_pointer_inside(state, mx, my);
    }

    /* Block interactions with non-overlay controls while a dropdown is open. */
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return state->interaction_locked;
}

static bool settings_button(SettingsMenuState *state,
                            Renderer *renderer,
                            const InputState *input,
                            float x,
                            float y,
                            float width,
                            float height,
                            const char *label,
                            bool highlighted)
{
    if (!renderer || !label) {
        return false;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    bool blocked = settings_interaction_blocked(state, input, x, y, width, height, false);
    bool hovered = !blocked && input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = highlighted ? 0.28f : 0.12f;
    if (!highlighted && hovered) {
        base = 0.18f;
    }
    float r = base;
    float g = base * 0.85f;
    float b = base * 0.7f;
    settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
    float alpha = highlighted ? 0.95f : 0.88f;
    if (hovered && !pressed) {
        float mix = settings_hover_mix();
        alpha += (1.0f - alpha) * (mix * 0.45f);
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }
    }
    renderer_draw_ui_rect(renderer, x, y, width, height, r, g, b, alpha);
    renderer_draw_ui_text(renderer, x + 20.0f, y + height * 0.5f - 8.0f, label, 0.97f, 0.97f, 0.99f, 1.0f);
    return pressed;
}

static bool settings_tab_button(SettingsMenuState *state,
                                Renderer *renderer,
                                const InputState *input,
                                float x,
                                float y,
                                float width,
                                float height,
                                const char *label,
                                bool active)
{
    if (!renderer || !label) {
        return false;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    bool blocked = settings_interaction_blocked(state, input, x, y, width, height, false);
    bool hovered = !blocked && input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = active ? 0.32f : 0.14f;
    if (!active && hovered) {
        base = 0.20f;
    }
    float r = base;
    float g = base * 0.82f;
    float b = base * 0.68f;
    settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
    float alpha = active ? 0.95f : 0.9f;
    if (!active && hovered && !pressed) {
        float mix = settings_hover_mix();
        alpha += (1.0f - alpha) * (mix * 0.35f);
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }
    }
    renderer_draw_ui_rect(renderer, x, y, width, height, r, g, b, alpha);
    renderer_draw_ui_text(renderer,
                          x + 18.0f,
                          y + height * 0.5f - 8.0f,
                          label,
                          0.95f,
                          0.95f,
                          0.98f,
                          active ? 1.0f : 0.92f);
    return pressed;
}

static bool settings_toggle(SettingsMenuState *state,
                            Renderer *renderer,
                            const InputState *input,
                            float x,
                            float y,
                            float width,
                            float height,
                            const char *label,
                            bool *value)
{
    if (!renderer || !label || !value) {
        return false;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    bool blocked = settings_interaction_blocked(state, input, x, y, width, height, false);
    bool hovered = !blocked && input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = *value ? 0.25f : 0.10f;
    float r = base;
    float g = base * 0.85f;
    float b = base * 0.7f;
    settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
    float alpha = 0.88f;
    if (hovered && !pressed) {
        float mix = settings_hover_mix();
        alpha += (1.0f - alpha) * (mix * 0.4f);
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }
    }
    renderer_draw_ui_rect(renderer, x, y, width, height, r, g, b, alpha);

    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s: %s", label, *value ? "ON" : "OFF");
    renderer_draw_ui_text(renderer, x + 16.0f, y + height * 0.5f - 8.0f, buffer, 0.96f, 0.96f, 0.98f, 0.98f);

    if (pressed) {
        *value = !*value;
        return true;
    }
    return false;
}

static bool settings_binding_button(SettingsMenuState *state,
                                    Renderer *renderer,
                                    const InputState *input,
                                    float x,
                                    float y,
                                    float width,
                                    float height,
                                    const char *label,
                                    bool listening,
                                    bool disabled)
{
    if (!renderer || !label) {
        return false;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    bool blocked = settings_interaction_blocked(state, input, x, y, width, height, false);
    bool hovered = !disabled && !blocked && input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = listening ? 0.32f : 0.14f;
    if (!listening && hovered) {
        base = 0.22f;
    }
    float r = base;
    float g = base * 0.82f;
    float b = base * 0.66f;
    if (!disabled) {
        settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
    }
    float alpha = disabled ? 0.55f : 0.9f;
    if (hovered && !pressed && !disabled) {
        float mix = settings_hover_mix();
        alpha += (1.0f - alpha) * (mix * 0.35f);
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }
    }
    renderer_draw_ui_rect(renderer, x, y, width, height, r, g, b, alpha);
    renderer_draw_ui_text(renderer, x + 16.0f, y + height * 0.5f - 8.0f, label, 0.96f, 0.96f, 0.98f, disabled ? 0.75f : 1.0f);
    return !disabled && pressed;
}

static bool settings_reset_button(SettingsMenuState *state,
                                  Renderer *renderer,
                                  const InputState *input,
                                  float x,
                                  float y,
                                  float width,
                                  float height)
{
    return settings_button(state, renderer, input, x, y, width, height, "Reset", false);
}

static bool settings_dropdown_header(SettingsMenuState *state,
                                     Renderer *renderer,
                                     const InputState *input,
                                     float x,
                                     float y,
                                     float width,
                                     float height,
                                     const char *label,
                                     const char *value,
                                     bool open)
{
    if (!renderer || !label || !value) {
        return false;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    bool blocked = settings_interaction_blocked(state, input, x, y, width, height, open);
    bool hovered = !blocked && input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;
    float base = open ? 0.32f : 0.14f;
    if (!open && hovered) {
        base = 0.24f;
    }
    float r = base;
    float g = base * 0.85f;
    float b = base * 0.7f;
    if (!open) {
        settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
    } else if (pressed) {
        settings_apply_interaction_tint(&r, &g, &b, true, pressed);
    }
    float alpha = open ? 0.98f : (hovered ? 0.9f : 0.85f);
    if (hovered && !open && !pressed) {
        float mix = settings_hover_mix();
        alpha += (1.0f - alpha) * (mix * 0.4f);
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }
    }
    renderer_draw_ui_rect(renderer, x, y, width, height, r, g, b, alpha);
    renderer_draw_ui_text(renderer, x + 18.0f, y + height * 0.5f - 8.0f, label, 0.96f, 0.96f, 0.98f, 0.96f);

    float value_x = x + width - 18.0f - (float)strlen(value) * 8.0f;
    float min_value_x = x + 160.0f;
    if (value_x < min_value_x) {
        value_x = min_value_x;
    }
    renderer_draw_ui_text(renderer, value_x, y + height * 0.5f - 8.0f, value, 0.92f, 0.92f, 0.98f, 0.92f);

    const char *indicator = open ? "^" : "v";
    renderer_draw_ui_text(renderer, x + width - 26.0f, y + height * 0.5f - 8.0f, indicator, 0.92f, 0.92f, 0.98f, 0.92f);

    return hovered && input && input->mouse_left_pressed;
}

static bool settings_slider(SettingsMenuState *state,
                            Renderer *renderer,
                            const InputState *input,
                            float x,
                            float y,
                            float width,
                            float height,
                            const char *label,
                            float min_value,
                            float max_value,
                            float step,
                            float display_scale,
                            float *value,
                            const char *format)
{
    if (!renderer || !label || !value || !format) {
        return false;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    const float slider_padding = 16.0f;
    const float slider_height = 14.0f;
    float slider_x = x + slider_padding;
    float slider_y = y + height * 0.5f;
    float slider_width = width - slider_padding * 2.0f;
    if (slider_width < 40.0f) {
        slider_width = 40.0f;
    }

    bool blocked = settings_interaction_blocked(state, input, slider_x, slider_y - 8.0f, slider_width, slider_height + 16.0f, false);
    bool hovered = !blocked && input && point_in_rect(mx, my, slider_x, slider_y, slider_width, slider_height);
    bool active = hovered && input && input->mouse_left_down;

    float t = 0.0f;
    if (max_value > min_value) {
        t = (*value - min_value) / (max_value - min_value);
    }
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }

    renderer_draw_ui_text(renderer,
                          x + slider_padding,
                          y + 6.0f,
                          label,
                          0.92f,
                          0.92f,
                          0.96f,
                          0.94f);

    char value_text[48];
    float display_value = (*value) * display_scale;
    snprintf(value_text, sizeof(value_text), format, display_value);
    renderer_draw_ui_text(renderer,
                          x + width - slider_padding - (float)strlen(value_text) * 8.0f,
                          y + 6.0f,
                          value_text,
                          0.82f,
                          0.82f,
                          0.9f,
                          0.92f);

    float track_r = 0.12f;
    float track_g = 0.12f;
    float track_b = 0.18f;
    if (hovered) {
        float mix = settings_hover_mix();
        track_r += (1.0f - track_r) * (mix * 0.25f);
        track_g += (1.0f - track_g) * (mix * 0.25f);
        track_b += (1.0f - track_b) * (mix * 0.15f);
    }
    renderer_draw_ui_rect(renderer, slider_x, slider_y, slider_width, slider_height, track_r, track_g, track_b, 0.88f);
    renderer_draw_ui_rect(renderer, slider_x, slider_y, slider_width * t, slider_height, 0.32f, 0.38f, 0.62f, 0.94f);

    float handle_x = slider_x + slider_width * t - 6.0f;
    float handle_r = hovered ? 0.85f : 0.75f;
    float handle_g = 0.82f;
    float handle_b = 0.95f;
    settings_apply_interaction_tint(&handle_r, &handle_g, &handle_b, hovered, active);
    float handle_alpha = hovered ? 0.96f : 0.9f;
    if (hovered && !active) {
        float mix = settings_hover_mix();
        handle_alpha += (1.0f - handle_alpha) * (mix * 0.4f);
        if (handle_alpha > 1.0f) {
            handle_alpha = 1.0f;
        }
    }
    renderer_draw_ui_rect(renderer, handle_x, slider_y - 2.0f, 12.0f, slider_height + 4.0f, handle_r, handle_g, handle_b, handle_alpha);

    if (!input || blocked) {
        return false;
    }

    if (active) {
        float norm = (mx - slider_x) / slider_width;
        if (norm < 0.0f) {
            norm = 0.0f;
        } else if (norm > 1.0f) {
            norm = 1.0f;
        }
        float new_value = min_value + norm * (max_value - min_value);
        if (step > 0.0f) {
            float steps = roundf((new_value - min_value) / step);
            new_value = min_value + steps * step;
        }
        if (new_value < min_value) {
            new_value = min_value;
        } else if (new_value > max_value) {
            new_value = max_value;
        }
        if (fabsf(new_value - *value) > 0.0001f) {
            *value = new_value;
            return true;
        }
    }

    return false;
}

static size_t settings_find_resolution_index(const PreferencesResolution *resolutions,
                                             size_t count,
                                             uint32_t width,
                                             uint32_t height)
{
    if (!resolutions || count == 0) {
        return 0;
    }

    for (size_t i = 0; i < count; ++i) {
        if (resolutions[i].width == width && resolutions[i].height == height) {
            return i;
        }
    }
    return 0;
}

void settings_menu_init(SettingsMenuState *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->active_category = SETTINGS_MENU_CATEGORY_CONTROLS;
    state->pending_action = INPUT_ACTION_COUNT;
}

void settings_menu_cancel_rebind(SettingsMenuState *state)
{
    if (!state) {
        return;
    }

    state->waiting_for_rebind = false;
    state->pending_action = INPUT_ACTION_COUNT;
}

static void render_controls_tab(SettingsMenuState *state,
                                Renderer *renderer,
                                const InputState *input,
                                float x,
                                float y,
                                float width,
                                float height,
                                SettingsMenuResult *result)
{
    if (!state || !renderer) {
        return;
    }

    const float instructions_height = 28.0f;
    renderer_draw_ui_text(renderer,
                          x,
                          y,
                          "Click a binding to change it. Press Escape to cancel a pending change.",
                          0.82f,
                          0.82f,
                          0.9f,
                          0.92f);

    const float list_top = y + instructions_height + 12.0f;
    const bool wide_layout = (width >= 720.0f);
    const float row_height = wide_layout ? SETTINGS_ROW_HEIGHT_WIDE : SETTINGS_ROW_HEIGHT_NARROW;
    const float header_height = wide_layout ? SETTINGS_LIST_HEADER_HEIGHT : 0.0f;

    if (wide_layout) {
        const float col_action = width * 0.38f;
        const float col_binding = width * 0.26f;
        const float col_default = width * 0.18f;
        const float col_reset = width - (col_action + col_binding + col_default);

        renderer_draw_ui_rect(renderer, x, list_top, width, header_height, 0.12f, 0.12f, 0.16f, 0.85f);
        renderer_draw_ui_text(renderer, x + 8.0f, list_top + header_height * 0.5f - 8.0f, "Action", 0.86f, 0.86f, 0.92f, 0.92f);
        renderer_draw_ui_text(renderer, x + col_action + 8.0f, list_top + header_height * 0.5f - 8.0f, "Binding", 0.86f, 0.86f, 0.92f, 0.92f);
        renderer_draw_ui_text(renderer, x + col_action + col_binding + 8.0f, list_top + header_height * 0.5f - 8.0f, "Default", 0.86f, 0.86f, 0.92f, 0.92f);
        renderer_draw_ui_text(renderer, x + col_action + col_binding + col_default + 8.0f, list_top + header_height * 0.5f - 8.0f, "Reset", 0.86f, 0.86f, 0.92f, 0.92f);

        float row_y = list_top + header_height + SETTINGS_LIST_SPACING;
        size_t action_count = input_action_count();
        for (size_t i = 0; i < action_count; ++i) {
            InputAction action = input_action_by_index(i);
            const bool is_pending = state->waiting_for_rebind && state->pending_action == action;
            const char *action_name = input_action_display_name(action);
            PlatformKey current_key = input_binding_get(action);
            PlatformKey default_key = input_action_default_key(action);
            const char *binding_label = is_pending ? "Press a key..." : input_key_display_name(current_key);
            const char *default_label = input_key_display_name(default_key);

            renderer_draw_ui_rect(renderer, x, row_y, width, row_height, 0.07f, 0.07f, 0.1f, 0.6f);
            renderer_draw_ui_text(renderer, x + 12.0f, row_y + row_height * 0.5f - 8.0f, action_name, 0.95f, 0.95f, 0.98f, 0.96f);

            const float binding_x = x + col_action + 8.0f;
            const float binding_width = col_binding - 16.0f;
            bool binding_clicked = settings_binding_button(state,
                                                           renderer,
                                                           input,
                                                           binding_x,
                                                           row_y + 8.0f,
                                                           binding_width,
                                                           row_height - 16.0f,
                                                           binding_label,
                                                           is_pending,
                                                           state->waiting_for_rebind && !is_pending);

            renderer_draw_ui_text(renderer,
                                  x + col_action + col_binding + 8.0f,
                                  row_y + row_height * 0.5f - 8.0f,
                                  default_label,
                                  0.85f,
                                  0.85f,
                                  0.9f,
                                  0.9f);

            const float reset_x = x + col_action + col_binding + col_default + 8.0f;
            const float reset_width = col_reset - 16.0f;
            bool reset_clicked = settings_reset_button(state,
                                                       renderer,
                                                       input,
                                                       reset_x,
                                                       row_y + 8.0f,
                                                       reset_width,
                                                       row_height - 16.0f);

            if (binding_clicked) {
                if (is_pending) {
                    settings_menu_cancel_rebind(state);
                } else {
                    state->waiting_for_rebind = true;
                    state->pending_action = action;
                    state->feedback_frames = 0;
                    state->feedback_has_message = false;
                    state->feedback_action = INPUT_ACTION_COUNT;
                }
            }

            if (reset_clicked) {
                PlatformKey desired = input_action_default_key(action);
                input_binding_set(action, desired);
                if (result) {
                    result->binding_reset = true;
                    result->binding_reset_action = action;
                }
                settings_record_feedback(state, "Binding reset to default", action, desired);
                state->waiting_for_rebind = false;
                state->pending_action = INPUT_ACTION_COUNT;
            }

            row_y += row_height + SETTINGS_LIST_SPACING;
        }
    } else {
        size_t action_count = input_action_count();
        float row_y = list_top;
        for (size_t i = 0; i < action_count; ++i) {
            InputAction action = input_action_by_index(i);
            const bool is_pending = state->waiting_for_rebind && state->pending_action == action;
            const char *action_name = input_action_display_name(action);
            PlatformKey current_key = input_binding_get(action);
            PlatformKey default_key = input_action_default_key(action);
            const char *binding_label = is_pending ? "Press a key..." : input_key_display_name(current_key);
            const char *default_label = input_key_display_name(default_key);

            renderer_draw_ui_rect(renderer, x, row_y, width, row_height, 0.07f, 0.07f, 0.1f, 0.62f);
            renderer_draw_ui_text(renderer, x + 12.0f, row_y + 12.0f, action_name, 0.95f, 0.95f, 0.98f, 0.96f);

            bool binding_clicked = settings_binding_button(state,
                                                           renderer,
                                                           input,
                                                           x + 12.0f,
                                                           row_y + 30.0f,
                                                           width - 24.0f,
                                                           34.0f,
                                                           binding_label,
                                                           is_pending,
                                                           state->waiting_for_rebind && !is_pending);

            char default_buffer[64];
            snprintf(default_buffer, sizeof(default_buffer), "Default: %s", default_label);
            renderer_draw_ui_text(renderer,
                                  x + 12.0f,
                                  row_y + 30.0f + 34.0f + 6.0f,
                                  default_buffer,
                                  0.82f,
                                  0.82f,
                                  0.9f,
                                  0.88f);

            bool reset_clicked = settings_reset_button(state,
                                                       renderer,
                                                       input,
                                                       x + width - 112.0f,
                                                       row_y + 30.0f + 34.0f + 2.0f,
                                                       100.0f,
                                                       28.0f);

            if (binding_clicked) {
                if (is_pending) {
                    settings_menu_cancel_rebind(state);
                } else {
                    state->waiting_for_rebind = true;
                    state->pending_action = action;
                    state->feedback_frames = 0;
                    state->feedback_has_message = false;
                    state->feedback_action = INPUT_ACTION_COUNT;
                }
            }

            if (reset_clicked) {
                PlatformKey desired = input_action_default_key(action);
                input_binding_set(action, desired);
                if (result) {
                    result->binding_reset = true;
                    result->binding_reset_action = action;
                }
                settings_record_feedback(state, "Binding reset to default", action, desired);
                state->waiting_for_rebind = false;
                state->pending_action = INPUT_ACTION_COUNT;
            }

            row_y += row_height + SETTINGS_LIST_SPACING;
        }
    }

    const float reset_all_y = y + height - 56.0f;
    if (settings_button(state,
                        renderer,
                        input,
                        x,
                        reset_all_y,
                        width,
                        40.0f,
                        "Reset All Controls",
                        false)) {
        input_bindings_reset_defaults();
        if (result) {
            result->reset_all_bindings = true;
        }
        settings_record_feedback(state, "All controls restored to defaults", INPUT_ACTION_COUNT, PLATFORM_KEY_UNKNOWN);
        state->waiting_for_rebind = false;
        state->pending_action = INPUT_ACTION_COUNT;
    }
}

static void render_graphics_tab(SettingsMenuState *state,
                                const SettingsMenuContext *context,
                                Renderer *renderer,
                                const InputState *input,
                                float x,
                                float y,
                                float width,
                                SettingsMenuResult *result)
{
    if (!state || !renderer) {
        return;
    }

    const PreferencesResolution *resolutions = context ? context->resolutions : NULL;
    size_t resolution_count = context ? context->resolution_count : 0;

    if (!state->graphics_initialized) {
        if (context && context->window_mode) {
            state->graphics_mode = *context->window_mode;
        } else {
            state->graphics_mode = PLATFORM_WINDOW_MODE_FULLSCREEN;
        }

        if (resolutions && resolution_count > 0 && context && context->resolution_width && context->resolution_height) {
            state->graphics_resolution_index = settings_find_resolution_index(resolutions,
                                                                             resolution_count,
                                                                             *context->resolution_width,
                                                                             *context->resolution_height);
        } else {
            state->graphics_resolution_index = 0;
        }
        state->graphics_initialized = true;
    }

    renderer_draw_ui_text(renderer,
                          x,
                          y,
                          "Configure display mode and resolution.",
                          0.82f,
                          0.82f,
                          0.9f,
                          0.92f);
    renderer_draw_ui_text(renderer,
                          x,
                          y + 22.0f,
                          "Click to open a list, then use the mouse wheel or scrollbar to browse.",
                          0.68f,
                          0.68f,
                          0.75f,
                          0.86f);

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;

    float row_y = y + 54.0f;
    const float row_height = 46.0f;
    const float row_width = width * 0.7f;
    float viewport_height = (float)renderer_viewport_height(renderer);
    if (viewport_height < 1.0f) {
        viewport_height = 1.0f;
    }
    float max_dropdown_height = viewport_height * 0.6f;
    if (max_dropdown_height < row_height) {
        max_dropdown_height = row_height;
    }
    size_t max_visible_rows = (size_t)(max_dropdown_height / row_height);
    if (max_visible_rows == 0U) {
        max_visible_rows = 1U;
    }

    static const char *const mode_names[] = {
        "Fullscreen",
        "Windowed",
        "Borderless"
    };

    const char *mode_label = mode_names[state->graphics_mode < PLATFORM_WINDOW_MODE_COUNT ? state->graphics_mode : 0];
    float mode_header_x = x;
    float mode_header_y = row_y;
    float mode_header_w = row_width;
    float mode_header_h = row_height;
    bool mode_toggled = settings_dropdown_header(state,
                                                 renderer,
                                                 input,
                                                 mode_header_x,
                                                 mode_header_y,
                                                 mode_header_w,
                                                 mode_header_h,
                                                 "Window Mode",
                                                 mode_label,
                                                 state->graphics_mode_dropdown_open);
    if (mode_toggled) {
        bool now_open = !state->graphics_mode_dropdown_open;
        state->graphics_mode_dropdown_open = now_open;
        if (now_open) {
            state->graphics_resolution_dropdown_open = false;
        }
    }

    if (state->graphics_mode_dropdown_open) {
        const float option_height = row_height;
        const float list_x = mode_header_x;
        const float list_y = mode_header_y + mode_header_h;
        const float list_w = mode_header_w;
        const float list_h = option_height * (float)PLATFORM_WINDOW_MODE_COUNT;

        settings_interaction_capture(state, mode_header_x, mode_header_y, mode_header_w, mode_header_h + list_h);

        renderer_draw_ui_rect(renderer, list_x, list_y, list_w, list_h, 0.10f, 0.10f, 0.14f, 0.94f);

        for (int i = 0; i < (int)PLATFORM_WINDOW_MODE_COUNT; ++i) {
            float item_y = list_y + option_height * (float)i;
            bool hovered = input && point_in_rect(mx, my, list_x, item_y, list_w, option_height);
            bool pressed = hovered && input && input->mouse_left_pressed;
            bool selected = state->graphics_mode == (PlatformWindowMode)i;
            float base = selected ? 0.32f : 0.18f;
            float r = base;
            float g = base * 0.85f;
            float b = base * 0.7f;
            settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
            if (selected) {
                if (r < 0.32f) {
                    r = 0.32f;
                }
                if (g < 0.28f) {
                    g = 0.28f;
                }
                if (b < 0.24f) {
                    b = 0.24f;
                }
            }
            float option_alpha = selected ? 0.98f : 0.92f;
            if (hovered && !pressed) {
                float mix = settings_hover_mix();
                option_alpha += (1.0f - option_alpha) * (mix * 0.4f);
                if (option_alpha > 1.0f) {
                    option_alpha = 1.0f;
                }
            }
            renderer_draw_ui_rect(renderer, list_x + 2.0f, item_y + 2.0f, list_w - 4.0f, option_height - 4.0f, r, g, b, option_alpha);
            renderer_draw_ui_text(renderer, list_x + 18.0f, item_y + option_height * 0.5f - 8.0f, mode_names[i], 0.95f, 0.95f, 0.98f, 0.96f);

            if (pressed) {
                state->graphics_mode = (PlatformWindowMode)i;
                state->graphics_mode_dropdown_open = false;
                if (result) {
                    result->graphics_changed = true;
                    result->graphics_mode = state->graphics_mode;
                    if (resolutions && resolution_count > 0) {
                        const PreferencesResolution *res = &resolutions[state->graphics_resolution_index % resolution_count];
                        result->graphics_width = res->width;
                        result->graphics_height = res->height;
                    } else if (context && context->resolution_width && context->resolution_height) {
                        result->graphics_width = *context->resolution_width;
                        result->graphics_height = *context->resolution_height;
                    } else {
                        result->graphics_width = 1920;
                        result->graphics_height = 1080;
                    }
                }
                settings_interaction_consume(state);
                break;
            }
        }

        if (input && input->mouse_left_pressed) {
            bool inside_header = point_in_rect(mx, my, mode_header_x, mode_header_y, mode_header_w, mode_header_h);
            bool inside_list = point_in_rect(mx, my, list_x, list_y, list_w, list_h);
            if (!inside_header && !inside_list) {
                state->graphics_mode_dropdown_open = false;
                settings_interaction_consume(state);
            }
        }

        return;
    }

    row_y += row_height + 12.0f;
    if (resolutions && resolution_count > 0) {
        uint32_t context_width = (context && context->resolution_width) ? *context->resolution_width : 0U;
        uint32_t context_height = (context && context->resolution_height) ? *context->resolution_height : 0U;
        bool context_matches_any = false;

        if (context_width > 0U && context_height > 0U) {
            size_t match = settings_find_resolution_index(resolutions,
                                                          resolution_count,
                                                          context_width,
                                                          context_height);
            if (match < resolution_count &&
                resolutions[match].width == context_width &&
                resolutions[match].height == context_height) {
                state->graphics_resolution_index = match;
                context_matches_any = true;
            }
        }

        if (state->graphics_resolution_index >= resolution_count) {
            state->graphics_resolution_index = resolution_count - 1;
        }

        const PreferencesResolution *current = &resolutions[state->graphics_resolution_index];
        char fallback_label[64] = {0};
        const char *resolution_label = NULL;
        bool matches_context = context_matches_any &&
                               current &&
                               current->width == context_width &&
                               current->height == context_height;
        if (matches_context && current->label) {
            resolution_label = current->label;
        } else if (context_width > 0U && context_height > 0U) {
            settings_format_resolution(fallback_label, sizeof(fallback_label), context_width, context_height);
            resolution_label = fallback_label[0] != '\0' ? fallback_label : "Custom";
        } else if (current && current->label) {
            resolution_label = current->label;
        } else {
            resolution_label = "Custom";
        }

        size_t visible_capacity = resolution_count < 8 ? resolution_count : 8;
        if (visible_capacity > max_visible_rows) {
            visible_capacity = max_visible_rows;
        }
        const size_t max_offset = (resolution_count > visible_capacity) ? (resolution_count - visible_capacity) : 0;

        bool res_toggled = settings_dropdown_header(state,
                                                    renderer,
                                                    input,
                                                    x,
                                                    row_y,
                                                    row_width,
                                                    row_height,
                                                    "Resolution",
                                                    resolution_label,
                                                    state->graphics_resolution_dropdown_open);
        if (res_toggled) {
            bool now_open = !state->graphics_resolution_dropdown_open;
            state->graphics_resolution_dropdown_open = now_open;
            if (now_open) {
                state->graphics_mode_dropdown_open = false;
                size_t current_index = state->graphics_resolution_index;
                if (visible_capacity > 0 && resolution_count > visible_capacity) {
                    size_t desired = current_index > 0 ? current_index : 0;
                    if (desired >= visible_capacity) {
                        desired = current_index + 1 > visible_capacity ? current_index + 1 - visible_capacity : 0;
                    } else {
                        desired = 0;
                    }
                    if (desired > max_offset) {
                        desired = max_offset;
                    }
                    state->graphics_resolution_scroll_offset = desired;
                } else {
                    state->graphics_resolution_scroll_offset = 0;
                }
            }
        }

        if (state->graphics_resolution_scroll_offset > max_offset) {
            state->graphics_resolution_scroll_offset = max_offset;
        }

        if (state->graphics_resolution_dropdown_open && visible_capacity > 0) {
            const float option_height = row_height;
            const float list_x = x;
            const float list_y = row_y + row_height;
            const float list_w = row_width;
            float list_h = option_height * (float)visible_capacity;
            const float scrollbar_width = 12.0f;
            const bool scrollbar_needed = resolution_count > visible_capacity;
            const float options_w = scrollbar_needed ? (list_w - scrollbar_width - 4.0f) : list_w;

            settings_interaction_capture(state, x, row_y, row_width, row_height + list_h);

            renderer_draw_ui_rect(renderer, list_x, list_y, list_w, list_h, 0.10f, 0.10f, 0.14f, 0.94f);

            size_t scroll_offset = state->graphics_resolution_scroll_offset;
            if (input && input->mouse_wheel != 0.0f) {
                bool over_list = point_in_rect(mx, my, list_x, list_y, list_w, list_h);
                if (over_list) {
                    if (input->mouse_wheel > 0.1f && scroll_offset > 0) {
                        --scroll_offset;
                    } else if (input->mouse_wheel < -0.1f && scroll_offset < max_offset) {
                        ++scroll_offset;
                    }
                }
            }

            if (scrollbar_needed) {
                const float track_x = list_x + list_w - scrollbar_width - 2.0f;
                const float track_y = list_y + 2.0f;
                const float track_h = list_h - 4.0f;
                renderer_draw_ui_rect(renderer, track_x, track_y, scrollbar_width, track_h, 0.08f, 0.08f, 0.12f, 0.85f);

                if (input && input->mouse_left_down) {
                    if (point_in_rect(mx, my, track_x, track_y, scrollbar_width, track_h)) {
                        float rel = (my - track_y) / track_h;
                        if (rel < 0.0f) {
                            rel = 0.0f;
                        }
                        if (rel > 1.0f) {
                            rel = 1.0f;
                        }
                        size_t new_offset = (size_t)(rel * (float)max_offset + 0.5f);
                        if (new_offset > max_offset) {
                            new_offset = max_offset;
                        }
                        scroll_offset = new_offset;
                    }
                }

                const float track_range = track_h;
                float knob_h = (float)visible_capacity / (float)resolution_count * track_h;
                if (knob_h < 14.0f) {
                    knob_h = 14.0f;
                }
                if (knob_h > track_h) {
                    knob_h = track_h;
                }
                float knob_y = track_y;
                if (max_offset > 0) {
                    knob_y = track_y + ((float)scroll_offset / (float)max_offset) * (track_range - knob_h);
                }
                renderer_draw_ui_rect(renderer, track_x + 2.0f, knob_y, scrollbar_width - 4.0f, knob_h, 0.28f, 0.28f, 0.34f, 0.92f);
            }

            state->graphics_resolution_scroll_offset = scroll_offset;

            size_t first = scroll_offset;
            size_t visible = visible_capacity;
            for (size_t i = 0; i < visible; ++i) {
                size_t idx = first + i;
                if (idx >= resolution_count) {
                    break;
                }
                const PreferencesResolution *entry = &resolutions[idx];
                float item_y = list_y + option_height * (float)i;
                bool hovered = point_in_rect(mx, my, list_x, item_y, options_w, option_height);
                bool pressed = hovered && input && input->mouse_left_pressed;
                bool selected = context_matches_any &&
                                 entry->width == context_width &&
                                 entry->height == context_height;
                float base = selected ? 0.32f : 0.18f;
                float r = base;
                float g = base * 0.85f;
                float b = base * 0.7f;
                settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
                if (selected) {
                    if (r < 0.32f) {
                        r = 0.32f;
                    }
                    if (g < 0.28f) {
                        g = 0.28f;
                    }
                    if (b < 0.24f) {
                        b = 0.24f;
                    }
                }
                float option_alpha = selected ? 0.98f : 0.92f;
                if (hovered && !pressed) {
                    float mix = settings_hover_mix();
                    option_alpha += (1.0f - option_alpha) * (mix * 0.4f);
                    if (option_alpha > 1.0f) {
                        option_alpha = 1.0f;
                    }
                }
                renderer_draw_ui_rect(renderer, list_x + 2.0f, item_y + 2.0f, options_w - 4.0f, option_height - 4.0f, r, g, b, option_alpha);
                renderer_draw_ui_text(renderer,
                                      list_x + 18.0f,
                                      item_y + option_height * 0.5f - 8.0f,
                                      entry->label ? entry->label : "Custom",
                                      0.95f,
                                      0.95f,
                                      0.98f,
                                      0.96f);

                if (pressed) {
                    state->graphics_resolution_index = idx;
                    state->graphics_resolution_dropdown_open = false;
                    if (context && context->resolution_width && context->resolution_height) {
                        *context->resolution_width = entry->width;
                        *context->resolution_height = entry->height;
                    }
                    if (result) {
                        result->graphics_changed = true;
                        result->graphics_mode = state->graphics_mode;
                        result->graphics_width = entry->width;
                        result->graphics_height = entry->height;
                    }
                    settings_interaction_consume(state);
                    break;
                }
            }

            if (input && input->mouse_left_pressed) {
                bool inside_header = point_in_rect(mx, my, x, row_y, row_width, row_height);
                bool inside_list = point_in_rect(mx, my, list_x, list_y, list_w, list_h);
                if (!inside_header && !inside_list) {
                    state->graphics_resolution_dropdown_open = false;
                    settings_interaction_consume(state);
                }
            }

            return;
        } else if (!state->graphics_resolution_dropdown_open) {
            state->graphics_resolution_scroll_offset = 0;
        }
    } else {
        renderer_draw_ui_text(renderer,
                              x,
                              row_y,
                              "No preset resolutions available.",
                              0.78f,
                              0.78f,
                              0.84f,
                              0.9f);
    }
}
/* Place ce prototype avant render_audio_tab(...) */
static void settings_render_audio_dropdown(Renderer *renderer,
                                           SettingsMenuState *state,
                                           const SettingsMenuContext *context,
                                           const InputState *input,
                                           float header_x,
                                           float header_y,
                                           float width,
                                           float dropdown_height,
                                           float item_height,
                                           bool is_output,
                                           SettingsMenuResult *result)
{
    if (!renderer || !state) {
        return;
    }

    (void)result;

    const float list_x = header_x;
    const float list_y = header_y + dropdown_height;
    const float list_w = width;
    const float list_h = item_height * 8.0f; /* max visible items = 8 */
    const float scrollbar_width = 12.0f;

    AudioDeviceInfo *devices = is_output ? state->audio_output_devices : state->audio_input_devices;
    size_t device_count = is_output ? state->audio_output_device_count : state->audio_input_device_count;
    size_t *scroll_offset = is_output ? &state->audio_output_scroll_offset : &state->audio_input_scroll_offset;
    size_t *selected_index = is_output ? &state->audio_output_index : &state->audio_input_index;

    if (device_count == 0U) {
        return;
    }

    size_t visible_capacity = device_count < 8U ? device_count : 8U;
    size_t max_offset = (device_count > visible_capacity) ? (device_count - visible_capacity) : 0U;
    if (*scroll_offset > max_offset) {
        *scroll_offset = max_offset;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;

    /* Bloque les interactions ailleurs pendant l’ouverture du menu */
    settings_interaction_capture(state, header_x, header_y, width, dropdown_height + list_h);

    /* Fond de la liste */
    renderer_draw_ui_rect(renderer, list_x, list_y, list_w, list_h, 0.10f, 0.10f, 0.14f, 0.94f);

    const bool scrollbar_needed = device_count > visible_capacity;
    const float options_w = scrollbar_needed ? (list_w - scrollbar_width - 4.0f) : list_w;

    /* Molette */
    if (input && input->mouse_wheel != 0.0f) {
        bool over_list = point_in_rect(mx, my, list_x, list_y, list_w, list_h);
        if (over_list) {
            if (input->mouse_wheel > 0.1f && *scroll_offset > 0) {
                --(*scroll_offset);
            } else if (input->mouse_wheel < -0.1f && *scroll_offset < max_offset) {
                ++(*scroll_offset);
            }
        }
    }

    /* Scrollbar */
    if (scrollbar_needed) {
        const float track_x = list_x + list_w - scrollbar_width - 2.0f;
        const float track_y = list_y + 2.0f;
        const float track_h = list_h - 4.0f;
        renderer_draw_ui_rect(renderer, track_x, track_y, scrollbar_width, track_h, 0.08f, 0.08f, 0.12f, 0.85f);

        if (input && input->mouse_left_down && point_in_rect(mx, my, track_x, track_y, scrollbar_width, track_h)) {
            float rel = (my - track_y) / track_h;
            if (rel < 0.0f) rel = 0.0f;
            if (rel > 1.0f) rel = 1.0f;
            size_t new_offset = (size_t)(rel * (float)max_offset + 0.5f);
            if (new_offset > max_offset) new_offset = max_offset;
            *scroll_offset = new_offset;
        }

        const float track_range = track_h;
        float knob_h = (float)visible_capacity / (float)device_count * track_h;
        if (knob_h < 14.0f) knob_h = 14.0f;
        if (knob_h > track_h) knob_h = track_h;
        float knob_y = track_y;
        if (max_offset > 0) {
            knob_y = track_y + ((float)(*scroll_offset) / (float)max_offset) * (track_range - knob_h);
        }
        renderer_draw_ui_rect(renderer, track_x + 2.0f, knob_y, scrollbar_width - 4.0f, knob_h, 0.28f, 0.28f, 0.34f, 0.92f);
    }

    /* Items visibles */
    size_t first = *scroll_offset;
    size_t visible = visible_capacity;
    for (size_t i = 0; i < visible; ++i) {
        size_t idx = first + i;
        if (idx >= device_count) break;

        const AudioDeviceInfo *entry = &devices[idx];
        float item_y = list_y + item_height * (float)i;
        bool hovered = point_in_rect(mx, my, list_x, item_y, options_w, item_height);
        bool pressed = hovered && input && input->mouse_left_pressed;
        bool selected = idx == *selected_index;
        float base = selected ? 0.32f : 0.18f;
        float r = base;
        float g = base * 0.85f;
        float b = base * 0.7f;
        settings_apply_interaction_tint(&r, &g, &b, hovered, pressed);
        if (selected) {
            if (r < 0.32f) r = 0.32f;
            if (g < 0.28f) g = 0.28f;
            if (b < 0.24f) b = 0.24f;
        }
        float option_alpha = selected ? 0.98f : 0.92f;
        if (hovered && !pressed) {
            float mix = settings_hover_mix();
            option_alpha += (1.0f - option_alpha) * (mix * 0.4f);
            if (option_alpha > 1.0f) {
                option_alpha = 1.0f;
            }
        }

        renderer_draw_ui_rect(renderer, list_x + 2.0f, item_y + 2.0f, options_w - 4.0f, item_height - 4.0f,
                              r, g, b, option_alpha);
        renderer_draw_ui_text(renderer, list_x + 18.0f, item_y + item_height * 0.5f - 8.0f,
                              entry->name ? entry->name : "Unknown Device",
                              0.95f, 0.95f, 0.98f, 0.96f);

        if (pressed) {
            /* Sélection */
            *selected_index = idx;

            if (is_output && context && context->audio_output_device) {
                *context->audio_output_device = entry->id;
            } else if (!is_output && context && context->audio_input_device) {
                *context->audio_input_device = entry->id;
            }

            /* Petit feedback utilisateur (barre en bas du panneau) */
            if (entry->name && entry->name[0] != '\0') {
                char msg[192];
                snprintf(msg, sizeof(msg), "%s device set to: %s",
                         is_output ? "Output" : "Input",
                         entry->name);
                settings_record_feedback(state, msg, INPUT_ACTION_COUNT, PLATFORM_KEY_UNKNOWN);
            } else {
                settings_record_feedback(state,
                                         is_output ? "Output device changed" : "Input device changed",
                                         INPUT_ACTION_COUNT,
                                         PLATFORM_KEY_UNKNOWN);
            }
            
            /* Ferme le menu */
            if (is_output) state->audio_output_dropdown_open = false;
            else state->audio_input_dropdown_open = false;

            settings_interaction_consume(state);
            break;
        }
    }

    /* Fermer si clic à l’extérieur */
    if (input && input->mouse_left_pressed) {
        bool inside_header = point_in_rect(mx, my, header_x, header_y, width, dropdown_height);
        bool inside_list = point_in_rect(mx, my, list_x, list_y, list_w, list_h);
        if (!inside_header && !inside_list) {
            if (is_output) state->audio_output_dropdown_open = false;
            else state->audio_input_dropdown_open = false;
            settings_interaction_consume(state);
        }
    }
}


/* Version complète corrigée */
static void render_audio_tab(SettingsMenuState *state,
                             const SettingsMenuContext *context,
                             Renderer *renderer,
                             const InputState *input,
                             float x,
                             float y,
                             float width,
                             SettingsMenuResult *result)
{
    if (!state || !renderer) {
        return;
    }

    const float row_height = 60.0f;
    const float row_spacing = 14.0f;
    const float dropdown_height = 44.0f;
    const float dropdown_item_height = 36.0f;

    float row_y = y;
    float output_header_y = -1.0f;
    float input_header_y = -1.0f;

    bool have_volume_controls = context &&
                                 context->master_volume &&
                                 context->music_volume &&
                                 context->effects_volume &&
                                 context->voice_volume &&
                                 context->microphone_volume;

    if (!have_volume_controls) {
        renderer_draw_ui_text(renderer,
                              x,
                              row_y,
                              "Audio preferences are not available.",
                              0.82f,
                              0.82f,
                              0.9f,
                              0.9f);
        return;
    }

    state->audio_initialized = true;

    state->audio_output_device_count =
        audio_enumerate_output_devices(state->audio_output_devices, SETTINGS_MENU_MAX_AUDIO_DEVICES);
    state->audio_input_device_count =
        audio_enumerate_input_devices(state->audio_input_devices, SETTINGS_MENU_MAX_AUDIO_DEVICES);

    if (state->audio_output_device_count == 0U) {
        state->audio_output_index = 0U;
        state->audio_output_scroll_offset = 0U;
    } else {
        uint32_t desired = UINT32_MAX;
        if (context && context->audio_output_device) {
            desired = *context->audio_output_device;
        }
        size_t selected = 0U;
        if (desired != UINT32_MAX) {
            bool found = false;
            for (size_t i = 0; i < state->audio_output_device_count; ++i) {
                if (state->audio_output_devices[i].id == desired) {
                    selected = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                selected = 0U;
            }
        }
        state->audio_output_index = selected;
        if (context && context->audio_output_device) {
            *context->audio_output_device = state->audio_output_devices[selected].id;
        }
        if (state->audio_output_scroll_offset >= state->audio_output_device_count) {
            state->audio_output_scroll_offset = 0U;
        }
    }

    if (state->audio_input_device_count == 0U) {
        state->audio_input_index = 0U;
        state->audio_input_scroll_offset = 0U;
    } else {
        uint32_t desired = UINT32_MAX;
        if (context && context->audio_input_device) {
            desired = *context->audio_input_device;
        }
        size_t selected = 0U;
        if (desired != UINT32_MAX) {
            bool found = false;
            for (size_t i = 0; i < state->audio_input_device_count; ++i) {
                if (state->audio_input_devices[i].id == desired) {
                    selected = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                selected = 0U;
            }
        }
        state->audio_input_index = selected;
        if (context && context->audio_input_device) {
            *context->audio_input_device = state->audio_input_devices[selected].id;
        }
        if (state->audio_input_scroll_offset >= state->audio_input_device_count) {
            state->audio_input_scroll_offset = 0U;
        }
    }

    if (context && context->audio_output_device) {
        float header_y = row_y;
        output_header_y = header_y;
        const char *device_name = "Unavailable";
        if (state->audio_output_device_count > 0U) {
            size_t idx = state->audio_output_index;
            if (idx >= state->audio_output_device_count) {
                idx = 0U;
            }
            device_name = state->audio_output_devices[idx].name;
        }

        bool toggled = settings_dropdown_header(state,
                                                renderer,
                                                input,
                                                x,
                                                header_y,
                                                width,
                                                dropdown_height,
                                                "Output Device",
                                                device_name,
                                                state->audio_output_dropdown_open);
        if (toggled) {
            bool now_open = !state->audio_output_dropdown_open;
            state->audio_output_dropdown_open = now_open;
            if (now_open) {
                state->audio_input_dropdown_open = false;
                state->graphics_mode_dropdown_open = false;
                state->graphics_resolution_dropdown_open = false;
                size_t visible_capacity =
                    state->audio_output_device_count < 8U ? state->audio_output_device_count : 8U;
                if (visible_capacity > 0U && state->audio_output_device_count > visible_capacity) {
                    size_t selected = state->audio_output_index;
                    if (selected >= visible_capacity) {
                        size_t desired_offset =
                            selected + 1U > visible_capacity ? selected + 1U - visible_capacity : 0U;
                        size_t max_offset = state->audio_output_device_count - visible_capacity;
                        if (desired_offset > max_offset) {
                            desired_offset = max_offset;
                        }
                        state->audio_output_scroll_offset = desired_offset;
                    } else {
                        state->audio_output_scroll_offset = 0U;
                    }
                } else {
                    state->audio_output_scroll_offset = 0U;
                }
            }
        }

        row_y += dropdown_height + row_spacing;
    } else {
        state->audio_output_dropdown_open = false;
    }

    if (context && context->audio_input_device) {
        float header_y = row_y;
        input_header_y = header_y;
        const char *device_name = "Unavailable";
        if (state->audio_input_device_count > 0U) {
            size_t idx = state->audio_input_index;
            if (idx >= state->audio_input_device_count) {
                idx = 0U;
            }
            device_name = state->audio_input_devices[idx].name;
        }

        bool toggled = settings_dropdown_header(state,
                                                renderer,
                                                input,
                                                x,
                                                header_y,
                                                width,
                                                dropdown_height,
                                                "Input Device",
                                                device_name,
                                                state->audio_input_dropdown_open);
        if (toggled) {
            bool now_open = !state->audio_input_dropdown_open;
            state->audio_input_dropdown_open = now_open;
            if (now_open) {
                state->audio_output_dropdown_open = false;
                state->graphics_mode_dropdown_open = false;
                state->graphics_resolution_dropdown_open = false;
                size_t visible_capacity =
                    state->audio_input_device_count < 8U ? state->audio_input_device_count : 8U;
                if (visible_capacity > 0U && state->audio_input_device_count > visible_capacity) {
                    size_t selected = state->audio_input_index;
                    if (selected >= visible_capacity) {
                        size_t desired_offset =
                            selected + 1U > visible_capacity ? selected + 1U - visible_capacity : 0U;
                        size_t max_offset = state->audio_input_device_count - visible_capacity;
                        if (desired_offset > max_offset) {
                            desired_offset = max_offset;
                        }
                        state->audio_input_scroll_offset = desired_offset;
                    } else {
                        state->audio_input_scroll_offset = 0U;
                    }
                } else {
                    state->audio_input_scroll_offset = 0U;
                }
            }
        }

        row_y += dropdown_height + row_spacing;
    } else {
        state->audio_input_dropdown_open = false;
    }
    /* --- BLOQUER LES INTERACTIONS DE FOND QUAND UN DROPDOWN AUDIO EST OUVERT --- */
    if (state->audio_output_dropdown_open && output_header_y >= 0.0f) {
        size_t device_count = state->audio_output_device_count;
        size_t visible_capacity = device_count < 8U ? device_count : 8U;
        float list_h = (float)visible_capacity * dropdown_item_height;
        if (list_h < dropdown_item_height) list_h = dropdown_item_height;
        settings_interaction_capture(state,
                                     x,
                                     output_header_y,
                                     width,
                                     dropdown_height + list_h);
    } else if (state->audio_input_dropdown_open && input_header_y >= 0.0f) {
        size_t device_count = state->audio_input_device_count;
        size_t visible_capacity = device_count < 8U ? device_count : 8U;
        float list_h = (float)visible_capacity * dropdown_item_height;
        if (list_h < dropdown_item_height) list_h = dropdown_item_height;
        settings_interaction_capture(state,
                                     x,
                                     input_header_y,
                                     width,
                                     dropdown_height + list_h);
    }

    if (context->master_volume) {
        bool changed = settings_slider(state,
                                       renderer,
                                       input,
                                       x,
                                       row_y,
                                       width,
                                       row_height,
                                       "Master Volume",
                                       0.0f,
                                       1.0f,
                                       0.01f,
                                       100.0f,
                                       context->master_volume,
                                       "%.0f%%");
        if (changed && result) {
            result->master_volume_changed = true;
            result->master_volume = *context->master_volume;
        }
        row_y += row_height + row_spacing;
    }

    if (context->music_volume) {
        bool changed = settings_slider(state,
                                       renderer,
                                       input,
                                       x,
                                       row_y,
                                       width,
                                       row_height,
                                       "Music Volume",
                                       0.0f,
                                       1.0f,
                                       0.01f,
                                       100.0f,
                                       context->music_volume,
                                       "%.0f%%");
        if (changed && result) {
            result->music_volume_changed = true;
            result->music_volume = *context->music_volume;
        }
        row_y += row_height + row_spacing;
    }

    if (context->effects_volume) {
        bool changed = settings_slider(state,
                                       renderer,
                                       input,
                                       x,
                                       row_y,
                                       width,
                                       row_height,
                                       "Effects Volume",
                                       0.0f,
                                       1.0f,
                                       0.01f,
                                       100.0f,
                                       context->effects_volume,
                                       "%.0f%%");
        if (changed && result) {
            result->effects_volume_changed = true;
            result->effects_volume = *context->effects_volume;
        }
        row_y += row_height + row_spacing;
    }

    if (context->voice_volume) {
        bool changed = settings_slider(state,
                                       renderer,
                                       input,
                                       x,
                                       row_y,
                                       width,
                                       row_height,
                                       "Voice Chat Volume",
                                       0.0f,
                                       1.0f,
                                       0.01f,
                                       100.0f,
                                       context->voice_volume,
                                       "%.0f%%");
        if (changed && result) {
            result->voice_volume_changed = true;
            result->voice_volume = *context->voice_volume;
        }
        row_y += row_height + row_spacing;
    }

    if (context->microphone_volume) {
        bool changed = settings_slider(state,
                                       renderer,
                                       input,
                                       x,
                                       row_y,
                                       width,
                                       row_height,
                                       "Microphone Gain",
                                       0.0f,
                                       1.0f,
                                       0.01f,
                                       100.0f,
                                       context->microphone_volume,
                                       "%.0f%%");
        if (changed && result) {
            result->microphone_volume_changed = true;
            result->microphone_volume = *context->microphone_volume;
        }
        row_y += row_height + row_spacing;
    }

    if (context && context->voice_activation_mode) {
        bool push_to_talk = (*context->voice_activation_mode != PREFERENCES_VOICE_VOICE_DETECTION);
        bool mode_changed = settings_toggle(state,
                                            renderer,
                                            input,
                                            x,
                                            row_y,
                                            width * 0.6f,
                                            44.0f,
                                            "Push-To-Talk",
                                            &push_to_talk);
        if (mode_changed) {
            *context->voice_activation_mode = push_to_talk ? PREFERENCES_VOICE_PUSH_TO_TALK : PREFERENCES_VOICE_VOICE_DETECTION;
            if (result) {
                result->voice_mode_changed = true;
                result->voice_mode = *context->voice_activation_mode;
            }
        }
        row_y += 54.0f;

        if (*context->voice_activation_mode == PREFERENCES_VOICE_VOICE_DETECTION && context->voice_activation_threshold_db) {
            float min_db = -80.0f;
            float max_db = -10.0f;
            if (*context->voice_activation_threshold_db < min_db) {
                *context->voice_activation_threshold_db = min_db;
            }
            if (*context->voice_activation_threshold_db > max_db) {
                *context->voice_activation_threshold_db = max_db;
            }

            bool threshold_changed = settings_slider(state,
                                                     renderer,
                                                     input,
                                                     x,
                                                     row_y,
                                                     width,
                                                     row_height,
                                                     "Voice Activation Threshold",
                                                     min_db,
                                                     max_db,
                                                     1.0f,
                                                     1.0f,
                                                     context->voice_activation_threshold_db,
                                                     "%.0f dB");
            if (threshold_changed && result) {
                result->voice_threshold_changed = true;
                result->voice_activation_threshold_db = *context->voice_activation_threshold_db;
            }
            row_y += row_height + 8.0f;
            renderer_draw_ui_text(renderer,
                                  x,
                                  row_y,
                                  "Lower values capture quieter speech.",
                                  0.78f,
                                  0.78f,
                                  0.86f,
                                  0.9f);
            row_y += row_spacing;
        } else {
            renderer_draw_ui_text(renderer,
                                  x,
                                  row_y,
                                  "Configure the Push-To-Talk key in the Controls tab.",
                                  0.78f,
                                  0.78f,
                                  0.86f,
                                  0.9f);
            row_y += 32.0f;
        }
    }

    /* --- DROPDOWNS : doivent être rendus à la fin, mais à l'intérieur de la fonction --- */
    if (state->audio_output_dropdown_open && state->audio_output_device_count > 0U && output_header_y >= 0.0f) {
        settings_render_audio_dropdown(renderer,
                                       state,
                                       context,
                                       input,
                                       x,
                                       output_header_y,
                                       width,
                                       dropdown_height,
                                       dropdown_item_height,
                                       true,
                                       result);
    }
    if (state->audio_input_dropdown_open && state->audio_input_device_count > 0U && input_header_y >= 0.0f) {
        settings_render_audio_dropdown(renderer,
                                       state,
                                       context,
                                       input,
                                       x,
                                       input_header_y,
                                       width,
                                       dropdown_height,
                                       dropdown_item_height,
                                       false,
                                       result);
    }
}



static void render_accessibility_tab(SettingsMenuState *state,
                                     const SettingsMenuContext *context,
                                     Renderer *renderer,
                                     const InputState *input,
                                     float x,
                                     float y,
                                     float width,
                                     SettingsMenuResult *result)
{
    float entry_y = y;

    if (context && context->show_fps_overlay) {
        bool changed = settings_toggle(state,
                                       renderer,
                                       input,
                                       x,
                                       entry_y,
                                       width * 0.6f,
                                       44.0f,
                                       "FPS Overlay",
                                       context->show_fps_overlay);
        if (changed && result) {
            result->show_fps_overlay_changed = true;
        }
        entry_y += 54.0f;
    }

    if (context && context->view_bobbing) {
        bool changed = settings_toggle(state,
                                       renderer,
                                       input,
                                       x,
                                       entry_y,
                                       width * 0.6f,
                                       44.0f,
                                       "View Bobbing",
                                       context->view_bobbing);
        if (changed && result) {
            result->view_bobbing_changed = true;
        }
        entry_y += 54.0f;
    }

    if (context && context->double_jump) {
        bool changed = settings_toggle(state,
                                       renderer,
                                       input,
                                       x,
                                       entry_y,
                                       width * 0.6f,
                                       44.0f,
                                       "Double Jump",
                                       context->double_jump);
        if (changed && result) {
            result->double_jump_changed = true;
        }
        entry_y += 54.0f;
    }

    if (entry_y == y) {
        renderer_draw_ui_text(renderer,
                              x,
                              y,
                              "Accessibility toggles will unlock as new features mature.",
                              0.82f,
                              0.82f,
                              0.9f,
                              0.92f);
    }
}

SettingsMenuResult settings_menu_render(SettingsMenuState *state,
                                        const SettingsMenuContext *context,
                                        Renderer *renderer,
                                        const InputState *input,
                                        double time_seconds)
{
    SettingsMenuResult result;
    memset(&result, 0, sizeof(result));

    if (!state || !renderer) {
        return result;
    }

    g_settings_ui_time = time_seconds;

    SettingsMenuContext empty_context = {0};
    if (!context) {
        context = &empty_context;
    }

    settings_interaction_begin_frame(state);

    if (!state->last_initialized) {
        if (context->show_fps_overlay) {
            state->last_show_fps_overlay = *context->show_fps_overlay;
        }
        if (context->view_bobbing) {
            state->last_view_bobbing = *context->view_bobbing;
        }
        if (context->double_jump) {
            state->last_double_jump = *context->double_jump;
        }
        state->last_initialized = true;
    }

    if (state->waiting_for_rebind) {
        PlatformKey candidate = input_first_pressed_key(input);
        if (candidate != PLATFORM_KEY_UNKNOWN) {
            if (candidate == PLATFORM_KEY_ESCAPE) {
                settings_menu_cancel_rebind(state);
            } else {
                input_binding_set(state->pending_action, candidate);
                settings_record_feedback(state, NULL, state->pending_action, candidate);
                result.binding_changed = true;
                result.binding_changed_action = state->pending_action;
                result.binding_new_key = candidate;
                settings_menu_cancel_rebind(state);
            }
        }
    }

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    float panel_width = width - SETTINGS_PANEL_MARGIN * 2.0f;
    float panel_height = height - SETTINGS_PANEL_MARGIN * 2.0f;

    if (panel_width > 960.0f) {
        panel_width = 960.0f;
    }
    if (panel_width < 420.0f) {
        panel_width = width * 0.96f;
    }

    if (panel_height > height - 60.0f) {
        panel_height = height - 60.0f;
    }
    if (panel_height < 360.0f) {
        panel_height = height * 0.9f;
    }

    float panel_x = (width - panel_width) * 0.5f;
    float panel_y = (height - panel_height) * 0.5f;

    renderer_draw_ui_rect(renderer, panel_x - 12.0f, panel_y - 12.0f, panel_width + 24.0f, panel_height + 24.0f, 0.03f, 0.03f, 0.05f, 0.82f);
    renderer_draw_ui_rect(renderer, panel_x, panel_y, panel_width, panel_height, 0.05f, 0.05f, 0.08f, 0.94f);

    const char *title = context->in_game ? "In-Game Settings" : "Settings";
    renderer_draw_ui_text(renderer, panel_x + SETTINGS_CONTENT_PADDING, panel_y + 30.0f, title, 0.96f, 0.96f, 0.99f, 1.0f);

    const char *subtitle = context->in_game ? "Tune the experience without leaving your session." : "Adjust preferences before diving into a match.";
    renderer_draw_ui_text(renderer, panel_x + SETTINGS_CONTENT_PADDING, panel_y + 58.0f, subtitle, 0.78f, 0.78f, 0.86f, 0.88f);

    const float tabs_y = panel_y + 92.0f;
    const float tabs_width = panel_width - SETTINGS_CONTENT_PADDING * 2.0f;
    const float tab_button_width = (tabs_width - (SETTINGS_MENU_CATEGORY_COUNT - 1) * SETTINGS_TABS_SPACING) / (float)SETTINGS_MENU_CATEGORY_COUNT;
    float tab_x = panel_x + SETTINGS_CONTENT_PADDING;

    static const char *tab_titles[SETTINGS_MENU_CATEGORY_COUNT] = {
        "Graphics",
        "Controls",
        "Audio",
        "Accessibility"
    };

    for (int tab = 0; tab < SETTINGS_MENU_CATEGORY_COUNT; ++tab) {
        bool pressed = settings_tab_button(state,
                                           renderer,
                                           input,
                                           tab_x,
                                           tabs_y,
                                           tab_button_width,
                                           SETTINGS_TABS_HEIGHT,
                                           tab_titles[tab],
                                           state->active_category == (SettingsMenuCategory)tab);
        if (pressed) {
            state->active_category = (SettingsMenuCategory)tab;
            if (state->waiting_for_rebind) {
                settings_menu_cancel_rebind(state);
            }
        }
        tab_x += tab_button_width + SETTINGS_TABS_SPACING;
    }

    const float content_x = panel_x + SETTINGS_CONTENT_PADDING;
    const float content_y = tabs_y + SETTINGS_TABS_HEIGHT + 34.0f;
    const float content_width = panel_width - SETTINGS_CONTENT_PADDING * 2.0f;
    const float content_height = panel_y + panel_height - content_y - 92.0f;

    const float back_height = 44.0f;
    const float back_x = content_x;
    const float back_y = panel_y + panel_height - back_height - 32.0f;
    bool dropdown_open = state->graphics_mode_dropdown_open ||
                         state->graphics_resolution_dropdown_open ||
                         state->audio_output_dropdown_open ||
                         state->audio_input_dropdown_open;
    bool dropdown_pointer_inside = false;
    if (dropdown_open && state->interaction_locked && input) {
        float mx_back = (float)input->mouse_x;
        float my_back = (float)input->mouse_y;
        dropdown_pointer_inside = settings_interaction_pointer_inside(state, mx_back, my_back);
    }
    const InputState *back_input = (dropdown_open && dropdown_pointer_inside) ? NULL : input;
    bool back_pressed = settings_button(state,
                                        renderer,
                                        back_input,
                                        back_x,
                                        back_y,
                                        content_width,
                                        back_height,
                                        context->in_game ? "Return" : "Back",
                                        false);
    if (back_pressed) {
        result.back_requested = true;
        settings_menu_cancel_rebind(state);
        state->feedback_frames = 0;
        state->feedback_has_message = false;
    }

    switch (state->active_category) {
    case SETTINGS_MENU_CATEGORY_GRAPHICS:
        render_graphics_tab(state, context, renderer, input, content_x, content_y, content_width, &result);
        break;
    case SETTINGS_MENU_CATEGORY_CONTROLS:
        render_controls_tab(state, renderer, input, content_x, content_y, content_width, content_height, &result);
        break;
    case SETTINGS_MENU_CATEGORY_AUDIO:
        render_audio_tab(state, context, renderer, input, content_x, content_y, content_width, &result);
        break;
    case SETTINGS_MENU_CATEGORY_ACCESSIBILITY:
        render_accessibility_tab(state, context, renderer, input, content_x, content_y, content_width, &result);
        break;
    default:
        break;
    }

    if (state->feedback_frames > 0) {
        const float feedback_y = panel_y + panel_height - 86.0f;
        if (state->feedback_has_message) {
            renderer_draw_ui_text(renderer,
                                  content_x,
                                  feedback_y,
                                  state->feedback_message,
                                  0.82f,
                                  0.82f,
                                  0.9f,
                                  0.92f);
        } else if (state->feedback_action < INPUT_ACTION_COUNT && state->feedback_key < PLATFORM_KEY_COUNT) {
            char buffer[128];
            snprintf(buffer,
                     sizeof(buffer),
                     "%s bound to %s",
                     input_action_display_name(state->feedback_action),
                     input_key_display_name(state->feedback_key));
            renderer_draw_ui_text(renderer,
                                  content_x,
                                  feedback_y,
                                  buffer,
                                  0.82f,
                                  0.82f,
                                  0.9f,
                                  0.92f);
        }
        --state->feedback_frames;
    }

    if (context->show_fps_overlay) {
        if (*context->show_fps_overlay != state->last_show_fps_overlay) {
            result.show_fps_overlay_changed = true;
        }
        state->last_show_fps_overlay = *context->show_fps_overlay;
    }
    if (context->view_bobbing) {
        if (*context->view_bobbing != state->last_view_bobbing) {
            result.view_bobbing_changed = true;
        }
        state->last_view_bobbing = *context->view_bobbing;
    }
    if (context->double_jump) {
        if (*context->double_jump != state->last_double_jump) {
            result.double_jump_changed = true;
        }
        state->last_double_jump = *context->double_jump;
    }
    return result;
}







