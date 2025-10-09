#pragma once

#include "engine/ecs.h"
#include "engine/math.h"
#include "engine/weapons.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_MAX_ENTITIES 128
#define GAME_MAX_REMOTE_PLAYERS 16
#define GAME_MAX_WEAPON_PICKUPS 256

typedef struct GameConfig GameConfig;

typedef enum EntityType {
    ENTITY_TYPE_PLAYER = 0,
    ENTITY_TYPE_STATIC,
    ENTITY_TYPE_REMOTE_PLAYER,
    ENTITY_TYPE_WEAPON_PICKUP
} EntityType;

typedef struct GameEntity {
    EntityId id;
    EntityType type;
    vec3 position;
    vec3 scale;
    vec3 color;
    bool visible;
} GameEntity;

typedef struct WeaponPickup {
    WeaponId weapon_id;
    int ammo_in_clip;
    int ammo_reserve;
    uint32_t network_id;
    EntityId entity_id;
    vec3 base_position;
    float bob_timer;
    bool active;
} WeaponPickup;

typedef struct GameWorld {
    GameEntity entities[GAME_MAX_ENTITIES];
    size_t entity_count;
    WeaponPickup weapon_pickups[GAME_MAX_WEAPON_PICKUPS];
    size_t weapon_pickup_count;
    float ground_height;
} GameWorld;

// World management functions
void world_init(GameWorld *world);
void world_reset(GameWorld *world);
void world_update(GameWorld *world, float dt);

size_t world_add_entity(GameWorld *world,
                        EntityType type,
                        vec3 position,
                        vec3 scale,
                        vec3 color,
                        bool visible);

GameEntity *world_get_entity(GameWorld *world, size_t index);
const GameEntity *world_get_entity_const(const GameWorld *world, size_t index);

GameEntity *world_create_entity(GameWorld *world, EntityType type);
void world_remove_entity(GameWorld *world, EntityId id);
GameEntity *world_find_entity(GameWorld *world, EntityId id);
void world_set_ground_height(GameWorld *world, float height);

bool world_entity_is_solid(const GameEntity *entity);

void world_spawn_default_geometry(GameWorld *world);
void world_spawn_default_weapon_pickups(GameWorld *world);
size_t world_spawn_remote_players(GameWorld *world,
                                  const GameConfig *config,
                                  size_t max_indices,
                                  size_t *indices_out);

WeaponPickup *world_spawn_weapon_pickup(GameWorld *world,
                                        WeaponId weapon_id,
                                        vec3 position,
                                        int ammo_in_clip,
                                        int ammo_reserve,
                                        uint32_t network_id);
WeaponPickup *world_get_weapon_pickup(GameWorld *world, size_t index);
const WeaponPickup *world_get_weapon_pickup_const(const GameWorld *world, size_t index);
bool world_remove_weapon_pickup(GameWorld *world, size_t index);
bool world_remove_weapon_pickup_by_id(GameWorld *world, uint32_t network_id);
WeaponPickup *world_find_weapon_pickup_by_id(GameWorld *world, uint32_t network_id, size_t *index_out);
WeaponPickup *world_find_nearest_weapon_pickup(GameWorld *world,
                                               vec3 position,
                                               float radius,
                                               size_t *index_out);
