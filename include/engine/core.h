#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct EngineConfig {
    uint32_t width;
    uint32_t height;
    const char *title;
    uint32_t target_fps;
    uint64_t max_frames; /* 0 means run until window close */
    bool show_fps;
} EngineConfig;

int engine_run(const EngineConfig *config);

