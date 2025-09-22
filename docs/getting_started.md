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

Running `build\Debug\sp1986.exe` (or `build/sp1986` with Ninja) spawns a Win32 + OpenGL window that now registers raw mouse input, clears to a color driven by a sandbox “player” controller, and prints the controller state to the console. Move with `WASD`, raise/lower with `Space`/`Ctrl`, look with the mouse, sprint with `Shift`, and press `Esc` to close immediately.

## Suggested Next Steps
1. Extend the platform layer with richer input features (raw mouse toggle, cursor capture, high-DPI awareness beyond `SetProcessDPIAware`, fullscreen toggles) and abstract for other backends.
2. Wire the camera view/projection matrices into a real renderer path: load GL functions, compile shaders, and draw test geometry (triangle/cube).
3. Implement file IO inside `resources_load_file` and point `resources_init` to the assets directory.
4. Extend the ECS to manage components (transform, camera, render data) and feed the renderer.
5. Instantiate a `PhysicsWorld` in the game loop and advance it with a fixed timestep to drive the player controller.
6. Add unit tests for maths and ECS primitives when functionality solidifies (CMake option `SP1986_BUILD_TESTS`).

## Repository Hygiene
- Vendor third-party libraries under external/ or add FetchContent entries in external/CMakeLists.txt.
- Keep platform dependencies isolated to the platform layer; other modules should include engine headers only.
- Track design decisions in docs/ as subsystems evolve.

