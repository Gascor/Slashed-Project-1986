#include "engine/renderer.h"

#include <stdlib.h>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif
#include <GL/gl.h>

struct Renderer {
    float clear_color[4];
};

Renderer *renderer_create(void)
{
    struct Renderer *renderer = (struct Renderer *)calloc(1, sizeof(struct Renderer));
    if (!renderer) {
        return NULL;
    }

    renderer->clear_color[0] = 0.05f;
    renderer->clear_color[1] = 0.08f;
    renderer->clear_color[2] = 0.12f;
    renderer->clear_color[3] = 1.0f;

    glClearColor(renderer->clear_color[0], renderer->clear_color[1], renderer->clear_color[2], renderer->clear_color[3]);
    glDisable(GL_DEPTH_TEST);

    return renderer;
}

void renderer_destroy(Renderer *renderer)
{
    free(renderer);
}

void renderer_draw_frame(Renderer *renderer)
{
    if (!renderer) {
        return;
    }

    glClear(GL_COLOR_BUFFER_BIT);
}
