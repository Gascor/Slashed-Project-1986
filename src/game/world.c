#include "engine/world.h"
#include "engine/game.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

static vec3 world_default_scale_for_type(EntityType type)
{
    switch (type) {
    case ENTITY_TYPE_PLAYER:
        return vec3_make(0.5f, 1.0f, 0.5f);
    case ENTITY_TYPE_REMOTE_PLAYER:
        return vec3_make(1.0f, 1.0f, 1.0f);
    case ENTITY_TYPE_WEAPON_PICKUP:
        return vec3_make(0.8f, 0.25f, 0.25f);
    default:
        break;
    }
    return vec3_make(1.0f, 1.0f, 1.0f);
}

static vec3 world_default_color_for_type(EntityType type)
{
    switch (type) {
    case ENTITY_TYPE_PLAYER:
        return vec3_make(0.2f, 0.2f, 0.3f);
    case ENTITY_TYPE_REMOTE_PLAYER:
        return vec3_make(0.6f, 0.4f, 0.2f);
    case ENTITY_TYPE_WEAPON_PICKUP:
        return vec3_make(0.85f, 0.75f, 0.35f);
    default:
        break;
    }
    return vec3_make(1.0f, 1.0f, 1.0f);
}

static vec3 weapon_pickup_scale_for_category(WeaponCategory category)
{
    switch (category) {
    case WEAPON_CATEGORY_PISTOL:
        return vec3_make(0.65f, 0.22f, 0.20f);
    case WEAPON_CATEGORY_SMG:
        return vec3_make(0.80f, 0.24f, 0.26f);
    case WEAPON_CATEGORY_RIFLE:
        return vec3_make(1.15f, 0.28f, 0.32f);
    case WEAPON_CATEGORY_SHOTGUN:
        return vec3_make(1.30f, 0.30f, 0.36f);
    case WEAPON_CATEGORY_SNIPER:
        return vec3_make(1.45f, 0.32f, 0.38f);
    case WEAPON_CATEGORY_LMG:
        return vec3_make(1.60f, 0.34f, 0.40f);
    default:
        break;
    }
    return vec3_make(0.8f, 0.25f, 0.25f);
}

static vec3 weapon_pickup_color_for_category(WeaponCategory category)
{
    switch (category) {
    case WEAPON_CATEGORY_PISTOL:
        return vec3_make(0.95f, 0.85f, 0.35f);
    case WEAPON_CATEGORY_SMG:
        return vec3_make(0.30f, 0.85f, 0.60f);
    case WEAPON_CATEGORY_RIFLE:
        return vec3_make(0.35f, 0.65f, 0.95f);
    case WEAPON_CATEGORY_SHOTGUN:
        return vec3_make(0.95f, 0.55f, 0.35f);
    case WEAPON_CATEGORY_SNIPER:
        return vec3_make(0.75f, 0.35f, 0.95f);
    case WEAPON_CATEGORY_LMG:
        return vec3_make(0.95f, 0.70f, 0.35f);
    default:
        break;
    }
    return vec3_make(0.80f, 0.80f, 0.80f);
}

void world_init(GameWorld *world)
{
    world_reset(world);
}

void world_reset(GameWorld *world)
{
    if (!world) {
        return;
    }

    memset(world, 0, sizeof(*world));
    world->ground_height = 0.0f;
}

void world_update(GameWorld *world, float dt)
{
    if (!world) {
        return;
    }

    if (dt < 0.0f) {
        dt = 0.0f;
    }

    const float bob_speed = 1.6f;
    for (size_t i = 0; i < world->weapon_pickup_count; ++i) {
        WeaponPickup *pickup = &world->weapon_pickups[i];
        if (!pickup->active) {
            continue;
        }

        pickup->bob_timer += dt * bob_speed;
        if (pickup->bob_timer > (float)M_PI * 2.0f) {
            pickup->bob_timer -= (float)M_PI * 2.0f;
        }

        GameEntity *entity = world_find_entity(world, pickup->entity_id);
        if (!entity) {
            continue;
        }

        entity->position = pickup->base_position;
        entity->position.y += sinf(pickup->bob_timer) * 0.18f + 0.10f;
    }
}

size_t world_add_entity(GameWorld *world,
                        EntityType type,
                        vec3 position,
                        vec3 scale,
                        vec3 color,
                        bool visible)
{
    if (!world || world->entity_count >= GAME_MAX_ENTITIES) {
        return SIZE_MAX;
    }

    GameEntity *entity = &world->entities[world->entity_count];
    memset(entity, 0, sizeof(*entity));

    entity->id = ecs_create_entity();
    entity->type = type;
    entity->position = position;
    entity->scale = scale;
    entity->color = color;
    entity->visible = visible;

    return world->entity_count++;
}

GameEntity *world_get_entity(GameWorld *world, size_t index)
{
    if (!world || index >= world->entity_count) {
        return NULL;
    }
    return &world->entities[index];
}

const GameEntity *world_get_entity_const(const GameWorld *world, size_t index)
{
    if (!world || index >= world->entity_count) {
        return NULL;
    }
    return &world->entities[index];
}

GameEntity *world_create_entity(GameWorld *world, EntityType type)
{
    if (!world) {
        return NULL;
    }

    vec3 scale = world_default_scale_for_type(type);
    vec3 color = world_default_color_for_type(type);
    size_t index = world_add_entity(world, type, vec3_make(0.0f, 0.0f, 0.0f), scale, color, true);
    return world_get_entity(world, index);
}

static void world_remove_entity_internal(GameWorld *world, EntityId id)
{
    if (!world || id == 0) {
        return;
    }

    for (size_t i = 0; i < world->entity_count; ++i) {
        if (world->entities[i].id == id) {
            if (i < world->entity_count - 1) {
                world->entities[i] = world->entities[world->entity_count - 1];
            }
            --world->entity_count;
            break;
        }
    }
}

void world_remove_entity(GameWorld *world, EntityId id)
{
    if (!world || id == 0) {
        return;
    }

    for (size_t p = 0; p < world->weapon_pickup_count; ++p) {
        if (world->weapon_pickups[p].entity_id == id) {
            if (p < world->weapon_pickup_count - 1) {
                world->weapon_pickups[p] = world->weapon_pickups[world->weapon_pickup_count - 1];
            }
            --world->weapon_pickup_count;
            break;
        }
    }

    world_remove_entity_internal(world, id);
}

GameEntity *world_find_entity(GameWorld *world, EntityId id)
{
    if (!world || id == 0) {
        return NULL;
    }

    for (size_t i = 0; i < world->entity_count; ++i) {
        if (world->entities[i].id == id) {
            return &world->entities[i];
        }
    }
    return NULL;
}

void world_set_ground_height(GameWorld *world, float height)
{
    if (world) {
        world->ground_height = height;
    }
}

bool world_entity_is_solid(const GameEntity *entity)
{
    return entity && entity->visible && entity->type == ENTITY_TYPE_STATIC;
}

void world_spawn_default_geometry(GameWorld *world)
{
    if (!world) {
        return;
    }

    const vec3 wall_color = vec3_make(0.18f, 0.22f, 0.30f);
    const vec3 crate_color = vec3_make(0.35f, 0.28f, 0.16f);

    (void)world_add_entity(world,
                           ENTITY_TYPE_STATIC,
                           vec3_make(0.0f, 1.5f, -12.0f),
                           vec3_make(18.0f, 3.0f, 1.0f),
                           wall_color,
                           true);

    (void)world_add_entity(world,
                           ENTITY_TYPE_STATIC,
                           vec3_make(6.0f, 1.5f, -4.5f),
                           vec3_make(2.0f, 3.0f, 6.0f),
                           wall_color,
                           true);

    (void)world_add_entity(world,
                           ENTITY_TYPE_STATIC,
                           vec3_make(-6.0f, 1.0f, -6.0f),
                           vec3_make(2.5f, 2.0f, 2.5f),
                           crate_color,
                           true);

    (void)world_add_entity(world,
                           ENTITY_TYPE_STATIC,
                           vec3_make(2.0f, 0.75f, 3.0f),
                           vec3_make(1.5f, 1.5f, 1.5f),
                           crate_color,
                           true);
}

size_t world_spawn_remote_players(GameWorld *world,
                                  const GameConfig *config,
                                  size_t max_indices,
                                  size_t *indices_out)
{
    if (!world || !config) {
        return 0U;
    }

    const vec3 remote_colors[GAME_MAX_REMOTE_PLAYERS] = {
        vec3_make(0.85f, 0.25f, 0.25f),
        vec3_make(0.25f, 0.85f, 0.35f),
        vec3_make(0.25f, 0.55f, 0.95f),
        vec3_make(0.95f, 0.65f, 0.25f),
        vec3_make(0.95f, 0.30f, 0.70f),
        vec3_make(0.30f, 0.95f, 0.85f),
        vec3_make(0.75f, 0.30f, 0.95f),
        vec3_make(0.55f, 0.75f, 0.25f),
        vec3_make(0.95f, 0.45f, 0.45f),
        vec3_make(0.45f, 0.95f, 0.45f),
        vec3_make(0.45f, 0.55f, 0.95f),
        vec3_make(0.95f, 0.85f, 0.45f),
        vec3_make(0.80f, 0.35f, 0.95f),
        vec3_make(0.35f, 0.95f, 0.80f),
        vec3_make(0.95f, 0.55f, 0.80f),
        vec3_make(0.60f, 0.40f, 0.95f),
    };

    size_t count = 0U;
    const size_t limit = max_indices < GAME_MAX_REMOTE_PLAYERS ? max_indices : GAME_MAX_REMOTE_PLAYERS;

    for (size_t i = 0; i < GAME_MAX_REMOTE_PLAYERS; ++i) {
        vec3 position = vec3_make(-4.0f + (float)i * 2.8f, config->player_height, -6.0f);
        size_t index = world_add_entity(world,
                                        ENTITY_TYPE_REMOTE_PLAYER,
                                        position,
                                        vec3_make(1.0f, config->player_height * 2.0f, 1.0f),
                                        remote_colors[i],
                                        true);
        if (index == SIZE_MAX) {
            continue;
        }

        if (indices_out && count < limit) {
            indices_out[count] = index;
        }
        if (count < limit) {
            ++count;
        }
    }

    return count;
}

void world_spawn_default_weapon_pickups(GameWorld *world)
{
    if (!world) {
        return;
    }

    const size_t max_spawn = 50;
    const size_t columns = 10;
    const float spacing = 2.4f;
    const float start_x = -((float)(columns - 1) * spacing * 0.5f);
    const float start_z = -3.5f;

    size_t spawned = 0;
    uint32_t next_network_id = 1;
    for (size_t def_index = 1; def_index < WEAPON_ID_COUNT && spawned < max_spawn; ++def_index) {
        WeaponId weapon_id = weapon_definition_id_by_index(def_index);
        const WeaponDefinition *definition = weapon_definition(weapon_id);
        if (!definition || definition->clip_size <= 0) {
            continue;
        }

        size_t row = spawned / columns;
        size_t column = spawned % columns;
        vec3 position = vec3_make(start_x + (float)column * spacing,
                                  world->ground_height + 0.2f,
                                  start_z - (float)row * spacing);

        world_spawn_weapon_pickup(world,
                                  weapon_id,
                                  position,
                                  definition->clip_size,
                                  definition->ammo_reserve,
                                  next_network_id++);
        ++spawned;
    }
}

WeaponPickup *world_spawn_weapon_pickup(GameWorld *world,
                                        WeaponId weapon_id,
                                        vec3 position,
                                        int ammo_in_clip,
                                        int ammo_reserve,
                                        uint32_t network_id)
{
    if (!world || weapon_id <= WEAPON_ID_NONE || world->weapon_pickup_count >= GAME_MAX_WEAPON_PICKUPS) {
        return NULL;
    }

    const WeaponDefinition *definition = weapon_definition(weapon_id);
    if (!definition) {
        return NULL;
    }

    if (network_id != 0) {
        for (size_t i = 0; i < world->weapon_pickup_count; ++i) {
            if (world->weapon_pickups[i].network_id == network_id) {
                WeaponPickup *existing = &world->weapon_pickups[i];
                existing->weapon_id = weapon_id;
                existing->ammo_in_clip = (ammo_in_clip >= 0) ? ammo_in_clip : definition->clip_size;
                if (existing->ammo_in_clip > definition->clip_size) {
                    existing->ammo_in_clip = definition->clip_size;
                }
                if (existing->ammo_in_clip < 0) {
                    existing->ammo_in_clip = 0;
                }

                existing->ammo_reserve = (ammo_reserve >= 0) ? ammo_reserve : definition->ammo_reserve;
                if (existing->ammo_reserve < 0) {
                    existing->ammo_reserve = 0;
                }

                GameEntity *entity = world_find_entity(world, existing->entity_id);
                if (entity) {
                    entity->position = position;
                    existing->base_position = position;
                }
                return existing;
            }
        }
    }

    vec3 scale = weapon_pickup_scale_for_category(definition->category);
    vec3 color = weapon_pickup_color_for_category(definition->category);

    float min_y = world->ground_height + scale.y * 0.5f;
    if (position.y < min_y) {
        position.y = min_y;
    }

    size_t entity_index = world_add_entity(world,
                                           ENTITY_TYPE_WEAPON_PICKUP,
                                           position,
                                           scale,
                                           color,
                                           true);
    if (entity_index == SIZE_MAX) {
        return NULL;
    }

    WeaponPickup *pickup = &world->weapon_pickups[world->weapon_pickup_count++];
    pickup->weapon_id = weapon_id;
    pickup->ammo_in_clip = (ammo_in_clip >= 0) ? ammo_in_clip : definition->clip_size;
    if (pickup->ammo_in_clip > definition->clip_size) {
        pickup->ammo_in_clip = definition->clip_size;
    }
    if (pickup->ammo_in_clip < 0) {
        pickup->ammo_in_clip = 0;
    }

    pickup->ammo_reserve = (ammo_reserve >= 0) ? ammo_reserve : definition->ammo_reserve;
    if (pickup->ammo_reserve < 0) {
        pickup->ammo_reserve = 0;
    }

    const GameEntity *entity = world_get_entity_const(world, entity_index);
    pickup->entity_id = entity ? entity->id : 0;
    pickup->base_position = entity ? entity->position : position;
    pickup->bob_timer = 0.0f;
    pickup->network_id = network_id;
    pickup->active = true;

    return pickup;
}

WeaponPickup *world_get_weapon_pickup(GameWorld *world, size_t index)
{
    if (!world || index >= world->weapon_pickup_count) {
        return NULL;
    }
    return &world->weapon_pickups[index];
}

const WeaponPickup *world_get_weapon_pickup_const(const GameWorld *world, size_t index)
{
    if (!world || index >= world->weapon_pickup_count) {
        return NULL;
    }
    return &world->weapon_pickups[index];
}

bool world_remove_weapon_pickup(GameWorld *world, size_t index)
{
    if (!world || index >= world->weapon_pickup_count) {
        return false;
    }

    WeaponPickup pickup = world->weapon_pickups[index];
    if (pickup.entity_id != 0) {
        world_remove_entity_internal(world, pickup.entity_id);
    }

    if (index < world->weapon_pickup_count - 1) {
        world->weapon_pickups[index] = world->weapon_pickups[world->weapon_pickup_count - 1];
    }
    --world->weapon_pickup_count;
    return true;
}

bool world_remove_weapon_pickup_by_id(GameWorld *world, uint32_t network_id)
{
    if (!world || network_id == 0) {
        return false;
    }

    for (size_t i = 0; i < world->weapon_pickup_count; ++i) {
        if (world->weapon_pickups[i].network_id == network_id) {
            return world_remove_weapon_pickup(world, i);
        }
    }
    return false;
}

WeaponPickup *world_find_weapon_pickup_by_id(GameWorld *world, uint32_t network_id, size_t *index_out)
{
    if (!world || network_id == 0) {
        if (index_out) {
            *index_out = SIZE_MAX;
        }
        return NULL;
    }

    for (size_t i = 0; i < world->weapon_pickup_count; ++i) {
        if (world->weapon_pickups[i].network_id == network_id) {
            if (index_out) {
                *index_out = i;
            }
            return &world->weapon_pickups[i];
        }
    }

    if (index_out) {
        *index_out = SIZE_MAX;
    }
    return NULL;
}

WeaponPickup *world_find_nearest_weapon_pickup(GameWorld *world,
                                               vec3 position,
                                               float radius,
                                               size_t *index_out)
{
    if (!world) {
        if (index_out) {
            *index_out = SIZE_MAX;
        }
        return NULL;
    }

    const float radius_sq = radius * radius;
    WeaponPickup *best_pickup = NULL;
    float best_distance_sq = 0.0f;
    size_t best_index = SIZE_MAX;

    for (size_t i = 0; i < world->weapon_pickup_count; ++i) {
        WeaponPickup *pickup = &world->weapon_pickups[i];
        if (!pickup->active) {
            continue;
        }

        const GameEntity *entity = world_find_entity(world, pickup->entity_id);
        vec3 pickup_position = entity ? entity->position : pickup->base_position;
        vec3 delta = vec3_sub(pickup_position, position);
        float distance_sq = vec3_dot(delta, delta);
        if (distance_sq > radius_sq) {
            continue;
        }

        if (!best_pickup || distance_sq < best_distance_sq) {
            best_pickup = pickup;
            best_distance_sq = distance_sq;
            best_index = i;
        }
    }

    if (index_out) {
        *index_out = best_index;
    }

    return best_pickup;
}
