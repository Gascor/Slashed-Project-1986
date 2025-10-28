#pragma once

#include <stdint.h>

#include "engine/camera.h"
#include "engine/math.h"

typedef struct Renderer Renderer;

Renderer *renderer_create(void);
void renderer_destroy(Renderer *renderer);

void renderer_set_clear_color(Renderer *renderer, float r, float g, float b, float a);
void renderer_set_viewport(Renderer *renderer, uint32_t width, uint32_t height);

uint32_t renderer_viewport_width(const Renderer *renderer);
uint32_t renderer_viewport_height(const Renderer *renderer);

void renderer_begin_scene(Renderer *renderer, const Camera *camera);
void renderer_draw_grid(Renderer *renderer, float half_extent, float spacing, float height);
void renderer_draw_box(Renderer *renderer, vec3 center, vec3 half_extents, vec3 color);
void renderer_draw_weapon_viewmodel(Renderer *renderer, float recoil_amount);

void renderer_draw_frame(Renderer *renderer);

void renderer_begin_ui(Renderer *renderer);
void renderer_draw_ui_text(Renderer *renderer, float x, float y, const char *text, float r, float g, float b, float a);

void renderer_draw_ui_rect(Renderer *renderer, float x, float y, float width, float height, float r, float g, float b, float a);
void renderer_draw_ui_logo(Renderer *renderer, float center_x, float center_y, float max_width, float max_height);
void renderer_draw_crosshair(Renderer *renderer, float center_x, float center_y, float size, float spread, float thickness);
void renderer_end_ui(Renderer *renderer);

