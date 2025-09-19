#pragma once

typedef struct Renderer Renderer;

Renderer *renderer_create(void);
void renderer_destroy(Renderer *renderer);
void renderer_draw_frame(Renderer *renderer);
