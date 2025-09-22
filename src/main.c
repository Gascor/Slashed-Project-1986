#include "engine/core.h"
#include <string.h>

int main(int argc, char **argv)
{
    bool show_fps = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--show-fps") == 0 || strcmp(argv[i], "--fps") == 0) {
            show_fps = true;
        }
    }

    EngineConfig config = {
        .width = 1280,
        .height = 720,
        .title = "Slashed Project 1986 (stub)",
        .target_fps = 0,
        .max_frames = 0,
        .show_fps = show_fps,
    };

    return engine_run(&config);
}

