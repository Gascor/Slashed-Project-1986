#include "engine/core.h"

int main(void)
{
    EngineConfig config = {
        .width = 1280,
        .height = 720,
        .title = "Slashed Project 1986 (stub)",
        .target_fps = 60,
        .max_frames = 300,
    };

    return engine_run(&config);
}
