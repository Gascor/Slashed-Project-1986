cmake_minimum_required(VERSION 3.20)
project(SlashedProject1986 C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SP1986_BUILD_TESTS "Build unit tests" ON)
option(SP1986_BUILD_EXAMPLES "Build example programs" OFF)

set(ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(ENGINE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(PLATFORM_SOURCE ${ENGINE_SOURCE_DIR}/platform/window_stub.c)
if (WIN32)
    set(PLATFORM_SOURCE ${ENGINE_SOURCE_DIR}/platform/window_win32.c)
endif()

add_library(engine STATIC
    ${ENGINE_SOURCE_DIR}/core/application.c
    ${PLATFORM_SOURCE}
    ${ENGINE_SOURCE_DIR}/renderer/renderer.c
    ${ENGINE_SOURCE_DIR}/ecs/ecs.c
    ${ENGINE_SOURCE_DIR}/physics/physics.c
    ${ENGINE_SOURCE_DIR}/resources/loader.c
)

target_include_directories(engine
    PUBLIC
        ${ENGINE_INCLUDE_DIR}
)

target_compile_definitions(engine
    PRIVATE
        $<$<CONFIG:Debug>:SP1986_DEBUG>
)

add_executable(sp1986
    ${ENGINE_SOURCE_DIR}/main.c
)

target_link_libraries(sp1986 PRIVATE engine)

if (WIN32)
    target_link_libraries(sp1986 PRIVATE opengl32 user32 gdi32)
endif()

target_include_directories(sp1986 PRIVATE ${ENGINE_INCLUDE_DIR})

if (MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
    target_compile_options(sp1986 PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(sp1986 PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(external EXCLUDE_FROM_ALL)
