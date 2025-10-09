#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "engine/camera.h"
#include "engine/input.h"
#include "engine/master_protocol.h"
#include "engine/network.h"
#include "engine/physics.h"
#include "engine/renderer.h"

typedef struct GameConfig {
    float mouse_sensitivity;
    float move_speed;
    float sprint_multiplier;
    float jump_velocity;
    float gravity;
    float player_height;
    float ground_acceleration;
    float ground_friction;
    float air_control;
    bool enable_double_jump;
    float double_jump_window;
    bool allow_flight;
    bool enable_view_bobbing;
    float view_bobbing_amplitude;
    float view_bobbing_frequency;
} GameConfig;

typedef struct GameState GameState;

GameState *game_create(const GameConfig *config, Renderer *renderer, PhysicsWorld *physics_world);
void game_destroy(GameState *game);

void game_resize(GameState *game, uint32_t width, uint32_t height);

void game_handle_input(GameState *game, const InputState *input, float dt);
void game_update(GameState *game, float dt);
void game_render(GameState *game);

const Camera *game_camera(const GameState *game);
const NetworkClientStats *game_network_stats(const GameState *game);
bool game_is_paused(const GameState *game);
bool game_should_quit(const GameState *game);
void game_clear_quit_request(GameState *game);
void game_set_double_jump_enabled(GameState *game, bool enabled);
bool game_connect_to_master_entry(GameState *game, const MasterServerEntry *entry);
bool game_request_open_server_browser(GameState *game);
