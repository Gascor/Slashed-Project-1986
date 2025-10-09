cmake_minimum_required(VERSION 3.20)
project(SlashedProject1986 C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SP1986_BUILD_TESTS "Build unit tests" ON)
option(SP1986_BUILD_EXAMPLES "Build example programs" OFF)

set(ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(ENGINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(external EXCLUDE_FROM_ALL)

set(PLATFORM_SOURCE "${ENGINE_SOURCE_DIR}/platform/window_stub.c")
if (WIN32)
    set(PLATFORM_SOURCE "${ENGINE_SOURCE_DIR}/platform/window_win32.c")
endif()

set(ENGINE_SOURCES
    "${ENGINE_SOURCE_DIR}/core/application.c"
    "${ENGINE_SOURCE_DIR}/core/audio.c"
    "${ENGINE_SOURCE_DIR}/core/camera.c"
    "${ENGINE_SOURCE_DIR}/core/menu.c"
    "${ENGINE_SOURCE_DIR}/core/preferences.c"
    "${ENGINE_SOURCE_DIR}/core/input.c"
    "${ENGINE_SOURCE_DIR}/core/math.c"
    "${ENGINE_SOURCE_DIR}/ecs/ecs.c"
    "${ENGINE_SOURCE_DIR}/game/game.c"
    "${ENGINE_SOURCE_DIR}/game/hud.c"
    "${ENGINE_SOURCE_DIR}/game/player.c"
    "${ENGINE_SOURCE_DIR}/game/server_browser.c"
    "${ENGINE_SOURCE_DIR}/game/weapons.c"
    "${ENGINE_SOURCE_DIR}/game/world.c"
    "${ENGINE_SOURCE_DIR}/network/client.c"
    "${ENGINE_SOURCE_DIR}/network/master_client.c"
    "${ENGINE_SOURCE_DIR}/network/master_server.c"
    "${ENGINE_SOURCE_DIR}/network/network.c"
    "${ENGINE_SOURCE_DIR}/network/server.c"
    "${ENGINE_SOURCE_DIR}/physics/physics.c"
    "${ENGINE_SOURCE_DIR}/renderer/renderer.c"
    "${ENGINE_SOURCE_DIR}/resources/loader.c"
    "${ENGINE_SOURCE_DIR}/ui/settings_menu.c"
    "${PLATFORM_SOURCE}"
)

add_library(engine STATIC ${ENGINE_SOURCES})

target_include_directories(engine
    PUBLIC
        "${ENGINE_INCLUDE_DIR}"
)

target_compile_definitions(engine
    PRIVATE
        $<$<CONFIG:Debug>:SP1986_DEBUG>
)

add_executable(sp1986
    "${ENGINE_SOURCE_DIR}/main.c"
)

target_include_directories(sp1986 PRIVATE "${ENGINE_INCLUDE_DIR}")

target_link_libraries(sp1986 PRIVATE engine)

if (WIN32)
    target_link_libraries(engine PRIVATE enet::enet ws2_32 winmm)
    target_link_libraries(sp1986 PRIVATE opengl32 user32 gdi32 winmm)
else()
    target_link_libraries(engine PRIVATE enet::enet m)
endif()

if (MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
    target_compile_options(sp1986 PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(sp1986 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- options
option(SP1986_BUILD_DEDICATED "Build dedicated game server (server.exe)" ON)
option(SP1986_BUILD_MASTER    "Build master list server (master_server.exe)" ON)

# --- serveur dédié
if (SP1986_BUILD_DEDICATED)
    add_executable(server
        ${ENGINE_SOURCE_DIR}/network/server_main.c
    )
    target_include_directories(server PRIVATE "${ENGINE_INCLUDE_DIR}")
    target_link_libraries(server PRIVATE engine)

    if (WIN32)
        target_link_libraries(server PRIVATE ws2_32)
    endif()

    if (MSVC)
        target_compile_definitions(server PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(server PRIVATE /W4 /permissive-)
    else()
        target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# --- master server (liste globale)
if (SP1986_BUILD_MASTER)
    add_executable(master_server
        ${ENGINE_SOURCE_DIR}/network/master_main.c
    )
    target_include_directories(master_server PRIVATE "${ENGINE_INCLUDE_DIR}")
    target_link_libraries(master_server PRIVATE engine)

    if (WIN32)
        target_link_libraries(master_server PRIVATE ws2_32)
    endif()

    if (MSVC)
        target_compile_definitions(master_server PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(master_server PRIVATE /W4 /permissive-)
    else()
        target_compile_options(master_server PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
