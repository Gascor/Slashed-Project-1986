#include "engine/player.h"

#include "engine/camera.h"
#include "engine/game.h"
#include "engine/math.h"
#include "engine/world.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

#define PLAYER_COLLIDER_RADIUS 0.35f
#define COLLISION_EPSILON 0.0005f
#define VIEW_BOB_DECAY 9.0f
#define PLAYER_STEP_EPSILON 0.05f

static bool aabb_intersects(vec3 a_center, vec3 a_half, vec3 b_center, vec3 b_half)
{
    return fabsf(a_center.x - b_center.x) <= (a_half.x + b_half.x) + COLLISION_EPSILON &&
           fabsf(a_center.y - b_center.y) <= (a_half.y + b_half.y) + COLLISION_EPSILON &&
           fabsf(a_center.z - b_center.z) <= (a_half.z + b_half.z) + COLLISION_EPSILON;
}

static float *player_velocity_axis(PlayerState *player, int axis)
{
    switch (axis) {
    case 0:
        return &player->velocity.x;
    case 1:
        return &player->velocity.y;
    default:
        return &player->velocity.z;
    }
}

static float vec3_component(vec3 value, int axis)
{
    switch (axis) {
    case 0:
        return value.x;
    case 1:
        return value.y;
    default:
        return value.z;
    }
}

static vec3 resolve_axis(PlayerState *player,
                         GameWorld *world,
                         const GameConfig *config,
                         vec3 current,
                         vec3 half,
                         float delta,
                         float *velocity_component,
                         int axis)
{
    if (delta == 0.0f || !world || !player || !config) {
        return current;
    }

    vec3 updated = current;
    if (axis == 0) {
        updated.x += delta;
    } else if (axis == 1) {
        updated.y += delta;
    } else {
        updated.z += delta;
    }

    bool collided = false;

    for (size_t i = 0; i < world->entity_count; ++i) {
        const GameEntity *entity = world_get_entity_const(world, i);
        if (!world_entity_is_solid(entity)) {
            continue;
        }

        vec3 e_half = vec3_scale(entity->scale, 0.5f);
        if (!aabb_intersects(updated, half, entity->position, e_half)) {
            continue;
        }

        if ((axis == 0 || axis == 2) && delta != 0.0f) {
            float player_bottom = updated.y - half.y;
            float entity_top = entity->position.y + e_half.y;
            if (player_bottom >= entity_top - PLAYER_STEP_EPSILON) {
                continue;
            }
        }

        collided = true;
        if (axis == 0) {
            if (delta > 0.0f) {
                updated.x = entity->position.x - e_half.x - half.x - COLLISION_EPSILON;
            } else {
                updated.x = entity->position.x + e_half.x + half.x + COLLISION_EPSILON;
            }
        } else if (axis == 1) {
            if (delta > 0.0f) {
                updated.y = entity->position.y - e_half.y - half.y - COLLISION_EPSILON;
            } else {
                updated.y = entity->position.y + e_half.y + half.y + COLLISION_EPSILON;
                player->grounded = true;
                player->double_jump_available = config->enable_double_jump;
                player->double_jump_timer = config->double_jump_window;
            }
        } else {
            if (delta > 0.0f) {
                updated.z = entity->position.z - e_half.z - half.z - COLLISION_EPSILON;
            } else {
                updated.z = entity->position.z + e_half.z + half.z + COLLISION_EPSILON;
            }
        }
    }

    if (axis == 1) {
        float ground = world->ground_height + half.y;
        if (updated.y < ground) {
            if (delta <= 0.0f) {
                player->grounded = true;
                player->double_jump_available = config->enable_double_jump;
                player->double_jump_timer = config->double_jump_window;
            }
            updated.y = ground;
            collided = true;
        }
    }

    if (collided && velocity_component) {
        *velocity_component = 0.0f;
    }

    return updated;
}

static vec3 resolve_sweep(PlayerState *player,
                          GameWorld *world,
                          const GameConfig *config,
                          vec3 start,
                          vec3 delta,
                          vec3 half)
{
    int axes[3] = {0, 1, 2};
    float magnitudes[3] = {fabsf(delta.x), fabsf(delta.y), fabsf(delta.z)};

    for (int i = 0; i < 2; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (magnitudes[j] > magnitudes[i]) {
                float tmp_mag = magnitudes[i];
                magnitudes[i] = magnitudes[j];
                magnitudes[j] = tmp_mag;

                int tmp_axis = axes[i];
                axes[i] = axes[j];
                axes[j] = tmp_axis;
            }
        }
    }

    if (delta.y > 0.0f) {
        for (int i = 0; i < 3; ++i) {
            if (axes[i] == 1) {
                int axis = axes[0];
                axes[0] = 1;
                axes[i] = axis;

                float mag = magnitudes[0];
                magnitudes[0] = fabsf(delta.y);
                magnitudes[i] = mag;
                break;
            }
        }
    }

    vec3 position = start;
    for (int i = 0; i < 3; ++i) {
        int axis = axes[i];
        float move = vec3_component(delta, axis);
        if (move == 0.0f) {
            continue;
        }
        float *velocity_component = player_velocity_axis(player, axis);
        position = resolve_axis(player, world, config, position, half, move, velocity_component, axis);
    }

    return position;
}

static void player_process_jump(PlayerState *player,
                                const PlayerCommand *command,
                                const GameConfig *config,
                                bool was_grounded,
                                float dt)
{
    if (!player || !command || !config || config->allow_flight) {
        return;
    }

    if (was_grounded) {
        player->double_jump_available = config->enable_double_jump;
        player->double_jump_timer = config->double_jump_window;
    } else if (player->double_jump_timer > 0.0f) {
        player->double_jump_timer -= dt;
        if (player->double_jump_timer < 0.0f) {
            player->double_jump_timer = 0.0f;
        }
    }

    if (!command->jump_requested) {
        return;
    }

    if (was_grounded) {
        player->velocity.y = config->jump_velocity;
        player->grounded = false;
        player->double_jump_available = config->enable_double_jump;
        player->double_jump_timer = config->double_jump_window;
        return;
    }

    if (config->enable_double_jump && player->double_jump_available && player->double_jump_timer > 0.0f) {
        player->velocity.y = config->jump_velocity;
        player->double_jump_available = false;
    }
}

void player_init(PlayerState *player, const GameConfig *config, vec3 start_position)
{
    if (!player || !config) {
        return;
    }

    memset(player, 0, sizeof(*player));
    player->height = config->player_height;
    player->position = start_position;
    player->collider_half_extents = vec3_make(PLAYER_COLLIDER_RADIUS, config->player_height * 0.5f, PLAYER_COLLIDER_RADIUS);
    player->camera_offset = vec3_make(0.0f, 0.0f, 0.0f);
    player->health = 100.0f;
    player->max_health = 100.0f;
    player->armor = 50.0f;
    player->max_armor = 100.0f;
    player->grounded = true;
    player->double_jump_available = config->enable_double_jump;
    player->double_jump_timer = config->double_jump_window;
    player->bob_phase = 0.0f;
    player->velocity = vec3_make(0.0f, 0.0f, 0.0f);
}

void player_reset_command(PlayerCommand *command)
{
    if (command) {
        memset(command, 0, sizeof(*command));
    }
}

void player_build_command(PlayerCommand *command,
                          const InputState *input,
                          const Camera *camera,
                          const GameConfig *config)
{
    if (!command || !input || !camera || !config) {
        return;
    }

    vec3 forward = camera_forward(camera);
    forward.y = 0.0f;
    if (vec3_length(forward) > 0.0f) {
        forward = vec3_normalize(forward);
    }

    vec3 right = camera_right(camera);
    right.y = 0.0f;
    if (vec3_length(right) > 0.0f) {
        right = vec3_normalize(right);
    }

    vec3 move = vec3_make(0.0f, 0.0f, 0.0f);
    if (input->move_forward != 0.0f) {
        move = vec3_add(move, vec3_scale(forward, input->move_forward));
    }
    if (input->move_right != 0.0f) {
        move = vec3_add(move, vec3_scale(right, input->move_right));
    }

    float magnitude = vec3_length(move);
    if (magnitude > 0.0f) {
        move = vec3_scale(move, 1.0f / magnitude);
    }

    command->move_direction = move;
    command->move_magnitude = magnitude;
    command->vertical_axis = config->allow_flight ? input->move_vertical : 0.0f;
    command->jump_requested = input->jump_pressed;
    command->sprint = input->sprinting;
    command->fire_down = input->fire_down;
    command->fire_pressed = input->fire_pressed;
    command->fire_released = input->mouse_left_released;
    command->reload_requested = input->reload_pressed;
    command->interact_requested = input->interact_pressed;
    command->drop_requested = input->drop_pressed;

    if (input->mouse_wheel > 0.1f) {
        command->weapon_slot_delta = 1;
    } else if (input->mouse_wheel < -0.1f) {
        command->weapon_slot_delta = -1;
    } else {
        command->weapon_slot_delta = 0;
    }
}

void player_update_physics(PlayerState *player,
                           const PlayerCommand *command,
                           const GameConfig *config,
                           GameWorld *world,
                           float dt,
                           size_t player_entity_index)
{
    if (!player || !command || !config || !world) {
        return;
    }

    const bool was_grounded = player->grounded;

    vec3 horizontal_velocity = vec3_make(player->velocity.x, 0.0f, player->velocity.z);
    const float speed = config->move_speed * (command->sprint ? config->sprint_multiplier : 1.0f);
    vec3 desired_velocity = vec3_scale(command->move_direction, speed);
    vec3 accel = vec3_sub(desired_velocity, horizontal_velocity);
    float accel_length = vec3_length(accel);
    float max_accel = (player->grounded ? config->ground_acceleration : config->air_control) * dt;
    if (accel_length > max_accel && accel_length > 0.0001f) {
        accel = vec3_scale(accel, max_accel / accel_length);
    }
    horizontal_velocity = vec3_add(horizontal_velocity, accel);

    if (player->grounded && command->move_magnitude < 0.01f) {
        float damping = expf(-config->ground_friction * dt);
        horizontal_velocity = vec3_scale(horizontal_velocity, damping);
    }

    player->velocity.x = horizontal_velocity.x;
    player->velocity.z = horizontal_velocity.z;

    if (config->allow_flight) {
        player->velocity.y = command->vertical_axis * speed;
        player->grounded = false;
        player->double_jump_available = false;
        player->double_jump_timer = 0.0f;
    } else {
        player->velocity.y -= config->gravity * dt;
        player_process_jump(player, command, config, was_grounded, dt);
        player->grounded = false;
    }

    vec3 displacement = vec3_scale(player->velocity, dt);
    const vec3 half = player->collider_half_extents;
    vec3 start_center = vec3_make(player->position.x, player->position.y - player->height * 0.5f, player->position.z);
    vec3 resolved_center = resolve_sweep(player, world, config, start_center, displacement, half);

    player->position = vec3_make(resolved_center.x, resolved_center.y + player->height * 0.5f, resolved_center.z);

    GameEntity *player_entity = world_get_entity(world, player_entity_index);
    if (player_entity) {
        player_entity->position = player->position;
    }
}

void player_update_camera(PlayerState *player,
                          Camera *camera,
                          const GameConfig *config,
                          const PlayerCommand *command,
                          float dt)
{
    if (!player || !camera || !config || !command) {
        return;
    }

    if (!config->enable_view_bobbing) {
        player->camera_offset = vec3_make(0.0f, 0.0f, 0.0f);
        camera->position = player->position;
        return;
    }

    vec3 horizontal_velocity = vec3_make(player->velocity.x, 0.0f, player->velocity.z);
    float speed = vec3_length(horizontal_velocity);

    if (speed > 0.2f && player->grounded && command->move_magnitude > 0.0f) {
        player->bob_phase += config->view_bobbing_frequency * dt;
        if (player->bob_phase > (float)M_PI * 2.0f) {
            player->bob_phase -= (float)M_PI * 2.0f;
        }
        float bob = sinf(player->bob_phase) * config->view_bobbing_amplitude;
        float sway = cosf(player->bob_phase * 0.5f) * config->view_bobbing_amplitude * 0.35f;
        player->camera_offset = vec3_make(sway, bob, 0.0f);
    } else {
        float decay = expf(-VIEW_BOB_DECAY * dt);
        player->camera_offset = vec3_scale(player->camera_offset, decay);
        if (fabsf(player->camera_offset.x) < 0.0001f) {
            player->camera_offset.x = 0.0f;
        }
        if (fabsf(player->camera_offset.y) < 0.0001f) {
            player->camera_offset.y = 0.0f;
        }
        if (fabsf(player->camera_offset.z) < 0.0001f) {
            player->camera_offset.z = 0.0f;
        }
    }

    camera->position = vec3_add(player->position, player->camera_offset);
}

void player_apply_damage(PlayerState *player, float damage)
{
    if (!player) {
        return;
    }

    if (player->armor > 0.0f) {
        float absorb = fminf(damage * 0.5f, player->armor);
        player->armor -= absorb;
        damage -= absorb;
    }

    player->health -= damage;
    if (player->health < 0.0f) {
        player->health = 0.0f;
    }
}

void player_heal(PlayerState *player, float amount)
{
    if (!player) {
        return;
    }

    player->health += amount;
    if (player->health > player->max_health) {
        player->health = player->max_health;
    }
}

