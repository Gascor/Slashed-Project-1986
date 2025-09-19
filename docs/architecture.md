# FPS Engine Skeleton (C)

## Toolchain
- Language: C17 (portable, good compiler support)
- Build: CMake + Ninja (cross-platform), MSVC/Clang/GCC toolchains
- Third-party libs kept minimal:
  - GLFW (window/input, no scene graph)
  - GLAD (OpenGL function loader)
  - stb image (texture loading)
  - Optional: cglm for linear algebra helpers (keeps maths header-only)

## Subsystems Overview

### Core
- Entry point and main loop with fixed-update / variable-render separation
- Platform layer initializes GLFW, handles OS events, wraps timing
- Memory arenas for transient allocations (frame allocator)

### Math
- Vec3/Vec4, Mat4, Quaternion structs + operations
- Transform helper (position/rotation/scale) with cached matrices
- Frustum construction for culling

### Renderer
- Renderer backend using OpenGL 4.5 core profile
- Shader manager for compiling/linking GLSL, uniform buffer helpers
- Mesh module (VBO/VAO management), material definition
- Render pipeline initial target: forward rendering, optional deferred path later
- Post-processing chain hook (framebuffer ping-pong)

### Scene / ECS
- Lightweight entity-component system
  - Components: Transform, Camera, MeshRenderer, Rigidbody, CharacterController, Light
  - Systems: Transform propagation, Render submission, Physics step, Script tick
- Scene graph stored as flat arrays for cache affinity

### Physics
- Broadphase: Dynamic AABB tree or uniform grid (start simple grid)
- Narrowphase: Capsule vs triangle mesh, AABB vs AABB
- Character controller: sweep tests, resolve penetration, gravity integration
- Raycast utilities for hitscan weapons

### Resources
- Virtual file system mapping assets folder
- Loaders: shaders, textures, meshes (OBJ initial, upgrade to glTF)
- Hot-reload watchers (platform-specific stub for later)

### Audio
- Minimal mixer using OpenAL Soft (C API) or mini audio backend
- Spatial audio hooks per entity

### Tooling
- Debug console overlay (ImGui optional later)
- Profiling macros writing to Chrome trace format
- Logging with severity levels, compiled-time toggles

## Project Layout


## Milestones
1. Window + OpenGL context + basic loop, clear screen color
2. Math module basics + camera
3. Mesh loading + shader pipeline -> draw cube
4. Player controller + collision primitives
5. Weapon and enemy prototype
6. Optimization & tooling



Project layout idea:

C-engine/
  CMakeLists.txt
  external/
    glfw/
    glad/
    stb/
  include/engine/
    core.h
    math.h
    renderer.h
    ecs.h
    physics.h
    resources.h
  src/
    main.c
    core/application.c
    core/time.c
    renderer/renderer.c
    renderer/shader.c
    ecs/ecs.c
    ecs/components.c
    physics/physics.c
    resources/loader.c
    platform/window_glfw.c
  assets/
    shaders/
    textures/
    models/
