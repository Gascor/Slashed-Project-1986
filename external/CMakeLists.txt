cmake_minimum_required(VERSION 3.20)

add_library(enet_stub STATIC
    enet_stub/enet_stub.c
)

set_target_properties(enet_stub PROPERTIES OUTPUT_NAME enet_stub)

target_include_directories(enet_stub
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/enet_stub"
)

add_library(enet::enet ALIAS enet_stub)
