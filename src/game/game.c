#include "engine/game.h"

#include "engine/ecs.h"
#include "engine/math.h"
#include "engine/network.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#    define M_PI_2 (M_PI / 2.0)
#endif

#define GAME_MAX_ENTITIES 128
#define GAME_MAX_REMOTE_PLAYERS 4
#define GAME_MAX_WEAPON_ITEMS 16
#define GAME_MAX_SERVER_LIST 64
#define GAME_SERVER_STATUS_MAX 128

#define PLAYER_COLLIDER_RADIUS 0.35f
#define COLLISION_EPSILON 0.0005f
#define VIEW_BOB_DECAY 9.0f
#define PLAYER_STEP_EPSILON 0.05f

typedef enum EntityType {
    ENTITY_TYPE_PLAYER = 0,
    ENTITY_TYPE_STATIC,
    ENTITY_TYPE_REMOTE_PLAYER
} EntityType;

typedef enum WeaponItemType {
    WEAPON_ITEM_NONE = 0,
    WEAPON_ITEM_EXTENDED_MAG,
    WEAPON_ITEM_RECOIL_STABILIZER,
    WEAPON_ITEM_TRIGGER_TUNING
} WeaponItemType;

typedef struct WeaponItem {
    WeaponItemType type;
    float amount;
    bool equipped;
} WeaponItem;

typedef struct GameEntity {
    EntityId id;
    EntityType type;
    vec3 position;
    vec3 scale;
    vec3 color;
    bool visible;
} GameEntity;

typedef struct GameWorld {
    GameEntity entities[GAME_MAX_ENTITIES];
    size_t entity_count;
    float ground_height;
} GameWorld;

typedef struct PlayerCommand {
    vec3 move_direction;
    float move_magnitude;
    float vertical_axis;
    bool jump_requested;
    bool sprint;
    bool fire_pressed;
    bool fire_down;
    bool reload_requested;
    bool interact_requested;
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

typedef struct WeaponState {
    int clip_size;
    int ammo_in_clip;
    int ammo_reserve;
    float fire_rate;
    float cooldown;
    float reload_time;
    float reload_timer;
    bool reloading;
    float recoil;
    float recoil_recovery_rate;
    int base_clip_size;
    float base_fire_rate;
    float base_recoil_recovery_rate;
} WeaponState;

typedef struct HudState {
    float crosshair_base;
    float crosshair_spread;
    float damage_flash;
    float network_indicator_timer;
} HudState;

typedef struct GameInventory {
    WeaponItem weapon_items[GAME_MAX_WEAPON_ITEMS];
    size_t weapon_item_count;
} GameInventory;

typedef struct ServerBrowserState {
    MasterServerEntry entries[GAME_MAX_SERVER_LIST];
    size_t entry_count;
    int selection;
    bool open;
    bool last_request_success;
    char status[GAME_SERVER_STATUS_MAX];
    double last_refresh_time;
} ServerBrowserState;

struct GameState {
    Renderer *renderer;
    PhysicsWorld *physics;
    NetworkClient *network;

    Camera camera;
    GameConfig config;

    GameWorld world;
    PlayerState player;
    PlayerCommand command;
    WeaponState weapon;
    HudState hud;
    GameInventory inventory;
    InputState last_input;
    ServerBrowserState server_browser;
    NetworkClientConfig network_config;
    MasterClientConfig master_config;
    char current_server_address[MASTER_SERVER_ADDR_MAX];
    uint16_t current_server_port;
    char master_server_host[MASTER_SERVER_ADDR_MAX];

    size_t player_entity_index;
    size_t remote_entity_indices[GAME_MAX_REMOTE_PLAYERS];
    size_t remote_entity_count;
    uint32_t remote_visible_count;
    double network_anim_time;

    double time_seconds;
    double session_time;

    bool paused;
    bool options_open;
    int pause_selection;
    int options_selection;

    bool request_quit;

    char objective_text[64];
    char hud_notification[96];
    float hud_notification_timer;
};

static GameConfig game_default_config(void)
{
    GameConfig config;
    config.mouse_sensitivity = 1.0f;
    config.move_speed = 6.0f;
    config.sprint_multiplier = 1.6f;
    config.jump_velocity = 6.0f;
    config.gravity = 9.81f;
    config.player_height = 1.7f;
    config.ground_acceleration = 32.0f;
    config.ground_friction = 4.0f;
    config.air_control = 6.0f;
    config.enable_double_jump = true;
    config.double_jump_window = 1.0f;
    config.allow_flight = false;
    config.enable_view_bobbing = true;
    config.view_bobbing_amplitude = 0.035f;
    config.view_bobbing_frequency = 9.0f;
    return config;
}

static WeaponItem weapon_item_make(WeaponItemType type, float amount)
{
    WeaponItem item;
    item.type = type;
    item.amount = amount;
    item.equipped = true;
    return item;
}

static void inventory_add_weapon_item(GameInventory *inventory, WeaponItem item)
{
    if (!inventory || inventory->weapon_item_count >= GAME_MAX_WEAPON_ITEMS) {
        return;
    }
    inventory->weapon_items[inventory->weapon_item_count++] = item;
}

static void weapon_reset_stats(WeaponState *weapon)
{
    if (!weapon) {
        return;
    }

    weapon->clip_size = weapon->base_clip_size;
    weapon->fire_rate = weapon->base_fire_rate;
    weapon->recoil_recovery_rate = weapon->base_recoil_recovery_rate;
}

static void weapon_apply_items(GameState *game)
{
    if (!game) {
        return;
    }

    WeaponState *weapon = &game->weapon;
    weapon_reset_stats(weapon);

    for (size_t i = 0; i < game->inventory.weapon_item_count; ++i) {
        const WeaponItem *item = &game->inventory.weapon_items[i];
        if (!item->equipped) {
            continue;
        }

        switch (item->type) {
        case WEAPON_ITEM_EXTENDED_MAG: {
            const float factor = (item->amount > -0.9f) ? (1.0f + item->amount) : 0.1f;
            weapon->clip_size = (int)((float)weapon->clip_size * factor + 0.5f);
            if (weapon->clip_size < 1) {
                weapon->clip_size = 1;
            }
            break;
        }
        case WEAPON_ITEM_RECOIL_STABILIZER:
            weapon->recoil_recovery_rate *= (1.0f + item->amount);
            if (weapon->recoil_recovery_rate < 0.1f) {
                weapon->recoil_recovery_rate = 0.1f;
            }
            break;
        case WEAPON_ITEM_TRIGGER_TUNING:
            weapon->fire_rate *= (1.0f + item->amount);
            if (weapon->fire_rate < 1.0f) {
                weapon->fire_rate = 1.0f;
            }
            break;
        default:
            break;
        }
    }

    if (weapon->ammo_in_clip > weapon->clip_size) {
        weapon->ammo_in_clip = weapon->clip_size;
    }
}

static void game_inventory_init(GameState *game)
{
    if (!game) {
        return;
    }

    memset(&game->inventory, 0, sizeof(game->inventory));
    weapon_apply_items(game);
}

static void game_world_reset(GameWorld *world)
{
    if (!world) {
        return;
    }
    world->entity_count = 0U;
    world->ground_height = 0.0f;
}

static size_t game_world_add_entity(GameWorld *world,
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
    entity->id = ecs_create_entity();
    entity->type = type;
    entity->position = position;
    entity->scale = scale;
    entity->color = color;
    entity->visible = visible;

    return world->entity_count++;
}

static GameEntity *game_world_entity(GameWorld *world, size_t index)
{
    if (!world || index >= world->entity_count) {
        return NULL;
    }
    return &world->entities[index];
}

static const GameEntity *game_world_entity_const(const GameWorld *world, size_t index)
{
    if (!world || index >= world->entity_count) {
        return NULL;
    }
    return &world->entities[index];
}

static void game_reset_command(PlayerCommand *command)
{
    if (command) {
        memset(command, 0, sizeof(*command));
    }
}

static void game_spawn_level_geometry(GameWorld *world)
{
    if (!world) {
        return;
    }

    const vec3 wall_color = vec3_make(0.18f, 0.22f, 0.30f);
    const vec3 crate_color = vec3_make(0.35f, 0.28f, 0.16f);

    (void)game_world_add_entity(world,
                                ENTITY_TYPE_STATIC,
                                vec3_make(0.0f, 1.5f, -12.0f),
                                vec3_make(18.0f, 3.0f, 1.0f),
                                wall_color,
                                true);

    (void)game_world_add_entity(world,
                                ENTITY_TYPE_STATIC,
                                vec3_make(6.0f, 1.5f, -4.5f),
                                vec3_make(2.0f, 3.0f, 6.0f),
                                wall_color,
                                true);

    (void)game_world_add_entity(world,
                                ENTITY_TYPE_STATIC,
                                vec3_make(-6.0f, 1.0f, -6.0f),
                                vec3_make(2.5f, 2.0f, 2.5f),
                                crate_color,
                                true);

    (void)game_world_add_entity(world,
                                ENTITY_TYPE_STATIC,
                                vec3_make(2.0f, 0.75f, 3.0f),
                                vec3_make(1.5f, 1.5f, 1.5f),
                                crate_color,
                                true);
}

static void game_spawn_remote_players(GameState *game)
{
    if (!game) {
        return;
    }

    const vec3 remote_color = vec3_make(0.85f, 0.1f, 0.1f);
    const vec3 remote_scale = vec3_make(0.4f, game->config.player_height * 2.0f, 0.4f);
    const float base_radius = 3.6f;

    game->remote_entity_count = 0U;
    game->remote_visible_count = 0U;
    game->network_anim_time = 0.0;

    for (size_t i = 0; i < GAME_MAX_REMOTE_PLAYERS; ++i) {
        float angle = (float)i * ((float)M_PI * 2.0f / (float)GAME_MAX_REMOTE_PLAYERS);
        vec3 offset = vec3_make(cosf(angle) * base_radius, 0.0f, sinf(angle) * base_radius);
        vec3 position = vec3_add(game->player.position, offset);
        position.y = game->config.player_height;

        size_t index = game_world_add_entity(&game->world,
                                             ENTITY_TYPE_REMOTE_PLAYER,
                                             position,
                                             remote_scale,
                                             remote_color,
                                             false);
        if (index != SIZE_MAX && game->remote_entity_count < GAME_MAX_REMOTE_PLAYERS) {
            game->remote_entity_indices[game->remote_entity_count++] = index;
        }
    }
}


static bool game_entity_is_solid(const GameEntity *entity)
{
    return entity && entity->visible && entity->type == ENTITY_TYPE_STATIC;
}

static bool aabb_intersects(vec3 a_center, vec3 a_half, vec3 b_center, vec3 b_half)
{
    return fabsf(a_center.x - b_center.x) <= (a_half.x + b_half.x) + COLLISION_EPSILON &&
           fabsf(a_center.y - b_center.y) <= (a_half.y + b_half.y) + COLLISION_EPSILON &&
           fabsf(a_center.z - b_center.z) <= (a_half.z + b_half.z) + COLLISION_EPSILON;
}

static vec3 game_resolve_axis(GameState *game, vec3 current, vec3 half, float delta, float *velocity_component, int axis)
{
    if (delta == 0.0f) {
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

    for (size_t i = 0; i < game->world.entity_count; ++i) {
        const GameEntity *entity = &game->world.entities[i];
        if (!game_entity_is_solid(entity)) {
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
                game->player.grounded = true;
                game->player.double_jump_available = game->config.enable_double_jump;
                game->player.double_jump_timer = game->config.double_jump_window;
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
        float ground = game->world.ground_height + half.y;
        if (updated.y < ground) {
            if (delta <= 0.0f) {
                game->player.grounded = true;
                game->player.double_jump_available = game->config.enable_double_jump;
                game->player.double_jump_timer = game->config.double_jump_window;
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

static vec3 game_resolve_sweep(GameState *game, vec3 start, vec3 delta, vec3 half)
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
        float *velocity_component = player_velocity_axis(&game->player, axis);
        position = game_resolve_axis(game, position, half, move, velocity_component, axis);
    }

    return position;
}

static bool axis_pressed_positive(float current, float previous)
{
    return current > 0.5f && previous <= 0.5f;
}

static bool axis_pressed_negative(float current, float previous)
{
    return current < -0.5f && previous >= -0.5f;
}

static const char *weapon_item_display_name(WeaponItemType type)
{
    switch (type) {
    case WEAPON_ITEM_EXTENDED_MAG:
        return "Extended Mag";
    case WEAPON_ITEM_RECOIL_STABILIZER:
        return "Recoil Stabilizer";
    case WEAPON_ITEM_TRIGGER_TUNING:
        return "Trigger Tuning";
    default:
        break;
    }
    return "Attachment";
}

static void game_notify(GameState *game, const char *message)
{
    if (!game || !message) {
        return;
    }

    snprintf(game->hud_notification, sizeof(game->hud_notification), "%s", message);
    game->hud_notification_timer = 2.5f;
}

static void game_server_browser_init(GameState *game)
{
    if (!game) {
        return;
    }

    ServerBrowserState *browser = &game->server_browser;
    browser->open = false;
    browser->entry_count = 0;
    browser->selection = 0;
    browser->last_request_success = false;
    browser->status[0] = '\0';
    browser->last_refresh_time = 0.0;
}

static void game_server_browser_refresh(GameState *game)
{
    if (!game) {
        return;
    }

    ServerBrowserState *browser = &game->server_browser;
    size_t count = 0;
    bool success = network_fetch_master_list(&game->master_config,
                                             browser->entries,
                                             GAME_MAX_SERVER_LIST,
                                             &count);
    if (count > GAME_MAX_SERVER_LIST) {
        count = GAME_MAX_SERVER_LIST;
    }

    browser->entry_count = count;
    if (count == 0) {
        browser->selection = 0;
    } else if (browser->selection >= (int)count) {
        browser->selection = (int)count - 1;
    } else if (browser->selection < 0) {
        browser->selection = 0;
    }
    browser->last_request_success = success;
    browser->last_refresh_time = game->time_seconds;

    if (success) {
        if (count > 0) {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "Found %zu server%s.",
                     count,
                     (count == 1) ? "" : "s");
        } else {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "No servers currently available.");
        }
    } else {
        if (count > 0) {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "Master unreachable; showing fallback list (%zu).",
                     count);
        } else {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "Failed to contact master server.");
        }
    }
}

static void game_server_browser_open(GameState *game)
{
    if (!game) {
        return;
    }

    game->server_browser.selection = 0;
    game->server_browser.open = true;
    game_server_browser_refresh(game);
}

static bool game_connect_to_master_internal(GameState *game, const MasterServerEntry *entry, bool notify)
{
    if (!game || !entry) {
        return false;
    }

    if (!entry->address[0] || entry->port == 0) {
        if (notify) {
            game_notify(game, "Server entry incomplete.");
        }
        return false;
    }

    char previous_address[MASTER_SERVER_ADDR_MAX];
    strncpy(previous_address, game->current_server_address, sizeof(previous_address) - 1);
    previous_address[sizeof(previous_address) - 1] = '\0';
    uint16_t previous_port = game->current_server_port;
    bool previous_latency = game->network_config.simulate_latency;

    strncpy(game->current_server_address, entry->address, sizeof(game->current_server_address) - 1);
    game->current_server_address[sizeof(game->current_server_address) - 1] = '\0';
    game->current_server_port = entry->port;

    game->network_config.host = game->current_server_address;
    game->network_config.port = game->current_server_port;
    game->network_config.simulate_latency = false;

    NetworkClient *new_client = network_client_create(&game->network_config);
    if (!new_client) {
        strncpy(game->current_server_address, previous_address, sizeof(game->current_server_address) - 1);
        game->current_server_address[sizeof(game->current_server_address) - 1] = '\0';
        game->current_server_port = previous_port;
        game->network_config.host = game->current_server_address;
        game->network_config.port = game->current_server_port;
        game->network_config.simulate_latency = previous_latency;
        if (notify) {
            game_notify(game, "Failed to initialize network client.");
        }
        return false;
    }

    if (game->network) {
        network_client_destroy(game->network);
    }

    game->network = new_client;
    network_client_connect(game->network);

    if (notify) {
        char message[128];
        snprintf(message,
                 sizeof(message),
                 "Connecting to %s:%u",
                 game->current_server_address,
                 (unsigned)game->current_server_port);
        game_notify(game, message);
    }

    return true;
}

static void game_server_browser_join(GameState *game)
{
    if (!game) {
        return;
    }

    ServerBrowserState *browser = &game->server_browser;
    if (!browser->open || browser->entry_count == 0) {
        game_notify(game, "No server selected.");
        return;
    }

    int index = browser->selection;
    if (index < 0 || index >= (int)browser->entry_count) {
        game_notify(game, "No server selected.");
        return;
    }

    const MasterServerEntry *entry = &browser->entries[index];
    if (game_connect_to_master_internal(game, entry, true)) {
        browser->open = false;
        game->paused = false;
    }
}
bool game_connect_to_master_entry(GameState *game, const MasterServerEntry *entry)
{
    return game_connect_to_master_internal(game, entry, true);
}
static void game_process_jump(GameState *game, bool was_grounded, float dt)
{
    if (!game || game->config.allow_flight) {
        return;
    }

    PlayerState *player = &game->player;

    if (was_grounded) {
        player->double_jump_available = game->config.enable_double_jump;
        player->double_jump_timer = game->config.double_jump_window;
    } else if (player->double_jump_timer > 0.0f) {
        player->double_jump_timer -= dt;
        if (player->double_jump_timer < 0.0f) {
            player->double_jump_timer = 0.0f;
        }
    }

    if (!game->command.jump_requested) {
        return;
    }

    if (was_grounded) {
        player->velocity.y = game->config.jump_velocity;
        player->grounded = false;
        player->double_jump_available = game->config.enable_double_jump;
        player->double_jump_timer = game->config.double_jump_window;
        return;
    }

    if (game->config.enable_double_jump && player->double_jump_available && player->double_jump_timer > 0.0f) {
        player->velocity.y = game->config.jump_velocity;
        player->double_jump_available = false;
    }
}

static void game_update_view_bobbing(GameState *game, float dt)
{
    if (!game) {
        return;
    }

    PlayerState *player = &game->player;

    if (!game->config.enable_view_bobbing) {
        player->camera_offset = vec3_make(0.0f, 0.0f, 0.0f);
        game->camera.position = player->position;
        return;
    }

    vec3 horizontal_velocity = vec3_make(player->velocity.x, 0.0f, player->velocity.z);
    float speed = vec3_length(horizontal_velocity);

    if (speed > 0.2f && player->grounded) {
        player->bob_phase += game->config.view_bobbing_frequency * dt;
        if (player->bob_phase > (float)M_PI * 2.0f) {
            player->bob_phase -= (float)M_PI * 2.0f;
        }
        float bob = sinf(player->bob_phase) * game->config.view_bobbing_amplitude;
        float sway = cosf(player->bob_phase * 0.5f) * game->config.view_bobbing_amplitude * 0.35f;
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

    game->camera.position = vec3_add(player->position, player->camera_offset);
}

static void game_apply_movement(GameState *game, float dt)
{
    PlayerState *player = &game->player;
    const bool was_grounded = player->grounded;

    vec3 horizontal_velocity = vec3_make(player->velocity.x, 0.0f, player->velocity.z);
    const float target_speed = game->config.move_speed * (game->command.sprint ? game->config.sprint_multiplier : 1.0f);
    vec3 desired_velocity = vec3_scale(game->command.move_direction, target_speed);
    vec3 accel = vec3_sub(desired_velocity, horizontal_velocity);
    float accel_length = vec3_length(accel);
    float max_accel = (player->grounded ? game->config.ground_acceleration : game->config.air_control) * dt;
    if (accel_length > max_accel && accel_length > 0.0001f) {
        accel = vec3_scale(accel, max_accel / accel_length);
    }
    horizontal_velocity = vec3_add(horizontal_velocity, accel);

    if (player->grounded && game->command.move_magnitude < 0.01f) {
        float damping = expf(-game->config.ground_friction * dt);
        horizontal_velocity = vec3_scale(horizontal_velocity, damping);
    }

    player->velocity.x = horizontal_velocity.x;
    player->velocity.z = horizontal_velocity.z;

    if (game->config.allow_flight) {
        player->velocity.y = game->command.vertical_axis * target_speed;
        player->grounded = false;
        player->double_jump_available = false;
        player->double_jump_timer = 0.0f;
    } else {
        player->velocity.y -= game->config.gravity * dt;
        game_process_jump(game, was_grounded, dt);
        player->grounded = false;
    }

    vec3 displacement = vec3_scale(player->velocity, dt);
    const vec3 half = player->collider_half_extents;
    vec3 start_center = vec3_make(player->position.x, player->position.y - player->height * 0.5f, player->position.z);
    vec3 resolved_center = game_resolve_sweep(game, start_center, displacement, half);

    player->position = vec3_make(resolved_center.x, resolved_center.y + player->height * 0.5f, resolved_center.z);

    GameEntity *player_entity = game_world_entity(&game->world, game->player_entity_index);
    if (player_entity) {
        player_entity->position = player->position;
    }
}

static void game_update_weapon(GameState *game, float dt)
{
    if (!game) {
        return;
    }

    WeaponState *weapon = &game->weapon;
    if (weapon->cooldown > 0.0f) {
        weapon->cooldown -= dt;
        if (weapon->cooldown < 0.0f) {
            weapon->cooldown = 0.0f;
        }
    }

    if (weapon->recoil > 0.0f) {
        weapon->recoil -= weapon->recoil_recovery_rate * dt;
        if (weapon->recoil < 0.0f) {
            weapon->recoil = 0.0f;
        }
    }

    if (weapon->reloading) {
        weapon->reload_timer -= dt;
        if (weapon->reload_timer <= 0.0f) {
            weapon->reload_timer = 0.0f;
            weapon->reloading = false;

            int needed = weapon->clip_size - weapon->ammo_in_clip;
            if (needed > 0 && weapon->ammo_reserve > 0) {
                if (needed > weapon->ammo_reserve) {
                    needed = weapon->ammo_reserve;
                }
                weapon->ammo_in_clip += needed;
                weapon->ammo_reserve -= needed;
            }
        }
        return;
    }

    if (game->command.reload_requested && weapon->ammo_in_clip < weapon->clip_size && weapon->ammo_reserve > 0) {
        weapon->reloading = true;
        weapon->reload_timer = weapon->reload_time;
        return;
    }

    if (game->command.fire_down && weapon->cooldown <= 0.0f && weapon->ammo_in_clip > 0) {
        weapon->ammo_in_clip -= 1;
        weapon->cooldown = 1.0f / weapon->fire_rate;
        weapon->recoil += 3.5f;
        if (weapon->recoil > 12.0f) {
            weapon->recoil = 12.0f;
        }

        game->hud.damage_flash = 0.3f;

        if (weapon->ammo_in_clip == 0 && weapon->ammo_reserve > 0) {
            weapon->reloading = true;
            weapon->reload_timer = weapon->reload_time;
        }
    }
}

static void game_update_network(GameState *game, float dt)
{
    if (!game || !game->network) {
        return;
    }

    network_client_update(game->network, dt);
    game->network_anim_time += dt;

    const NetworkClientStats *stats = network_client_stats(game->network);

    uint32_t desired_visible = game->remote_visible_count;
    if (stats) {
        desired_visible = stats->remote_player_count;
        if (desired_visible > GAME_MAX_REMOTE_PLAYERS) {
            desired_visible = GAME_MAX_REMOTE_PLAYERS;
        }

        if (stats->time_since_last_packet > 2.5f && desired_visible > 0 && !stats->connected) {
            desired_visible = 0;
        }
    } else {
        desired_visible = 0;
    }

    if (desired_visible > game->remote_entity_count) {
        desired_visible = (uint32_t)game->remote_entity_count;
    }

    game->remote_visible_count = desired_visible;

    const vec3 remote_color = vec3_make(0.85f, 0.1f, 0.1f);
    const vec3 base_pos = game->player.position;
    const float base_radius = 3.6f;
    const float orbit_speed = 0.6f;

    for (size_t i = 0; i < game->remote_entity_count; ++i) {
        size_t index = game->remote_entity_indices[i];
        GameEntity *entity = game_world_entity(&game->world, index);
        if (!entity) {
            continue;
        }

        if (i < game->remote_visible_count) {
            float count = (game->remote_visible_count > 0U) ? (float)game->remote_visible_count : 1.0f;
            float angle = (float)game->network_anim_time * orbit_speed + ((float)i / count) * (float)(M_PI * 2.0f);
            float radius = base_radius + 0.4f * (float)i;
            float bob = 0.25f * sinf((float)game->network_anim_time * 1.4f + (float)i);

            entity->visible = true;
            entity->position.x = base_pos.x + cosf(angle) * radius;
            entity->position.y = game->config.player_height + bob;
            entity->position.z = base_pos.z + sinf(angle) * radius;
            entity->color = remote_color;
        } else {
            entity->visible = false;
        }
    }
}


GameState *game_create(const GameConfig *config, Renderer *renderer, PhysicsWorld *physics_world)
{
    if (!renderer || !physics_world) {
        return NULL;
    }

    GameState *game = (GameState *)calloc(1, sizeof(GameState));
    if (!game) {
        return NULL;
    }

    game->renderer = renderer;
    game->physics = physics_world;
    game->config = config ? *config : game_default_config();

    game_world_reset(&game->world);
    game->world.ground_height = 0.0f;

    vec3 player_start = vec3_make(0.0f, game->config.player_height, 6.0f);
    game->player.height = game->config.player_height;
    game->player.position = player_start;
    game->player.velocity = vec3_make(0.0f, 0.0f, 0.0f);
    game->player.collider_half_extents = vec3_make(PLAYER_COLLIDER_RADIUS, game->config.player_height * 0.5f, PLAYER_COLLIDER_RADIUS);
    game->player.camera_offset = vec3_make(0.0f, 0.0f, 0.0f);
    game->player.health = 100.0f;
    game->player.max_health = 100.0f;
    game->player.armor = 50.0f;
    game->player.max_armor = 100.0f;
    game->player.grounded = true;
    game->player.double_jump_available = game->config.enable_double_jump;
    game->player.double_jump_timer = game->config.double_jump_window;
    game->player.bob_phase = 0.0f;

    size_t player_index = game_world_add_entity(&game->world,
                                                ENTITY_TYPE_PLAYER,
                                                player_start,
                                                vec3_make(0.5f, game->config.player_height, 0.5f),
                                                vec3_make(0.2f, 0.2f, 0.3f),
                                                false);
    game->player_entity_index = (player_index == SIZE_MAX) ? 0U : player_index;

    game_spawn_level_geometry(&game->world);
    game_spawn_remote_players(game);
    game->network_anim_time = 0.0;

    const float aspect = 16.0f / 9.0f;
    game->camera = camera_create(player_start,
                                 0.0f,
                                 0.0f,
                                 (float)M_PI / 180.0f * CAMERA_DEFAULT_FOV_DEG,
                                 aspect,
                                 CAMERA_DEFAULT_NEAR,
                                 CAMERA_DEFAULT_FAR);
    camera_set_pitch_limits(&game->camera, -(float)M_PI_2 * 0.98f, (float)M_PI_2 * 0.98f);

    game->weapon.base_clip_size = 30;
    game->weapon.base_fire_rate = 10.0f;
    game->weapon.base_recoil_recovery_rate = 9.0f;
    game->weapon.clip_size = game->weapon.base_clip_size;
    game->weapon.ammo_in_clip = 30;
    game->weapon.ammo_reserve = 120;
    game->weapon.fire_rate = game->weapon.base_fire_rate;
    game->weapon.cooldown = 0.0f;
    game->weapon.reload_time = 1.6f;
    game->weapon.reload_timer = 0.0f;
    game->weapon.reloading = false;
    game->weapon.recoil = 0.0f;
    game->weapon.recoil_recovery_rate = game->weapon.base_recoil_recovery_rate;

    game->hud.crosshair_base = 12.0f;
    game->hud.crosshair_spread = game->hud.crosshair_base;
    game->hud.damage_flash = 0.0f;
    game->hud.network_indicator_timer = 0.0f;

    game_inventory_init(game);

    game->time_seconds = 0.0;
    game->session_time = 0.0;
    game->paused = false;
    game->options_open = false;
    game->pause_selection = 0;
    game->options_selection = 0;
    game->request_quit = false;
    snprintf(game->objective_text, sizeof(game->objective_text), "Secure the uplink");
    game->hud_notification[0] = '\0';
    game->hud_notification_timer = 0.0f;

    strncpy(game->current_server_address,
            "127.0.0.1",
            sizeof(game->current_server_address) - 1);
    game->current_server_address[sizeof(game->current_server_address) - 1] = '\0';
    game->current_server_port = 27015;

    strncpy(game->master_server_host,
            "127.0.0.1",
            sizeof(game->master_server_host) - 1);
    game->master_server_host[sizeof(game->master_server_host) - 1] = '\0';

    game->network_config.host = game->current_server_address;
    game->network_config.port = game->current_server_port;
    game->network_config.simulate_latency = true;

    game->master_config.host = game->master_server_host;
    game->master_config.port = 27050;

    game_server_browser_init(game);

    game->network = network_client_create(&game->network_config);
    if (!game->network) {
        game_notify(game, "Failed to initialize network client.");
    }

    return game;
}

void game_destroy(GameState *game)
{
    if (!game) {
        return;
    }

    if (game->network) {
        network_client_destroy(game->network);
        game->network = NULL;
    }

    free(game);
}

void game_resize(GameState *game, uint32_t width, uint32_t height)
{
    if (!game || !width || !height) {
        return;
    }

    const float aspect = (float)width / (float)height;
    camera_set_aspect(&game->camera, aspect);
}

void game_handle_input(GameState *game, const InputState *input, float dt)
{
    (void)dt;
    if (!game || !input) {
        return;
    }

    InputState previous_input = game->last_input;
    game->last_input = *input;

    game_reset_command(&game->command);

    if (input->escape_pressed) {
        if (game->paused) {
            if (game->options_open) {
                game->options_open = false;
            } else if (game->server_browser.open) {
                game->server_browser.open = false;
            } else {
                game->paused = false;
                game->pause_selection = 0;
            }
        } else {
            game->paused = true;
            game->pause_selection = 0;
            game->options_open = false;
            game->server_browser.open = false;
        }
    }

    if (game->paused) {
        bool move_up = axis_pressed_positive(input->move_forward, previous_input.move_forward) || input->mouse_wheel > 0.25f;
        bool move_down = axis_pressed_negative(input->move_forward, previous_input.move_forward) || input->mouse_wheel < -0.25f;

        if (game->options_open) {
            const int option_count = 4;
            if (move_up) {
                game->options_selection = (game->options_selection + option_count - 1) % option_count;
            }
            if (move_down) {
                game->options_selection = (game->options_selection + 1) % option_count;
            }

            if (input->fire_pressed || input->interact_pressed) {
                switch (game->options_selection) {
                case 0:
                    game->config.enable_view_bobbing = !game->config.enable_view_bobbing;
                    game_notify(game, game->config.enable_view_bobbing ? "View bobbing enabled" : "View bobbing disabled");
                    break;
                case 1:
                    game_set_double_jump_enabled(game, !game->config.enable_double_jump);
                    game_notify(game, game->config.enable_double_jump ? "Double jump enabled" : "Double jump disabled");
                    break;
                case 2:
                    game_notify(game, "Flight mode restricted to administrator console commands");
                    break;
                case 3:
                    game->options_open = false;
                    break;
                default:
                    break;
                }
            }
        } else if (game->server_browser.open) {
            size_t server_count = game->server_browser.entry_count;
            if (server_count > 0) {
                if (move_up) {
                    int selection = game->server_browser.selection;
                    selection = (selection + (int)server_count - 1) % (int)server_count;
                    game->server_browser.selection = selection;
                }
                if (move_down) {
                    int selection = game->server_browser.selection;
                    selection = (selection + 1) % (int)server_count;
                    game->server_browser.selection = selection;
                }
            }

            if (input->reload_pressed) {
                game_server_browser_refresh(game);
            }

            if (input->fire_pressed || input->interact_pressed) {
                if (server_count > 0) {
                    game_server_browser_join(game);
                } else {
                    game_server_browser_refresh(game);
                }
            }
        } else {
            const int menu_count = 4;
            if (move_up) {
                game->pause_selection = (game->pause_selection + menu_count - 1) % menu_count;
            }
            if (move_down) {
                game->pause_selection = (game->pause_selection + 1) % menu_count;
            }

            if (input->fire_pressed || input->interact_pressed) {
                switch (game->pause_selection) {
                case 0:
                    game->paused = false;
                    break;
                case 1:
                    game->options_open = true;
                    game->options_selection = 0;
                    break;
                case 2:
                    game_server_browser_open(game);
                    break;
                case 3:
                    game->request_quit = true;
                    break;
                default:
                    break;
                }
            }
        }

        return;
    }
    const float yaw_delta = input->look_delta_x * game->config.mouse_sensitivity;
    const float pitch_delta = input->look_delta_y * game->config.mouse_sensitivity;
    camera_add_yaw(&game->camera, yaw_delta);
    camera_add_pitch(&game->camera, pitch_delta);

    vec3 forward = camera_forward(&game->camera);
    forward.y = 0.0f;
    if (vec3_length(forward) > 0.0f) {
        forward = vec3_normalize(forward);
    }

    vec3 right = camera_right(&game->camera);
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

    game->command.move_direction = move;
    game->command.move_magnitude = magnitude;
    game->command.vertical_axis = game->config.allow_flight ? input->move_vertical : 0.0f;
    game->command.jump_requested = input->jump_pressed;
    game->command.sprint = input->sprinting;
    game->command.fire_down = input->fire_down;
    game->command.fire_pressed = input->fire_pressed;
    game->command.reload_requested = input->reload_pressed;
    game->command.interact_requested = input->interact_pressed;

    if (input->mouse_wheel > 0.1f) {
        game->command.weapon_slot_delta = 1;
    } else if (input->mouse_wheel < -0.1f) {
        game->command.weapon_slot_delta = -1;
    }
}

void game_update(GameState *game, float dt)
{
    if (!game) {
        return;
    }

    if (dt < 0.0f) {
        dt = 0.0f;
    }

    if (game->hud_notification_timer > 0.0f) {
        game->hud_notification_timer -= dt;
        if (game->hud_notification_timer < 0.0f) {
            game->hud_notification_timer = 0.0f;
        }
    }

    game->hud.network_indicator_timer += dt;

    game_update_network(game, dt);

    if (game->paused) {
        game->hud.crosshair_spread = game->hud.crosshair_base;
        game_update_view_bobbing(game, dt);

        const vec3 pos_paused = game->camera.position;
        const float paused_r = 0.05f + 0.45f * (0.5f + 0.5f * sinf(pos_paused.x * 0.35f));
        const float paused_g = 0.05f + 0.40f * (0.5f + 0.5f * sinf(pos_paused.y * 0.25f));
        const float paused_b = 0.10f + 0.45f * (0.5f + 0.5f * sinf(pos_paused.z * 0.35f));
        renderer_set_clear_color(game->renderer, paused_r, paused_g, paused_b, 1.0f);
        return;
    }

    game->time_seconds += (double)dt;
    game->session_time += (double)dt;

    physics_world_step(game->physics, dt);
    game_apply_movement(game, dt);
    game_update_weapon(game, dt);

    if (game->hud.damage_flash > 0.0f) {
        game->hud.damage_flash -= dt;
        if (game->hud.damage_flash < 0.0f) {
            game->hud.damage_flash = 0.0f;
        }
    }

    game->hud.crosshair_spread = game->hud.crosshair_base + game->weapon.recoil * 0.7f + game->command.move_magnitude * 6.0f;

    game_update_view_bobbing(game, dt);

    const vec3 pos = game->camera.position;
    const float color_r = 0.05f + 0.45f * (0.5f + 0.5f * sinf(pos.x * 0.35f));
    const float color_g = 0.05f + 0.40f * (0.5f + 0.5f * sinf(pos.y * 0.25f));
    const float color_b = 0.10f + 0.45f * (0.5f + 0.5f * sinf(pos.z * 0.35f));
    renderer_set_clear_color(game->renderer, color_r, color_g, color_b, 1.0f);
}

static void game_draw_pause_menu(GameState *game)
{
    if (!game) {
        return;
    }

    Renderer *renderer = game->renderer;
    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;

    renderer_begin_ui(renderer);

    renderer_draw_ui_rect(renderer, 0.0f, 0.0f, width, height, 0.02f, 0.02f, 0.04f, 0.65f);

    if (game->options_open) {
        const float panel_width = 520.0f;
        const float panel_height = 320.0f;
        const float panel_x = (width - panel_width) * 0.5f;
        const float panel_y = (height - panel_height) * 0.5f;
        renderer_draw_ui_rect(renderer, panel_x, panel_y, panel_width, panel_height, 0.04f, 0.04f, 0.06f, 0.9f);

        renderer_draw_ui_text(renderer, panel_x + 28.0f, panel_y + 36.0f, "Options", 0.95f, 0.95f, 0.95f, 1.0f);

        char option_lines[4][96];
        snprintf(option_lines[0], sizeof(option_lines[0]), "View bobbing: %s", game->config.enable_view_bobbing ? "ON" : "OFF");
        snprintf(option_lines[1], sizeof(option_lines[1]), "Double jump: %s", game->config.enable_double_jump ? "ON" : "OFF");
        snprintf(option_lines[2], sizeof(option_lines[2]), "Flight mode: %s", game->config.allow_flight ? "ON" : "OFF");
        snprintf(option_lines[3], sizeof(option_lines[3]), "Back");

        const int option_count = 4;
        const float item_height = 46.0f;
        float item_y = panel_y + 84.0f;
        for (int i = 0; i < option_count; ++i) {
            const bool selected = (i == game->options_selection);
            if (selected) {
                renderer_draw_ui_rect(renderer, panel_x + 20.0f, item_y - 8.0f, panel_width - 40.0f, item_height, 0.18f, 0.32f, 0.65f, 0.85f);
            }
            renderer_draw_ui_text(renderer, panel_x + 36.0f, item_y, option_lines[i], 0.95f, 0.95f, 0.95f, selected ? 1.0f : 0.8f);
            item_y += item_height;
        }

        renderer_draw_ui_text(renderer,
                              panel_x + 24.0f,
                              panel_y + panel_height - 48.0f,
                              "Use W/S or mouse wheel to navigate. Enter/Fire to toggle. Esc to return.",
                              0.85f,
                              0.85f,
                              0.85f,
                              0.85f);
    } else if (game->server_browser.open) {
                const float min_panel_width = 420.0f;
        const float min_panel_height = 360.0f;
        const float target_panel_width = width - 140.0f;
        const float target_panel_height = height - 180.0f;
        const float panel_width = (target_panel_width > min_panel_width) ? target_panel_width : min_panel_width;
        const float panel_height = (target_panel_height > min_panel_height) ? target_panel_height : min_panel_height;
        const float panel_x = (width - panel_width) * 0.5f;
        const float panel_y = (height - panel_height) * 0.5f;
        renderer_draw_ui_rect(renderer, panel_x, panel_y, panel_width, panel_height, 0.04f, 0.04f, 0.06f, 0.9f);

        renderer_draw_ui_text(renderer, panel_x + 32.0f, panel_y + 34.0f, "Server Browser", 0.95f, 0.95f, 0.95f, 1.0f);

        double elapsed = game->time_seconds - game->server_browser.last_refresh_time;
        if (elapsed < 0.0) {
            elapsed = 0.0;
        }

        char status_line[160];
        if (game->server_browser.status[0] != '\\0') {
            if (game->server_browser.last_refresh_time > 0.0) {
                snprintf(status_line,
                         sizeof(status_line),
                         "%s (updated %.1fs ago)",
                         game->server_browser.status,
                         (float)elapsed);
            } else {
                snprintf(status_line,
                         sizeof(status_line),
                         "%s",
                         game->server_browser.status);
            }
        } else {
            snprintf(status_line,
                     sizeof(status_line),
                     "Press R to refresh the server list.");
        }

        float status_r = game->server_browser.last_request_success ? 0.75f : 0.95f;
        float status_g = game->server_browser.last_request_success ? 0.95f : 0.7f;
        float status_b = game->server_browser.last_request_success ? 0.88f : 0.7f;
        renderer_draw_ui_text(renderer,
                              panel_x + 32.0f,
                              panel_y + 74.0f,
                              status_line,
                              status_r,
                              status_g,
                              status_b,
                              0.95f);

        const float list_x = panel_x + 40.0f;
        const float list_width = panel_width - 80.0f;
        const float row_height = 36.0f;
        const float header_y = panel_y + 126.0f;

        const float col_server = list_width * 0.45f;
        const float col_address = list_width * 0.30f;
        const float col_players = list_width * 0.12f;
        const float col_mode = list_width - (col_server + col_address + col_players);

        renderer_draw_ui_text(renderer, list_x, header_y, "Server", 0.85f, 0.85f, 0.95f, 0.9f);
        renderer_draw_ui_text(renderer, list_x + col_server, header_y, "Address", 0.85f, 0.85f, 0.95f, 0.9f);
        renderer_draw_ui_text(renderer, list_x + col_server + col_address, header_y, "Players", 0.85f, 0.85f, 0.95f, 0.9f);
        renderer_draw_ui_text(renderer, list_x + col_server + col_address + col_players, header_y, "Mode", 0.85f, 0.85f, 0.95f, 0.9f);
        size_t server_count = game->server_browser.entry_count;
        int selection = game->server_browser.selection;
        if (selection < 0) {
            selection = 0;
        }
        if (server_count == 0) {
            selection = 0;
        } else if (selection >= (int)server_count) {
            selection = (int)server_count - 1;
        }
        game->server_browser.selection = selection;

        const int max_visible = 10;
        int count = (int)server_count;
        int start = 0;
        if (count > max_visible) {
            start = selection - max_visible / 2;
            if (start < 0) {
                start = 0;
            }
            if (start + max_visible > count) {
                start = count - max_visible;
            }
        }
        int end = (count < max_visible) ? count : start + max_visible;

        float list_y = header_y + 32.0f;

        if (server_count == 0) {
            renderer_draw_ui_text(renderer,
                                  list_x,
                                  list_y,
                                  "No servers available. Press R to refresh.",
                                  0.8f,
                                  0.8f,
                                  0.9f,
                                  0.9f);
        } else {
            for (int i = start; i < end; ++i) {
                float item_y = list_y + (float)(i - start) * row_height;
                const bool selected = (i == selection);
                if (selected) {
                    renderer_draw_ui_rect(renderer,
                                          panel_x + 24.0f,
                                          item_y - 8.0f,
                                          panel_width - 48.0f,
                                          row_height + 4.0f,
                                          0.18f,
                                          0.32f,
                                          0.65f,
                                          0.85f);
                }

                const MasterServerEntry *entry = &game->server_browser.entries[i];
                const char *server_name = (entry->name[0] != '\0') ? entry->name : "Unnamed server";
                const char *address_text = (entry->address[0] != '\0') ? entry->address : "?";

                renderer_draw_ui_text(renderer,
                                      list_x,
                                      item_y,
                                      server_name,
                                      0.95f,
                                      0.95f,
                                      0.95f,
                                      selected ? 1.0f : 0.85f);

                char address_buffer[96];
                snprintf(address_buffer,
                         sizeof(address_buffer),
                         "%s:%u",
                         address_text,
                         (unsigned)entry->port);
                renderer_draw_ui_text(renderer,
                                      list_x + 320.0f,
                                      item_y,
                                      address_buffer,
                                      0.85f,
                                      0.9f,
                                      0.95f,
                                      selected ? 1.0f : 0.85f);

                char player_buffer[32];
                snprintf(player_buffer,
                         sizeof(player_buffer),
                         "%u/%u",
                         entry->players,
                         entry->max_players);
                renderer_draw_ui_text(renderer,
                                      list_x + 520.0f,
                                      item_y,
                                      player_buffer,
                                      0.9f,
                                      0.9f,
                                      0.95f,
                                      selected ? 1.0f : 0.85f);

                char mode_buffer[32];
                snprintf(mode_buffer,
                         sizeof(mode_buffer),
                         "Mode %u",
                         entry->mode);
                renderer_draw_ui_text(renderer,
                                      list_x + 620.0f,
                                      item_y,
                                      mode_buffer,
                                      0.8f,
                                      0.85f,
                                      0.95f,
                                      selected ? 1.0f : 0.85f);
            }
        }

        renderer_draw_ui_text(renderer,
                              panel_x + 28.0f,
                              panel_y + panel_height - 86.0f,
                              "W/S or mouse wheel to navigate the list.",
                              0.82f,
                              0.82f,
                              0.92f,
                              0.9f);
        renderer_draw_ui_text(renderer,
                              panel_x + 28.0f,
                              panel_y + panel_height - 56.0f,
                              "Enter/Fire to join. R to refresh. Esc to return.",
                              0.82f,
                              0.82f,
                              0.92f,
                              0.9f);
    } else {
        const float panel_width = 420.0f;
        const float panel_height = 300.0f;
        const float panel_x = (width - panel_width) * 0.5f;
        const float panel_y = (height - panel_height) * 0.5f;
        renderer_draw_ui_rect(renderer, panel_x, panel_y, panel_width, panel_height, 0.04f, 0.04f, 0.06f, 0.9f);

        renderer_draw_ui_text(renderer, panel_x + 28.0f, panel_y + 34.0f, "Game Paused", 0.95f, 0.95f, 0.95f, 1.0f);

        const char *menu_items[] = {
            "Resume mission",
            "Options",
            "Server browser",
            "Return to menu"
        };
        const int menu_count = 4;
        const float item_height = 48.0f;
        float item_y = panel_y + 86.0f;
        for (int i = 0; i < menu_count; ++i) {
            const bool selected = (i == game->pause_selection);
            if (selected) {
                renderer_draw_ui_rect(renderer, panel_x + 20.0f, item_y - 10.0f, panel_width - 40.0f, item_height, 0.22f, 0.38f, 0.75f, 0.9f);
            }
            renderer_draw_ui_text(renderer, panel_x + 36.0f, item_y, menu_items[i], 0.95f, 0.95f, 0.95f, selected ? 1.0f : 0.85f);
            item_y += item_height;
        }

        renderer_draw_ui_text(renderer,
                              panel_x + 24.0f,
                              panel_y + panel_height - 56.0f,
                              "W/S or mouse wheel to navigate. Enter/Fire to select. Esc to resume.",
                              0.85f,
                              0.85f,
                              0.85f,
                              0.85f);
    }

    renderer_end_ui(renderer);
}

static void game_draw_world(const GameState *game)
{
    if (!game) {
        return;
    }

    renderer_draw_grid(game->renderer, 32.0f, 1.0f, game->world.ground_height);

    for (size_t i = 0; i < game->world.entity_count; ++i) {
        const GameEntity *entity = game_world_entity_const(&game->world, i);
        if (!entity || !entity->visible || entity->type == ENTITY_TYPE_PLAYER) {
            continue;
        }

        vec3 half_extents = vec3_scale(entity->scale, 0.5f);
        renderer_draw_box(game->renderer, entity->position, half_extents, entity->color);
    }
}

static void game_draw_ui(GameState *game)
{
    if (!game) {
        return;
    }

    Renderer *renderer = game->renderer;
    renderer_begin_ui(renderer);

    const uint32_t vp_width = renderer_viewport_width(renderer);
    const uint32_t vp_height = renderer_viewport_height(renderer);
    const float width = (float)vp_width;
    const float height = (float)vp_height;
    const float hud_alpha = game->paused ? 0.5f : 1.0f;

    const PlayerState *player = &game->player;
    const WeaponState *weapon = &game->weapon;

    const float margin = 28.0f;

    char buffer[192];

    /* Top-left objective and status panel */
    renderer_draw_ui_rect(renderer, margin - 20.0f, margin - 20.0f, 320.0f, 110.0f, 0.05f, 0.05f, 0.07f, 0.65f * hud_alpha);
    int minutes = (int)(game->session_time / 60.0);
    int seconds = (int)fmod(game->session_time, 60.0);
    snprintf(buffer, sizeof(buffer),
         "Objective: %s
"
         "Elapsed: %02d:%02d
"
         "Sprint: %s",
         game->objective_text,
         minutes,
         seconds,
         game->command.sprint ? "Active" : "Ready");
    renderer_draw_ui_text(renderer, margin - 8.0f, margin + 4.0f, buffer, 0.95f, 0.95f, 0.95f, 0.92f * hud_alpha);

    /* Health & armour */
    const float health_panel_y = height - 160.0f;
    renderer_draw_ui_rect(renderer, margin - 20.0f, health_panel_y, 320.0f, 120.0f, 0.05f, 0.05f, 0.07f, 0.7f * hud_alpha);

    float health_ratio = (player->max_health > 0.0f) ? (player->health / player->max_health) : 0.0f;
    if (health_ratio < 0.0f) {
        health_ratio = 0.0f;
    }
    if (health_ratio > 1.0f) {
        health_ratio = 1.0f;
    }
    renderer_draw_ui_rect(renderer, margin, health_panel_y + 24.0f, 240.0f, 24.0f, 0.20f, 0.05f, 0.05f, 0.85f * hud_alpha);
    renderer_draw_ui_rect(renderer, margin, health_panel_y + 24.0f, 240.0f * health_ratio, 24.0f, 0.85f, 0.22f, 0.22f, 0.95f * hud_alpha);
    snprintf(buffer, sizeof(buffer), "Health: %03.0f / %03.0f", player->health, player->max_health);
    renderer_draw_ui_text(renderer, margin + 4.0f, health_panel_y + 44.0f, buffer, 0.98f, 0.94f, 0.94f, 0.95f * hud_alpha);

    float armor_ratio = (player->max_armor > 0.0f) ? (player->armor / player->max_armor) : 0.0f;
    if (armor_ratio < 0.0f) {
        armor_ratio = 0.0f;
    }
    if (armor_ratio > 1.0f) {
        armor_ratio = 1.0f;
    }
    renderer_draw_ui_rect(renderer, margin, health_panel_y + 72.0f, 240.0f, 18.0f, 0.08f, 0.18f, 0.32f, 0.8f * hud_alpha);
    renderer_draw_ui_rect(renderer, margin, health_panel_y + 72.0f, 240.0f * armor_ratio, 18.0f, 0.25f, 0.55f, 0.95f, 0.9f * hud_alpha);
    snprintf(buffer, sizeof(buffer), "Armor: %03.0f / %03.0f", player->armor, player->max_armor);
    renderer_draw_ui_text(renderer, margin + 4.0f, health_panel_y + 90.0f, buffer, 0.88f, 0.92f, 0.98f, 0.95f * hud_alpha);

    /* Bottom-right weapon + ammo panel */
    const float weapon_panel_width = 300.0f;
    const float weapon_panel_height = 140.0f;
    const float weapon_panel_x = width - weapon_panel_width - margin + 20.0f;
    const float weapon_panel_y = height - weapon_panel_height - margin + 12.0f;
    renderer_draw_ui_rect(renderer, weapon_panel_x, weapon_panel_y, weapon_panel_width, weapon_panel_height, 0.05f, 0.05f, 0.07f, 0.7f * hud_alpha);

    snprintf(buffer, sizeof(buffer), "Clip: %02d / %02d", weapon->ammo_in_clip, weapon->clip_size);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 22.0f, buffer, 0.95f, 0.88f, 0.50f, 0.95f * hud_alpha);
    snprintf(buffer, sizeof(buffer), "Reserve: %03d", weapon->ammo_reserve);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 46.0f, buffer, 0.85f, 0.85f, 0.85f, 0.92f * hud_alpha);

    char attachment_names[160];
    attachment_names[0] = '\0';
    size_t attachment_offset = 0;
    size_t attachment_count = 0;
    for (size_t i = 0; i < game->inventory.weapon_item_count; ++i) {
        const WeaponItem *item = &game->inventory.weapon_items[i];
        if (!item->equipped) {
            continue;
        }
        const char *name = weapon_item_display_name(item->type);
        if (attachment_count > 0 && attachment_offset < sizeof(attachment_names) - 2) {
            attachment_names[attachment_offset++] = ',';
            attachment_names[attachment_offset++] = ' ';
        }
        attachment_offset += (size_t)snprintf(attachment_names + attachment_offset,
                                              sizeof(attachment_names) - attachment_offset,
                                              "%s",
                                              name);
        if (attachment_offset >= sizeof(attachment_names)) {
            attachment_offset = sizeof(attachment_names) - 1;
        }
        attachment_names[attachment_offset] = '\0';
        ++attachment_count;
    }

    if (attachment_count == 0) {
        snprintf(buffer, sizeof(buffer), "Attachments: none");
    } else {
        snprintf(buffer, sizeof(buffer), "Attachments: %s", attachment_names);
    }
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 72.0f, buffer, 0.82f, 0.82f, 0.86f, 0.9f * hud_alpha);

    snprintf(buffer, sizeof(buffer), "Recoil: %.1f  Fire rate: %.1f/s", weapon->recoil, weapon->fire_rate);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 98.0f, buffer, 0.78f, 0.86f, 0.98f, 0.88f * hud_alpha);

    /* Top-right network panel */
    const float net_panel_width = 240.0f;
    renderer_draw_ui_rect(renderer, width - net_panel_width - margin + 12.0f, margin - 20.0f, net_panel_width, 110.0f, 0.05f, 0.05f, 0.07f, 0.68f * hud_alpha);
    const NetworkClientStats *net_stats = game_network_stats(game);
    if (net_stats) {
        snprintf(buffer, sizeof(buffer), "Connection: %s", net_stats->connected ? "Online" : "Offline");
        renderer_draw_ui_text(renderer, width - net_panel_width - margin + 24.0f, margin + 0.0f, buffer, 0.85f, 0.95f, 0.85f, 0.95f * hud_alpha);
        snprintf(buffer, sizeof(buffer), "Ping: %.0f ms", net_stats->simulated_ping_ms);
        renderer_draw_ui_text(renderer, width - net_panel_width - margin + 24.0f, margin + 22.0f, buffer, 0.85f, 0.85f, 0.95f, 0.92f * hud_alpha);
        snprintf(buffer, sizeof(buffer), "Players: %u", net_stats->remote_player_count + 1U);
        renderer_draw_ui_text(renderer, width - net_panel_width - margin + 24.0f, margin + 44.0f, buffer, 0.85f, 0.85f, 0.95f, 0.92f * hud_alpha);
        snprintf(buffer, sizeof(buffer), "Last packet: %.1fs", net_stats->time_since_last_packet);
        renderer_draw_ui_text(renderer, width - net_panel_width - margin + 24.0f, margin + 66.0f, buffer, 0.8f, 0.8f, 0.9f, 0.88f * hud_alpha);
    } else {
        renderer_draw_ui_text(renderer, width - net_panel_width - margin + 24.0f, margin + 16.0f, "Connection: offline", 0.85f, 0.5f, 0.5f, 0.95f * hud_alpha);
    }

    /* Bottom-center movement aids */
    const char *jump_status = (player->grounded || player->double_jump_available) ? "Ready" : "Cooling";
    snprintf(buffer, sizeof(buffer), "Double jump: %s", jump_status);
    renderer_draw_ui_text(renderer, width * 0.5f - 80.0f, height - 96.0f, buffer, 0.88f, 0.88f, 0.95f, 0.9f * hud_alpha);

    if (game->hud_notification_timer > 0.0f) {
        renderer_draw_ui_rect(renderer, width * 0.5f - 200.0f, margin, 400.0f, 36.0f, 0.02f, 0.02f, 0.02f, 0.55f * hud_alpha);
        renderer_draw_ui_text(renderer, width * 0.5f - 180.0f, margin + 10.0f, game->hud_notification, 0.95f, 0.95f, 0.95f, 0.95f * hud_alpha);
    }

    if (!game->paused) {
        renderer_draw_crosshair(renderer, width * 0.5f, height * 0.5f, 16.0f, game->hud.crosshair_spread, 2.5f);
    }

    renderer_end_ui(renderer);
}

void game_render(GameState *game)
{
    if (!game || !game->renderer) {
        return;
    }

    renderer_begin_scene(game->renderer, &game->camera);
    game_draw_world(game);
    renderer_draw_weapon_viewmodel(game->renderer, game->weapon.recoil + (game->weapon.reloading ? 2.0f : 0.0f));
    game_draw_ui(game);

    if (game->paused) {
        game_draw_pause_menu(game);
    }
}

const Camera *game_camera(const GameState *game)
{
    if (!game) {
        return NULL;
    }
    return &game->camera;
}

const NetworkClientStats *game_network_stats(const GameState *game)
{
    if (!game) {
        return NULL;
    }
    return game->network ? network_client_stats(game->network) : NULL;
}

void game_set_double_jump_enabled(GameState *game, bool enabled)
{
    if (!game) {
        return;
    }

    game->config.enable_double_jump = enabled;
    if (!enabled) {
        game->player.double_jump_available = false;
        game->player.double_jump_timer = 0.0f;
    } else if (game->player.grounded) {
        game->player.double_jump_available = true;
        game->player.double_jump_timer = game->config.double_jump_window;
    }
}



















bool game_is_paused(const GameState *game)
{
    return game ? game->paused : false;
}

bool game_should_quit(const GameState *game)
{
    return game ? game->request_quit : false;
}

void game_clear_quit_request(GameState *game)
{
    if (game) {
        game->request_quit = false;
    }
}














































