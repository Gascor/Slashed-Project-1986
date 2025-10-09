#pragma once

#include "engine/math.h"
#include "engine/input.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct PlayerCommand {
    vec3 move_direction;
    float move_magnitude;
    float vertical_axis;
    bool jump_requested;
    bool sprint;
    bool fire_pressed;
    bool fire_down;
    bool fire_released;
    bool reload_requested;
    bool interact_requested;
    bool drop_requested;
    int weapon_slot_delta;
} PlayerCommand;

typedef struct PlayerState {
    vec3 position;
    vec3 velocity;
    vec3 collider_half_extents;
    vec3 camera_offset;
    float height;
    float health;
    float max_health;
    float armor;
    float max_armor;
    bool grounded;
    bool double_jump_available;
    float double_jump_timer;
    float bob_phase;
} PlayerState;

typedef struct Camera Camera;
typedef struct GameConfig GameConfig;
typedef struct GameWorld GameWorld;

// Player management functions
void player_init(PlayerState *player, const GameConfig *config, vec3 start_position);
void player_reset_command(PlayerCommand *command);
void player_build_command(PlayerCommand *command,
                          const InputState *input,
                          const Camera *camera,
                          const GameConfig *config);
void player_update_physics(PlayerState *player,
                           const PlayerCommand *command,
                           const GameConfig *config,
                           GameWorld *world,
                           float dt,
                           size_t player_entity_index);
void player_update_camera(PlayerState *player,
                          Camera *camera,
                          const GameConfig *config,
                          const PlayerCommand *command,
                          float dt);
void player_apply_damage(PlayerState *player, float damage);
void player_heal(PlayerState *player, float amount);
