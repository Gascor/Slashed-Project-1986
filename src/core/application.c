#include "engine/core.h"
#include "engine/platform.h"
#include "engine/renderer.h"

#include <stdio.h>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <time.h>
#endif

static void sleep_milliseconds(unsigned int ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

int engine_run(const EngineConfig *config)
{
    if (!config) {
        return -1;
    }

    if (!platform_init()) {
        fprintf(stderr, "[engine] platform_init failed\n");
        return -2;
    }

    PlatformWindowDesc desc = {
        .width = config->width,
        .height = config->height,
        .title = config->title,
    };

    PlatformWindow *window = platform_create_window(&desc);
    if (!window) {
        fprintf(stderr, "[engine] platform_create_window failed\n");
        platform_shutdown();
        return -3;
    }

    Renderer *renderer = renderer_create();
    if (!renderer) {
        platform_destroy_window(window);
        platform_shutdown();
        return -4;
    }

    const double target_dt = config->target_fps ? 1.0 / (double)config->target_fps : 0.0;
    double last_time = platform_get_time();

    uint64_t frame_index = 0;
    const uint64_t frame_limit = config->max_frames;

    while (!platform_window_should_close(window)) {
        platform_begin_frame(window);
        platform_poll_events(window);

        if (platform_key_pressed(window, PLATFORM_KEY_ESCAPE)) {
            platform_window_request_close(window);
        }

        /* TODO: update systems */
        renderer_draw_frame(renderer);
        platform_swap_buffers(window);

        ++frame_index;
        if (frame_limit && frame_index >= frame_limit) {
            break;
        }

        if (target_dt > 0.0) {
            double now = platform_get_time();
            double frame_time = now - last_time;
            if (frame_time < target_dt) {
                unsigned int sleep_ms = (unsigned int)((target_dt - frame_time) * 1000.0);
                if (sleep_ms > 0U) {
                    sleep_milliseconds(sleep_ms);
                }
                /* spin for the remainder */
                while ((platform_get_time() - last_time) < target_dt) {
                }
            }
            last_time = platform_get_time();
        }
    }

    renderer_destroy(renderer);
    platform_destroy_window(window);
    platform_shutdown();

    return 0;
}
