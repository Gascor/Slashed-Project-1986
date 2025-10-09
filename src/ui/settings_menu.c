#include "engine/settings_menu.h"

#include "engine/renderer.h"

#include <stdio.h>
#include <string.h>

#define SETTINGS_PANEL_MARGIN            36.0f
#define SETTINGS_TABS_HEIGHT             42.0f
#define SETTINGS_TABS_SPACING            12.0f
#define SETTINGS_CONTENT_PADDING         32.0f
#define SETTINGS_ROW_HEIGHT_WIDE         48.0f
#define SETTINGS_ROW_HEIGHT_NARROW       76.0f
#define SETTINGS_LIST_HEADER_HEIGHT      30.0f
#define SETTINGS_LIST_SPACING            10.0f
#define SETTINGS_DEFAULT_FEEDBACK_FRAMES 180

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
        strncpy(state->feedback_message, message, sizeof(state->feedback_message) - 1);
        state->feedback_message[sizeof(state->feedback_message) - 1] = '\0';
    } else {
        state->feedback_has_message = false;
        state->feedback_message[0] = '\0';
    }
}

static bool settings_button(Renderer *renderer,
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
    bool hovered = input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = highlighted ? 0.28f : (hovered ? 0.22f : 0.12f);
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.85f, base * 0.7f, highlighted ? 0.95f : 0.88f);
    renderer_draw_ui_text(renderer, x + 20.0f, y + height * 0.5f - 8.0f, label, 0.96f, 0.96f, 0.98f, 1.0f);
    return pressed;
}

static bool settings_tab_button(Renderer *renderer,
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
    bool hovered = input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = active ? 0.32f : (hovered ? 0.20f : 0.14f);
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.82f, base * 0.68f, active ? 0.95f : 0.9f);
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

static bool settings_toggle(Renderer *renderer,
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
    bool hovered = input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = *value ? 0.25f : 0.10f;
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.85f, base * 0.7f, 0.88f);

    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s: %s", label, *value ? "ON" : "OFF");
    renderer_draw_ui_text(renderer, x + 16.0f, y + height * 0.5f - 8.0f, buffer, 0.96f, 0.96f, 0.98f, 0.98f);

    if (pressed) {
        *value = !*value;
        return true;
    }
    return false;
}

static bool settings_binding_button(Renderer *renderer,
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
    bool hovered = !disabled && input && point_in_rect(mx, my, x, y, width, height);
    bool pressed = hovered && input && input->mouse_left_pressed;

    float base = listening ? 0.32f : (hovered ? 0.22f : 0.14f);
    float alpha = disabled ? 0.55f : 0.9f;
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.82f, base * 0.66f, alpha);
    renderer_draw_ui_text(renderer, x + 16.0f, y + height * 0.5f - 8.0f, label, 0.96f, 0.96f, 0.98f, disabled ? 0.75f : 1.0f);
    return !disabled && pressed;
}

static bool settings_reset_button(Renderer *renderer,
                                  const InputState *input,
                                  float x,
                                  float y,
                                  float width,
                                  float height)
{
    return settings_button(renderer, input, x, y, width, height, "Reset", false);
}

static int settings_cycle_row(Renderer *renderer,
                               const InputState *input,
                               float x,
                               float y,
                               float width,
                               float height,
                               const char *label,
                               const char *value)
{
    if (!renderer || !label || !value) {
        return 0;
    }

    float mx = input ? (float)input->mouse_x : -1000.0f;
    float my = input ? (float)input->mouse_y : -1000.0f;
    bool hovered = input && point_in_rect(mx, my, x, y, width, height);
    bool next = hovered && input && input->mouse_left_pressed;
    bool previous = hovered && input && input->mouse_right_pressed;

    float base = hovered ? 0.24f : 0.14f;
    renderer_draw_ui_rect(renderer, x, y, width, height, base, base * 0.85f, base * 0.72f, hovered ? 0.95f : 0.88f);
    renderer_draw_ui_text(renderer, x + 18.0f, y + height * 0.5f - 8.0f, label, 0.96f, 0.96f, 0.98f, 0.95f);

    float text_width = (float)strlen(value) * 8.0f;
    float value_x = x + width - text_width - 18.0f;
    if (value_x < x + 140.0f) {
        value_x = x + 140.0f;
    }
    renderer_draw_ui_text(renderer, value_x, y + height * 0.5f - 8.0f, value, 0.9f, 0.9f, 0.96f, 0.9f);

    if (next) {
        return 1;
    }
    if (previous) {
        return -1;
    }
    return 0;
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
            bool binding_clicked = settings_binding_button(renderer,
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
            bool reset_clicked = settings_reset_button(renderer,
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

            bool binding_clicked = settings_binding_button(renderer,
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

            bool reset_clicked = settings_reset_button(renderer,
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
    if (settings_button(renderer,
                        input,
                        x,
                        reset_all_y,
                        width,
                        40.0f,
                        "Reset All Controls", false)) {
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
                          "Left click cycles forward, right click cycles backward.",
                          0.68f,
                          0.68f,
                          0.75f,
                          0.86f);

    float row_y = y + 54.0f;
    const float row_height = 46.0f;
    const float row_width = width * 0.7f;

    static const char *const mode_names[] = {
        "Fullscreen",
        "Windowed",
        "Borderless"
    };

    const char *mode_label = mode_names[state->graphics_mode < PLATFORM_WINDOW_MODE_COUNT ? state->graphics_mode : 0];
    int mode_delta = settings_cycle_row(renderer,
                                        input,
                                        x,
                                        row_y,
                                        row_width,
                                        row_height,
                                        "Window Mode",
                                        mode_label);
    if (mode_delta != 0) {
        int mode_index = (int)state->graphics_mode + mode_delta;
        if (mode_index < 0) {
            mode_index += (int)PLATFORM_WINDOW_MODE_COUNT;
        }
        mode_index %= (int)PLATFORM_WINDOW_MODE_COUNT;
        state->graphics_mode = (PlatformWindowMode)mode_index;
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
    }

    row_y += row_height + 12.0f;
    if (resolutions && resolution_count > 0) {
        const PreferencesResolution *current = &resolutions[state->graphics_resolution_index % resolution_count];
        int res_delta = settings_cycle_row(renderer,
                                           input,
                                           x,
                                           row_y,
                                           row_width,
                                           row_height,
                                           "Resolution",
                                           current->label ? current->label : "Custom");
        if (res_delta != 0) {
            int next = (int)state->graphics_resolution_index + res_delta;
            if (next < 0) {
                next += (int)resolution_count;
            }
            state->graphics_resolution_index = (size_t)(next % (int)resolution_count);
            current = &resolutions[state->graphics_resolution_index];
            if (result) {
                result->graphics_changed = true;
                result->graphics_mode = state->graphics_mode;
                result->graphics_width = current->width;
                result->graphics_height = current->height;
            }
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

static void render_accessibility_tab(SettingsMenuState *state,
                                     const SettingsMenuContext *context,
                                     Renderer *renderer,
                                     const InputState *input,
                                     float x,
                                     float y,
                                     float width,
                                     SettingsMenuResult *result)
{
    (void)state;

    float entry_y = y;

    if (context && context->show_fps_overlay) {
        bool changed = settings_toggle(renderer,
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
        bool changed = settings_toggle(renderer,
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
        bool changed = settings_toggle(renderer,
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
                                        const InputState *input)
{
    SettingsMenuResult result;
    memset(&result, 0, sizeof(result));

    if (!state || !renderer) {
        return result;
    }

    SettingsMenuContext empty_context = {0};
    if (!context) {
        context = &empty_context;
    }

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
        "Accessibility"
    };

    for (int tab = 0; tab < SETTINGS_MENU_CATEGORY_COUNT; ++tab) {
        bool pressed = settings_tab_button(renderer,
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

    switch (state->active_category) {
    case SETTINGS_MENU_CATEGORY_GRAPHICS:
        render_graphics_tab(state, context, renderer, input, content_x, content_y, content_width, &result);
        break;
    case SETTINGS_MENU_CATEGORY_CONTROLS:
        render_controls_tab(state, renderer, input, content_x, content_y, content_width, content_height, &result);
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

    const float back_height = 44.0f;
    const float back_x = content_x;
    const float back_y = panel_y + panel_height - back_height - 32.0f;
    if (settings_button(renderer, input, back_x, back_y, content_width, back_height, context->in_game ? "Return" : "Back", false)) {
        result.back_requested = true;
        settings_menu_cancel_rebind(state);
        state->feedback_frames = 0;
        state->feedback_has_message = false;
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


