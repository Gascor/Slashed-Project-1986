# Getting Started

## Prerequisites
- CMake 3.20+
- A C17-capable compiler (MSVC 2022, Clang 15+, or GCC 11+)
- Ninja (recommended) or Visual Studio generator on Windows
- Planned third-party libraries (not yet integrated):
  - stb image (textures)
  - Future cross-platform window/input abstraction (GLFW/SDL) if Win32 is not enough

## Configure & Build
    cmake -S . -B build -G "Ninja"
    cmake --build build

If you prefer Visual Studio, swap the generator: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Debug`.

Running `build\Debug\sp1986.exe` (or `build/sp1986` with Ninja) spawns a simple Win32 + OpenGL window that clears to a dark blue tone for the configured frame budget in `src/main.c`; press `Esc` to close it immediately.

## Suggested Next Steps
1. Expand the Win32 platform layer with input handling (keyboard/mouse), high-DPI support, and fullscreen toggles. Consider abstracting behind an interface so other backends (GLFW, SDL) can coexist.
2. Replace the immediate-mode renderer stub with a pipeline that loads shaders, sets up vertex buffers, and draws test geometry.
3. Implement file IO inside `resources_load_file` and point `resources_init` to the assets directory.
4. Extend the ECS to manage components (transform, camera, render data) and feed the renderer.
5. Instantiate a `PhysicsWorld` in the game loop and advance it with a fixed timestep to drive the player controller.
6. Add unit tests for maths and ECS primitives when functionality solidifies (CMake option `SP1986_BUILD_TESTS`).

## Repository Hygiene
- Vendor third-party libraries under external/ or add FetchContent entries in external/CMakeLists.txt.
- Keep platform dependencies isolated to the platform layer; other modules should include engine headers only.
- Track design decisions in docs/ as subsystems evolve.
