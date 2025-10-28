#include "engine/game.h"

#include "engine/audio.h"
#include "engine/hud.h"
#include "engine/network.h"
#include "engine/player.h"
#include "engine/preferences.h"
#include "engine/server_browser.h"
#include "engine/settings_menu.h"
#include "engine/weapons.h"
#include "engine/world.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#    define M_PI_2 (M_PI / 2.0)
#endif

#define GAME_VOICE_CAPTURE_SAMPLES 480U
#define GAME_VOICE_DEFAULT_SAMPLE_RATE 16000U

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
    WeaponId highlighted_pickup_id;
    size_t highlighted_pickup_index;
    uint32_t highlighted_pickup_network_id;
    bool pickup_in_range;
    float pickup_distance;
    InputState last_input;
    ServerBrowserState server_browser;
    SettingsMenuState settings_menu;
    NetworkClientConfig network_config;
    MasterClientConfig master_config;
    char current_server_address[MASTER_SERVER_ADDR_MAX];
    uint16_t current_server_port;
    char master_server_host[MASTER_SERVER_ADDR_MAX];

    size_t player_entity_index;
    size_t remote_entity_indices[GAME_MAX_REMOTE_PLAYERS];
    uint8_t remote_entity_ids[GAME_MAX_REMOTE_PLAYERS];
    char remote_entity_names[GAME_MAX_REMOTE_PLAYERS][NETWORK_MAX_PLAYER_NAME];
    size_t remote_entity_count;
    uint32_t next_local_pickup_sequence;

    double time_seconds;
    double session_time;

    bool paused;
    bool options_open;
    int pause_selection;

    bool request_quit;

    char objective_text[64];
    char hud_notification[96];
    float hud_notification_timer;
    int16_t voice_capture_buffer[NETWORK_VOICE_MAX_DATA / sizeof(int16_t)];
    size_t voice_capture_sample_count;
    bool voice_capture_available;
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

static void game_apply_inventory(GameState *game)
{
    if (!game) {
        return;
    }

    inventory_apply_equipped(&game->inventory, &game->weapon);
}

static void game_inventory_init(GameState *game)
{
    if (!game) {
        return;
    }

    inventory_init(&game->inventory);
    game_apply_inventory(game);
}

static void game_clear_remote_entities(GameState *game)
{
    if (!game) {
        return;
    }

    memset(game->remote_entity_ids, 0xFF, sizeof(game->remote_entity_ids));
    memset(game->remote_entity_names, 0, sizeof(game->remote_entity_names));

    for (size_t i = 0; i < game->remote_entity_count && i < GAME_MAX_REMOTE_PLAYERS; ++i) {
        size_t entity_index = game->remote_entity_indices[i];
        GameEntity *entity = world_get_entity(&game->world, entity_index);
        if (entity) {
            entity->visible = false;
        }
    }
}

static size_t game_find_remote_slot(const GameState *game, uint8_t id)
{
    if (!game) {
        return SIZE_MAX;
    }

    for (size_t i = 0; i < game->remote_entity_count && i < GAME_MAX_REMOTE_PLAYERS; ++i) {
        if (game->remote_entity_ids[i] == id) {
            return i;
        }
    }

    return SIZE_MAX;
}

static size_t game_acquire_remote_slot(GameState *game, uint8_t id)
{
    if (!game) {
        return SIZE_MAX;
    }

    size_t existing = game_find_remote_slot(game, id);
    if (existing != SIZE_MAX) {
        return existing;
    }

    for (size_t i = 0; i < game->remote_entity_count && i < GAME_MAX_REMOTE_PLAYERS; ++i) {
        if (game->remote_entity_ids[i] == 0xFF) {
            game->remote_entity_ids[i] = id;
            game->remote_entity_names[i][0] = '\0';
            return i;
        }
    }

    return SIZE_MAX;
}

static void game_release_remote_slot(GameState *game, size_t slot)
{
    if (!game || slot >= game->remote_entity_count || slot >= GAME_MAX_REMOTE_PLAYERS) {
        return;
    }

    uint8_t released_id = game->remote_entity_ids[slot];
    size_t entity_index = game->remote_entity_indices[slot];
    GameEntity *entity = world_get_entity(&game->world, entity_index);
    if (entity) {
        entity->visible = false;
    }

    game->remote_entity_ids[slot] = 0xFF;
    game->remote_entity_names[slot][0] = '\0';

    if (released_id != 0xFF) {
        audio_voice_stop(released_id);
    }
}

static void game_setup_world(GameState *game)
{
    if (!game) {
        return;
    }

    world_spawn_default_geometry(&game->world);
    world_spawn_default_weapon_pickups(&game->world);
    game->remote_entity_count = world_spawn_remote_players(&game->world,
                                                           &game->config,
                                                           GAME_MAX_REMOTE_PLAYERS,
                                                           game->remote_entity_indices);
    game_clear_remote_entities(game);
    game->next_local_pickup_sequence = 1U;
}

static bool axis_pressed_positive(float current, float previous)
{
    return current > 0.5f && previous <= 0.5f;
}

static bool axis_pressed_negative(float current, float previous)
{
    return current < -0.5f && previous >= -0.5f;
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
    if (game) {
        server_browser_init(&game->server_browser);
    }
}

static void game_server_browser_refresh(GameState *game)
{
    if (!game) {
        return;
    }

    server_browser_refresh(&game->server_browser, &game->master_config, game->time_seconds);
}

static bool game_server_browser_open(GameState *game)
{
    if (!game) {
        return false;
    }

    return server_browser_open(&game->server_browser, &game->master_config, game->time_seconds);
}

static bool game_server_replace_client(GameState *game, NetworkClient *new_client)
{
    if (!game || !new_client) {
        return false;
    }

    if (game->network) {
        network_client_destroy(game->network);
    }
    game->network = new_client;
    network_client_connect(game->network);
    return true;
}

static bool game_connect_to_entry(GameState *game, const MasterServerEntry *entry)
{
    if (!game || !entry || entry->address[0] == '\0' || entry->port == 0) {
        return false;
    }

    char previous_address[MASTER_SERVER_ADDR_MAX];
    strncpy(previous_address, game->current_server_address, sizeof(previous_address) - 1);
    previous_address[sizeof(previous_address) - 1] = '\0';
    uint16_t previous_port = game->current_server_port;

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
        return false;
    }

    game_server_replace_client(game, new_client);

    char message[128];
    snprintf(message,
             sizeof(message),
             "Connecting to %s:%u",
             game->current_server_address,
             (unsigned)game->current_server_port);
    game_notify(game, message);

    return true;
}

static void game_server_browser_join(GameState *game)
{
    if (!game) {
        return;
    }

    const MasterServerEntry *entry = server_browser_selected(&game->server_browser);
    if (!entry) {
        game_notify(game, "No server selected.");
        return;
    }

    if (!game_connect_to_entry(game, entry)) {
        game_notify(game, "Failed to initialize network client.");
        return;
    }

    game->server_browser.open = false;
    game->paused = false;
}

static vec3 game_flat_forward(const GameState *game)
{
    if (!game) {
        return vec3_make(0.0f, 0.0f, -1.0f);
    }

    vec3 forward = camera_forward(&game->camera);
    forward.y = 0.0f;
    float length = vec3_length(forward);
    if (length < 0.0001f) {
        return vec3_make(0.0f, 0.0f, -1.0f);
    }
    return vec3_scale(forward, 1.0f / length);
}

static vec3 game_flat_right(const GameState *game)
{
    if (!game) {
        return vec3_make(1.0f, 0.0f, 0.0f);
    }

    vec3 right = camera_right(&game->camera);
    right.y = 0.0f;
    float length = vec3_length(right);
    if (length < 0.0001f) {
        return vec3_make(1.0f, 0.0f, 0.0f);
    }
    return vec3_scale(right, 1.0f / length);
}

static uint32_t game_generate_pickup_id(GameState *game)
{
    if (!game) {
        return 0U;
    }

    uint32_t sequence = game->next_local_pickup_sequence++;
    uint8_t self_id = 0xFF;
    if (game->network) {
        self_id = network_client_self_id(game->network);
    }
    if (self_id == 0xFF) {
        self_id = 0xFE;
    }

    uint32_t prefix = 0x01000000u | ((uint32_t)self_id << 16);
    return prefix | (sequence & 0x0000FFFFu);
}

static void game_send_weapon_drop_event(GameState *game, const WeaponPickup *pickup)
{
    if (!game || !pickup || !game->network || pickup->network_id == 0) {
        return;
    }

    NetworkWeaponEvent event;
    memset(&event, 0, sizeof(event));
    event.type = NETWORK_WEAPON_EVENT_DROP;
    event.pickup_id = pickup->network_id;
    event.weapon_id = (uint16_t)pickup->weapon_id;
    int clip = pickup->ammo_in_clip;
    if (clip > INT16_MAX) {
        clip = INT16_MAX;
    } else if (clip < INT16_MIN) {
        clip = INT16_MIN;
    }
    int reserve = pickup->ammo_reserve;
    if (reserve > INT16_MAX) {
        reserve = INT16_MAX;
    } else if (reserve < INT16_MIN) {
        reserve = INT16_MIN;
    }
    event.ammo_in_clip = (int16_t)clip;
    event.ammo_reserve = (int16_t)reserve;

    vec3 position = pickup->base_position;
    const GameEntity *entity = world_find_entity(&game->world, pickup->entity_id);
    if (entity) {
        position = entity->position;
    }

    event.position[0] = position.x;
    event.position[1] = position.y;
    event.position[2] = position.z;

    (void)network_client_send_weapon_event(game->network, &event);
}

static void game_send_weapon_pickup_event(GameState *game, uint32_t pickup_id)
{
    if (!game || !game->network || pickup_id == 0) {
        return;
    }

    NetworkWeaponEvent event;
    memset(&event, 0, sizeof(event));
    event.type = NETWORK_WEAPON_EVENT_PICKUP;
    event.pickup_id = pickup_id;
    (void)network_client_send_weapon_event(game->network, &event);
}

static void game_drop_current_weapon(GameState *game)
{
    if (!game) {
        return;
    }

    if (weapon_state_is_unarmed(&game->weapon)) {
        game_notify(game, "No weapon equipped.");
        return;
    }

    const char *weapon_name = weapon_state_display_name(&game->weapon);
    vec3 forward = game_flat_forward(game);
    vec3 drop_position = vec3_add(game->player.position, vec3_scale(forward, 1.4f));
    drop_position.y = game->world.ground_height + 0.35f;

    uint32_t pickup_id = game_generate_pickup_id(game);
    WeaponPickup *pickup = world_spawn_weapon_pickup(&game->world,
                                                     weapon_state_id(&game->weapon),
                                                     drop_position,
                                                     game->weapon.ammo_in_clip,
                                                     game->weapon.ammo_reserve,
                                                     pickup_id);
    if (!pickup) {
        game_notify(game, "Can't drop the weapon here.");
        return;
    }

    game_send_weapon_drop_event(game, pickup);

    weapon_state_equip(&game->weapon, WEAPON_ID_NONE, 0, 0);
    game_apply_inventory(game);

    char message[96];
    snprintf(message, sizeof(message), "Dropped %s", weapon_name);
    game_notify(game, message);

    game->highlighted_pickup_index = SIZE_MAX;
    game->highlighted_pickup_id = WEAPON_ID_NONE;
    game->highlighted_pickup_network_id = 0;
    game->pickup_in_range = false;
    game->pickup_distance = 0.0f;
}

static void game_pickup_weapon(GameState *game, size_t pickup_index)
{
    if (!game) {
        return;
    }

    WeaponPickup *pickup = world_get_weapon_pickup(&game->world, pickup_index);
    if (!pickup) {
        return;
    }

    WeaponId new_id = pickup->weapon_id;
    int new_clip = pickup->ammo_in_clip;
    int new_reserve = pickup->ammo_reserve;
    uint32_t pickup_network_id = pickup->network_id;

    if (!weapon_state_is_unarmed(&game->weapon)) {
        vec3 right = game_flat_right(game);
        vec3 drop_pos = vec3_add(game->player.position, vec3_scale(right, 1.0f));
        drop_pos.y = game->world.ground_height + 0.32f;

        uint32_t drop_id = game_generate_pickup_id(game);
        WeaponPickup *dropped = world_spawn_weapon_pickup(&game->world,
                                                          weapon_state_id(&game->weapon),
                                                          drop_pos,
                                                          game->weapon.ammo_in_clip,
                                                          game->weapon.ammo_reserve,
                                                          drop_id);
        if (!dropped) {
            game_notify(game, "No room to swap weapons.");
            return;
        }

        game_send_weapon_drop_event(game, dropped);
    }

    weapon_state_equip(&game->weapon, new_id, new_clip, new_reserve);
    game_apply_inventory(game);

    if (!world_remove_weapon_pickup(&game->world, pickup_index)) {
        (void)world_remove_weapon_pickup_by_id(&game->world, pickup_network_id);
    }
    game_send_weapon_pickup_event(game, pickup_network_id);

    const char *equipped_name = weapon_state_display_name(&game->weapon);
    char message[96];
    snprintf(message, sizeof(message), "Equipped %s", equipped_name);
    game_notify(game, message);

    game->highlighted_pickup_index = SIZE_MAX;
    game->highlighted_pickup_id = WEAPON_ID_NONE;
    game->highlighted_pickup_network_id = 0;
    game->pickup_in_range = false;
    game->pickup_distance = 0.0f;
}

static void game_update_weapon_pickups(GameState *game)
{
    if (!game) {
        return;
    }

    size_t pickup_index = SIZE_MAX;
    WeaponPickup *pickup = world_find_nearest_weapon_pickup(&game->world,
                                                           game->player.position,
                                                           1.8f,
                                                           &pickup_index);

    if (!pickup) {
        game->highlighted_pickup_index = SIZE_MAX;
        game->highlighted_pickup_id = WEAPON_ID_NONE;
        game->highlighted_pickup_network_id = 0;
        game->pickup_in_range = false;
        game->pickup_distance = 0.0f;
        return;
    }

    const GameEntity *entity = world_find_entity(&game->world, pickup->entity_id);
    vec3 pickup_pos = entity ? entity->position : pickup->base_position;
    vec3 delta = vec3_sub(pickup_pos, game->player.position);

    game->highlighted_pickup_index = pickup_index;
    game->highlighted_pickup_id = pickup->weapon_id;
    game->highlighted_pickup_network_id = pickup->network_id;
    game->pickup_in_range = true;
    game->pickup_distance = vec3_length(delta);

    if (game->command.interact_requested) {
        game_pickup_weapon(game, pickup_index);
    }
}

static void game_process_weapon_events(GameState *game)
{
    if (!game || !game->network) {
        return;
    }

    const uint8_t self_id = network_client_self_id(game->network);
    const size_t buffer_size = 16;
    NetworkWeaponEvent events[16];

    for (;;) {
        size_t count = network_client_dequeue_weapon_events(game->network, events, buffer_size);
        if (count == 0) {
            break;
        }

        for (size_t i = 0; i < count; ++i) {
            const NetworkWeaponEvent *event = &events[i];
            if (event->actor_id == self_id) {
                continue;
            }

            switch (event->type) {
            case NETWORK_WEAPON_EVENT_DROP: {
                WeaponId weapon_id = (WeaponId)event->weapon_id;
                if (weapon_id <= WEAPON_ID_NONE || weapon_id >= WEAPON_ID_COUNT) {
                    continue;
                }
                vec3 position = vec3_make(event->position[0], event->position[1], event->position[2]);
                int ammo_clip = event->ammo_in_clip;
                if (ammo_clip < 0) {
                    ammo_clip = 0;
                }
                int ammo_reserve = event->ammo_reserve;
                if (ammo_reserve < 0) {
                    ammo_reserve = 0;
                }

                WeaponPickup *pickup = world_spawn_weapon_pickup(&game->world,
                                                                  weapon_id,
                                                                  position,
                                                                  ammo_clip,
                                                                  ammo_reserve,
                                                                  event->pickup_id);
                if (pickup) {
                    GameEntity *entity = world_find_entity(&game->world, pickup->entity_id);
                    if (entity) {
                        entity->position = position;
                        pickup->base_position = position;
                    }
                }
                break;
            }
            case NETWORK_WEAPON_EVENT_PICKUP: {
                size_t index = SIZE_MAX;
                WeaponPickup *pickup = world_find_weapon_pickup_by_id(&game->world, event->pickup_id, &index);
                if (!pickup) {
                    break;
                }

                world_remove_weapon_pickup(&game->world, index);

                game->highlighted_pickup_index = SIZE_MAX;
                game->highlighted_pickup_id = WEAPON_ID_NONE;
                game->highlighted_pickup_network_id = 0;
                game->pickup_in_range = false;
                game->pickup_distance = 0.0f;
                break;
            }
            default:
                break;
            }
        }

        if (count < buffer_size) {
            break;
        }
    }
}

static void game_process_voice_packets(GameState *game)
{
    if (!game || !game->network) {
        return;
    }

    enum { VOICE_PACKET_BUFFER = 8 };
    NetworkVoicePacket packets[VOICE_PACKET_BUFFER];

    for (;;) {
        size_t count = network_client_dequeue_voice_packets(game->network, packets, VOICE_PACKET_BUFFER);
        if (count == 0U) {
            break;
        }

        for (size_t i = 0; i < count; ++i) {
            const NetworkVoicePacket *packet = &packets[i];
            if (!packet || packet->codec != NETWORK_VOICE_CODEC_PCM16 || packet->data_size == 0U) {
                continue;
            }

            size_t expected_size = (size_t)packet->frame_count * packet->channels * sizeof(int16_t);
            if (packet->channels == 0U ||
                packet->channels > NETWORK_VOICE_MAX_CHANNELS ||
                expected_size == 0U ||
                expected_size > NETWORK_VOICE_MAX_DATA ||
                packet->data_size != expected_size) {
                continue;
            }

            int16_t sample_buffer[NETWORK_VOICE_MAX_DATA / sizeof(int16_t)];
            memcpy(sample_buffer, packet->data, packet->data_size);

            float playback_volume = packet->volume;
            if (playback_volume <= 0.0f) {
                playback_volume = 1.0f;
            } else if (playback_volume > 1.0f) {
                playback_volume = 1.0f;
            }

            AudioVoiceFrame frame = {
                .samples = sample_buffer,
                .sample_count = packet->frame_count,
                .sample_rate = packet->sample_rate,
                .channels = packet->channels,
                .volume = playback_volume,
            };

            audio_voice_submit(packet->speaker_id, &frame);
        }

        if (count < VOICE_PACKET_BUFFER) {
            break;
        }
    }
}

static void game_update_voice_chat(GameState *game, float dt)
{
    (void)dt;
    if (!game || !game->network) {
        return;
    }

    const NetworkClientStats *stats = network_client_stats(game->network);
    if (!stats || !stats->connected) {
        return;
    }

    if (!audio_microphone_active()) {
        bool started = audio_microphone_start();
        game->voice_capture_available = started;
        game->voice_capture_sample_count = 0U;
        if (!started) {
            return;
        }
    } else {
        game->voice_capture_available = true;
    }

    if (!game->voice_capture_available) {
        return;
    }

    const EnginePreferences *prefs = preferences_get();
    PreferencesVoiceActivationMode voice_mode =
        prefs ? prefs->voice_activation_mode : PREFERENCES_VOICE_PUSH_TO_TALK;
    float threshold_db = prefs ? prefs->voice_activation_threshold_db : -45.0f;
    if (threshold_db > 0.0f) {
        threshold_db = 0.0f;
    }
    if (threshold_db < -120.0f) {
        threshold_db = -120.0f;
    }
    float threshold_linear = powf(10.0f, threshold_db / 20.0f);

    bool push_to_talk_active = game->last_input.voice_talk_down;

    const size_t temp_capacity = NETWORK_VOICE_MAX_DATA / sizeof(int16_t);
    int16_t temp_buffer[NETWORK_VOICE_MAX_DATA / sizeof(int16_t)];

    size_t read_samples;
    bool block_network = game->paused && game->options_open;
    while ((read_samples = audio_microphone_read(temp_buffer, temp_capacity)) > 0U) {
        size_t offset = 0U;
        while (offset < read_samples) {
            size_t remaining = read_samples - offset;
            size_t needed = GAME_VOICE_CAPTURE_SAMPLES > game->voice_capture_sample_count
                                ? (GAME_VOICE_CAPTURE_SAMPLES - game->voice_capture_sample_count)
                                : GAME_VOICE_CAPTURE_SAMPLES;
            if (needed == 0U) {
                needed = GAME_VOICE_CAPTURE_SAMPLES;
                game->voice_capture_sample_count = 0U;
            }

            size_t to_copy = remaining < needed ? remaining : needed;
            memcpy(game->voice_capture_buffer + game->voice_capture_sample_count,
                   temp_buffer + offset,
                   to_copy * sizeof(int16_t));
            game->voice_capture_sample_count += to_copy;
            offset += to_copy;

            if (game->voice_capture_sample_count >= GAME_VOICE_CAPTURE_SAMPLES) {
                float sum = 0.0f;
                for (size_t i = 0; i < GAME_VOICE_CAPTURE_SAMPLES; ++i) {
                    float sample = (float)game->voice_capture_buffer[i] / 32768.0f;
                    sum += sample * sample;
                }
                float rms = sqrtf(sum / (float)GAME_VOICE_CAPTURE_SAMPLES);

                bool transmit = false;
                if (voice_mode == PREFERENCES_VOICE_PUSH_TO_TALK) {
                    transmit = push_to_talk_active;
                } else {
                    transmit = rms >= threshold_linear;
                }

                if (transmit) {
                    NetworkVoicePacket packet = {0};
                    packet.codec = NETWORK_VOICE_CODEC_PCM16;
                    packet.channels = audio_microphone_channels();
                    if (packet.channels == 0U || packet.channels > NETWORK_VOICE_MAX_CHANNELS) {
                        packet.channels = 1U;
                    }

                    packet.sample_rate = audio_microphone_sample_rate();
                    if (packet.sample_rate == 0U) {
                        packet.sample_rate = GAME_VOICE_DEFAULT_SAMPLE_RATE;
                    }

                    size_t total_samples = GAME_VOICE_CAPTURE_SAMPLES;
                    size_t frames = packet.channels > 0U ? (total_samples / packet.channels) : total_samples;
                    if (frames == 0U) {
                        frames = total_samples;
                    }
                    packet.frame_count = (uint16_t)frames;
                    packet.volume = 1.0f;

                    size_t byte_count = total_samples * sizeof(int16_t);
                    if (byte_count > sizeof(packet.data)) {
                        byte_count = sizeof(packet.data);
                    }

                    memcpy(packet.data, game->voice_capture_buffer, byte_count);
                    packet.data_size = byte_count;

                    if (!block_network) {
                        (void)network_client_send_voice_packet(game->network, &packet);
                    }
                }

                game->voice_capture_sample_count = 0U;
            }
        }
    }
}

static void game_synchronize_remote_players(GameState *game)
{
    if (!game || !game->network) {
        return;
    }

    size_t remote_count = 0;
    const NetworkRemotePlayer *remote_players = network_client_remote_players(game->network, &remote_count);
    if (!remote_players) {
        remote_count = 0;
    }

    uint8_t self_id = network_client_self_id(game->network);
    bool slot_used[GAME_MAX_REMOTE_PLAYERS] = { false };

    for (size_t i = 0; i < remote_count; ++i) {
        const NetworkRemotePlayer *remote = &remote_players[i];
        if (!remote->active || remote->id == self_id) {
            continue;
        }

        size_t slot = game_acquire_remote_slot(game, remote->id);
        if (slot == SIZE_MAX || slot >= game->remote_entity_count || slot >= GAME_MAX_REMOTE_PLAYERS) {
            continue;
        }

        GameEntity *entity = world_get_entity(&game->world, game->remote_entity_indices[slot]);
        if (!entity) {
            continue;
        }

        vec3 position = vec3_make(remote->position[0], remote->position[1], remote->position[2]);
        if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z)) {
            continue;
        }

        entity->position = position;
        entity->visible = true;
        if (remote->name[0] != '\0') {
            strncpy(game->remote_entity_names[slot], remote->name, NETWORK_MAX_PLAYER_NAME - 1);
            game->remote_entity_names[slot][NETWORK_MAX_PLAYER_NAME - 1] = '\0';
        } else {
            game->remote_entity_names[slot][0] = '\0';
        }
        slot_used[slot] = true;
    }

    for (size_t i = 0; i < game->remote_entity_count && i < GAME_MAX_REMOTE_PLAYERS; ++i) {
        if (!slot_used[i]) {
            game_release_remote_slot(game, i);
        }
    }
}

static void game_update_network(GameState *game, float dt)
{
    if (!game || !game->network) {
        return;
    }

    network_client_update(game->network, dt);
    const NetworkClientStats *stats = network_client_stats(game->network);
    if (!stats) {
        return;
    }

    if (!stats->connected) {
        game_clear_remote_entities(game);
        audio_voice_stop_all();
        audio_microphone_stop();
        game->voice_capture_sample_count = 0U;
        game->voice_capture_available = false;
        return;
    }

    NetworkClientPlayerState player_state;
    player_state.position[0] = game->player.position.x;
    player_state.position[1] = game->player.position.y;
    player_state.position[2] = game->player.position.z;
    player_state.yaw = game->camera.yaw;
    (void)network_client_send_player_state(game->network, &player_state);

    game_synchronize_remote_players(game);
    game_update_voice_chat(game, dt);
    game_process_voice_packets(game);
    game_process_weapon_events(game);
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

    world_init(&game->world);

    vec3 player_start = vec3_make(0.0f, game->config.player_height, 6.0f);
    player_init(&game->player, &game->config, player_start);

    size_t player_index = world_add_entity(&game->world,
                                           ENTITY_TYPE_PLAYER,
                                           player_start,
                                           vec3_make(0.5f, game->config.player_height, 0.5f),
                                           vec3_make(0.2f, 0.2f, 0.3f),
                                           false);
    game->player_entity_index = (player_index == SIZE_MAX) ? 0U : player_index;

    game_setup_world(game);

    const float aspect = 16.0f / 9.0f;
    game->camera = camera_create(player_start,
                                 0.0f,
                                 0.0f,
                                 (float)M_PI / 180.0f * CAMERA_DEFAULT_FOV_DEG,
                                 aspect,
                                 CAMERA_DEFAULT_NEAR,
                                 CAMERA_DEFAULT_FAR);
    camera_set_pitch_limits(&game->camera, -(float)M_PI_2 * 0.98f, (float)M_PI_2 * 0.98f);

    weapon_init(&game->weapon);
    game->highlighted_pickup_id = WEAPON_ID_NONE;
    game->highlighted_pickup_index = SIZE_MAX;
    game->highlighted_pickup_network_id = 0;
    game->pickup_in_range = false;
    game->pickup_distance = 0.0f;

    game->hud.crosshair_base = 12.0f;
    game->hud.crosshair_spread = game->hud.crosshair_base;
    game->hud.damage_flash = 0.0f;
    game->hud.network_indicator_timer = 0.0f;

    settings_menu_init(&game->settings_menu);
    game_inventory_init(game);

    game->time_seconds = 0.0;
    game->session_time = 0.0;
    game->paused = false;
    game->options_open = false;
    game->pause_selection = 0;
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

    audio_voice_stop_all();
    audio_microphone_stop();

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

    player_reset_command(&game->command);

    if (input->escape_pressed) {
        if (game->paused) {
            if (game->options_open) {
                if (game->settings_menu.waiting_for_rebind) {
                    settings_menu_cancel_rebind(&game->settings_menu);
                } else {
                    game->options_open = false;
                }
            } else if (game->server_browser.open) {
                game->server_browser.open = false;
            } else {
                game->paused = false;
                game->pause_selection = 0;
                settings_menu_cancel_rebind(&game->settings_menu);
            }
        } else {
            game->paused = true;
            game->pause_selection = 0;
            game->options_open = false;
            game->server_browser.open = false;
            settings_menu_cancel_rebind(&game->settings_menu);
        }
    }

    if (game->paused) {
        bool move_up = axis_pressed_positive(input->move_forward, previous_input.move_forward) || input->mouse_wheel > 0.25f;
        bool move_down = axis_pressed_negative(input->move_forward, previous_input.move_forward) || input->mouse_wheel < -0.25f;

        if (game->options_open) {
            return;
        }

        if (game->server_browser.open) {
            if (server_browser_has_entries(&game->server_browser)) {
                if (move_up) {
                    server_browser_move_selection(&game->server_browser, -1);
                }
                if (move_down) {
                    server_browser_move_selection(&game->server_browser, 1);
                }
            }

            if (input->reload_pressed) {
                game_server_browser_refresh(game);
            }

            if (input->fire_pressed || input->interact_pressed) {
                if (server_browser_has_entries(&game->server_browser)) {
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
                    settings_menu_cancel_rebind(&game->settings_menu);
                    break;
                case 1:
                    game->options_open = true;
                    settings_menu_cancel_rebind(&game->settings_menu);
                    game->settings_menu.active_category = SETTINGS_MENU_CATEGORY_CONTROLS;
                    break;
                case 2:
                    (void)game_server_browser_open(game);
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

    player_build_command(&game->command, input, &game->camera, &game->config);
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
        player_update_camera(&game->player, &game->camera, &game->config, &game->command, dt);

        const vec3 pos_paused = game->camera.position;
        const float paused_r = 0.05f + 0.45f * (0.5f + 0.5f * sinf(pos_paused.x * 0.35f));
        const float paused_g = 0.05f + 0.40f * (0.5f + 0.5f * sinf(pos_paused.y * 0.25f));
        const float paused_b = 0.10f + 0.45f * (0.5f + 0.5f * sinf(pos_paused.z * 0.35f));
        renderer_set_clear_color(game->renderer, paused_r, paused_g, paused_b, 1.0f);
        return;
    }

    if (game->command.drop_requested) {
        game_drop_current_weapon(game);
    }

    game->time_seconds += (double)dt;
    game->session_time += (double)dt;

    physics_world_step(game->physics, dt);
    player_update_physics(&game->player,
                          &game->command,
                          &game->config,
                          &game->world,
                          dt,
                          game->player_entity_index);

    game_update_weapon_pickups(game);

    WeaponUpdateInput weapon_input = {
        .dt = dt,
        .fire_down = game->command.fire_down,
        .fire_pressed = game->command.fire_pressed,
        .fire_released = game->command.fire_released,
        .reload_requested = game->command.reload_requested,
    };
    WeaponUpdateResult weapon_result = weapon_update(&game->weapon, &weapon_input);
    if (weapon_result.fired) {
        game->hud.damage_flash = 0.3f;
    }

    if (game->hud.damage_flash > 0.0f) {
        game->hud.damage_flash -= dt;
        if (game->hud.damage_flash < 0.0f) {
            game->hud.damage_flash = 0.0f;
        }
    }

    game->hud.crosshair_spread = game->hud.crosshair_base + game->weapon.recoil * 0.7f + game->command.move_magnitude * 6.0f;

    player_update_camera(&game->player, &game->camera, &game->config, &game->command, dt);

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
        SettingsMenuContext context;
        memset(&context, 0, sizeof(context));
        context.in_game = true;
        context.view_bobbing = &game->config.enable_view_bobbing;
        context.double_jump = &game->config.enable_double_jump;
        EnginePreferences *prefs = preferences_data();
        context.master_volume = prefs ? &prefs->volume_master : NULL;
        context.music_volume = prefs ? &prefs->volume_music : NULL;
        context.effects_volume = prefs ? &prefs->volume_effects : NULL;
        context.voice_volume = prefs ? &prefs->volume_voice : NULL;
        context.microphone_volume = prefs ? &prefs->volume_microphone : NULL;
        context.audio_output_device = prefs ? &prefs->audio_output_device : NULL;
        context.audio_input_device = prefs ? &prefs->audio_input_device : NULL;
        context.voice_activation_mode = prefs ? &prefs->voice_activation_mode : NULL;
        context.voice_activation_threshold_db = prefs ? &prefs->voice_activation_threshold_db : NULL;

        SettingsMenuResult menu_result = settings_menu_render(&game->settings_menu,
                                                              &context,
                                                              renderer,
                                                              &game->last_input,
                                                              game->time_seconds);

        if (menu_result.view_bobbing_changed) {
            game_notify(game, game->config.enable_view_bobbing ? "View bobbing enabled" : "View bobbing disabled");
        }

        if (menu_result.double_jump_changed) {
            game_set_double_jump_enabled(game, game->config.enable_double_jump);
            game_notify(game, game->config.enable_double_jump ? "Double jump enabled" : "Double jump disabled");
        }

        if (menu_result.binding_changed) {
            const char *action_name = input_action_display_name(menu_result.binding_changed_action);
            const char *key_name = input_key_display_name(menu_result.binding_new_key);
            if (action_name && key_name) {
                char buffer[96];
                snprintf(buffer, sizeof(buffer), "%s -> %s", action_name, key_name);
                game_notify(game, buffer);
            }
        }

        if (menu_result.binding_reset) {
            const char *action_name = input_action_display_name(menu_result.binding_reset_action);
            if (action_name) {
                char buffer[96];
                snprintf(buffer, sizeof(buffer), "%s reset to default", action_name);
                game_notify(game, buffer);
            }
        }

        bool audio_prefs_changed = false;
        if (menu_result.master_volume_changed && prefs) {
            audio_set_master_volume(prefs->volume_master);
            audio_music_set_volume(prefs->volume_master * prefs->volume_music);
            audio_prefs_changed = true;
        }
        if (menu_result.music_volume_changed && prefs) {
            audio_music_set_volume(prefs->volume_master * prefs->volume_music);
            audio_prefs_changed = true;
        }
        if (menu_result.effects_volume_changed && prefs) {
            audio_set_effects_volume(prefs->volume_effects);
            audio_prefs_changed = true;
        }
        if (menu_result.voice_volume_changed && prefs) {
            audio_set_voice_volume(prefs->volume_voice);
            audio_prefs_changed = true;
        }
        if (menu_result.microphone_volume_changed && prefs) {
            audio_set_microphone_volume(prefs->volume_microphone);
            audio_prefs_changed = true;
        }
        if (menu_result.output_device_changed && prefs) {
            audio_select_output_device(prefs->audio_output_device);
            audio_prefs_changed = true;
        }
        if (menu_result.input_device_changed && prefs) {
            audio_select_input_device(prefs->audio_input_device);
            audio_prefs_changed = true;
        }
        if (menu_result.voice_mode_changed && prefs) {
            audio_prefs_changed = true;
        }
        if (menu_result.voice_threshold_changed && prefs) {
            audio_prefs_changed = true;
        }
        if (audio_prefs_changed) {
            preferences_save();
        }

        if (menu_result.reset_all_bindings) {
            game_notify(game, "All controls reset to defaults");
        }

        if (menu_result.back_requested) {
            game->options_open = false;
            settings_menu_cancel_rebind(&game->settings_menu);
        }
    } else if (game->server_browser.open) {
        const float panel_width = 720.0f;
        const float panel_height = 480.0f;
        const float panel_x = (width - panel_width) * 0.5f;
        const float panel_y = (height - panel_height) * 0.5f;
        renderer_draw_ui_rect(renderer, panel_x, panel_y, panel_width, panel_height, 0.04f, 0.04f, 0.06f, 0.9f);

        renderer_draw_ui_text(renderer, panel_x + 28.0f, panel_y + 34.0f, "Server Browser", 0.95f, 0.95f, 0.95f, 1.0f);

        double elapsed = game->time_seconds - game->server_browser.last_refresh_time;
        if (elapsed < 0.0) {
            elapsed = 0.0;
        }

        char status_line[160];
        if (game->server_browser.status[0] != '\0') {
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
                              panel_x + 28.0f,
                              panel_y + 72.0f,
                              status_line,
                              status_r,
                              status_g,
                              status_b,
                              0.95f);

        const float header_y = panel_y + 116.0f;
        const float list_x = panel_x + 32.0f;
        const float row_height = 34.0f;

        renderer_draw_ui_text(renderer, list_x, header_y, "Server", 0.85f, 0.85f, 0.95f, 0.9f);
        renderer_draw_ui_text(renderer, list_x + 320.0f, header_y, "Address", 0.85f, 0.85f, 0.95f, 0.9f);
        renderer_draw_ui_text(renderer, list_x + 520.0f, header_y, "Players", 0.85f, 0.85f, 0.95f, 0.9f);
        renderer_draw_ui_text(renderer, list_x + 620.0f, header_y, "Mode", 0.85f, 0.85f, 0.95f, 0.9f);

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
        const GameEntity *entity = world_get_entity_const(&game->world, i);
        if (!entity || !entity->visible || entity->type == ENTITY_TYPE_PLAYER) {
            continue;
        }

        vec3 half_extents = vec3_scale(entity->scale, 0.5f);
        renderer_draw_box(game->renderer, entity->position, half_extents, entity->color);
    }
}

    static bool game_world_to_screen(const GameState *game,
                                     vec3 position,
                                     float *out_x,
                                     float *out_y,
                                     float *out_depth)
    {
        if (!game || !game->renderer || !out_x || !out_y) {
            return false;
        }

        mat4 vp = camera_view_projection_matrix(&game->camera);
        vec4 world = {position.x, position.y, position.z, 1.0f};
        vec4 clip;
        clip.x = vp.m[0] * world.x + vp.m[4] * world.y + vp.m[8] * world.z + vp.m[12] * world.w;
        clip.y = vp.m[1] * world.x + vp.m[5] * world.y + vp.m[9] * world.z + vp.m[13] * world.w;
        clip.z = vp.m[2] * world.x + vp.m[6] * world.y + vp.m[10] * world.z + vp.m[14] * world.w;
        clip.w = vp.m[3] * world.x + vp.m[7] * world.y + vp.m[11] * world.z + vp.m[15] * world.w;

        if (fabsf(clip.w) < 0.00001f) {
            return false;
        }

        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
        float ndc_z = clip.z / clip.w;

        if (!isfinite(ndc_x) || !isfinite(ndc_y) || !isfinite(ndc_z)) {
            return false;
        }

        if (ndc_z < -1.0f || ndc_z > 1.0f || clip.w <= 0.0f) {
            return false;
        }

        float width = (float)renderer_viewport_width(game->renderer);
        float height = (float)renderer_viewport_height(game->renderer);
        if (width <= 0.0f || height <= 0.0f) {
            return false;
        }

        *out_x = (ndc_x * 0.5f + 0.5f) * width;
        *out_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * height;

        if (out_depth) {
            *out_depth = ndc_z;
        }

        return true;
    }

    static void game_draw_remote_nameplates(GameState *game, Renderer *renderer, float hud_alpha)
    {
        if (!game || !renderer || hud_alpha <= 0.0f) {
            return;
        }

        const uint32_t vp_width = renderer_viewport_width(renderer);
        const uint32_t vp_height = renderer_viewport_height(renderer);
        if (vp_width == 0U || vp_height == 0U) {
            return;
        }

        for (size_t i = 0; i < game->remote_entity_count && i < GAME_MAX_REMOTE_PLAYERS; ++i) {
            if (game->remote_entity_ids[i] == 0xFF) {
                continue;
            }

            size_t entity_index = game->remote_entity_indices[i];
            GameEntity *entity = world_get_entity(&game->world, entity_index);
            if (!entity || !entity->visible) {
                continue;
            }

            vec3 head_pos = entity->position;
            head_pos.y += entity->scale.y * 0.55f;

            float screen_x = 0.0f;
            float screen_y = 0.0f;
            float depth = 0.0f;
            if (!game_world_to_screen(game, head_pos, &screen_x, &screen_y, &depth)) {
                continue;
            }

            const char *name = game->remote_entity_names[i];
            char fallback[NETWORK_MAX_PLAYER_NAME];
            if (!name || name[0] == '\0') {
                snprintf(fallback, sizeof(fallback), "Operative %u", (unsigned)game->remote_entity_ids[i]);
                name = fallback;
            }

            size_t name_len = strlen(name);
            if (name_len == 0) {
                continue;
            }

            float text_width = (float)name_len * 9.0f;
            float box_width = text_width + 18.0f;
            float box_height = 24.0f;
            float box_x = screen_x - box_width * 0.5f;
            float box_y = screen_y - 52.0f;

            if (box_x + box_width < 0.0f || box_x > (float)vp_width || box_y + box_height < 0.0f || box_y > (float)vp_height) {
                continue;
            }

            renderer_draw_ui_rect(renderer,
                                  box_x,
                                  box_y,
                                  box_width,
                                  box_height,
                                  0.05f,
                                  0.05f,
                                  0.08f,
                                  0.65f * hud_alpha);
            renderer_draw_ui_text(renderer,
                                  box_x + 9.0f,
                                  box_y + 6.0f,
                                  name,
                                  0.92f,
                                  0.95f,
                                  0.98f,
                                  0.95f * hud_alpha);
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

    game_draw_remote_nameplates(game, renderer, hud_alpha);

    const PlayerState *player = &game->player;
    const WeaponState *weapon = &game->weapon;

    const float margin = 28.0f;

    char buffer[192];

    /* Top-left objective and status panel */
    renderer_draw_ui_rect(renderer, margin - 20.0f, margin - 20.0f, 320.0f, 110.0f, 0.05f, 0.05f, 0.07f, 0.65f * hud_alpha);
    int minutes = (int)(game->session_time / 60.0);
    int seconds = (int)fmod(game->session_time, 60.0);
    snprintf(buffer,
             sizeof(buffer),
             "Objective: %s\n"
             "Elapsed: %02d:%02d\n"
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

    const char *current_weapon_name = weapon_state_display_name(weapon);
    const char *fire_mode_label = "Semi";
    switch (weapon_state_fire_mode(weapon)) {
    case WEAPON_FIRE_MODE_AUTO:
        fire_mode_label = "Auto";
        break;
    case WEAPON_FIRE_MODE_BURST:
        fire_mode_label = "Burst";
        break;
    default:
        break;
    }

    snprintf(buffer, sizeof(buffer), "%s [%s]", current_weapon_name, fire_mode_label);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 22.0f, buffer, 0.95f, 0.92f, 0.70f, 0.96f * hud_alpha);

    int clip_ammo = weapon->ammo_in_clip;
    int clip_size = weapon->clip_size;
    int reserve_ammo = weapon->ammo_reserve;
    if (weapon_state_is_unarmed(weapon)) {
        clip_ammo = 0;
        clip_size = 0;
        reserve_ammo = 0;
    }

    snprintf(buffer, sizeof(buffer), "Clip: %02d / %02d", clip_ammo, clip_size);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 48.0f, buffer, 0.95f, 0.88f, 0.50f, 0.95f * hud_alpha);
    snprintf(buffer, sizeof(buffer), "Reserve: %03d", reserve_ammo);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 72.0f, buffer, 0.85f, 0.85f, 0.85f, 0.92f * hud_alpha);

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
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 90.0f, buffer, 0.82f, 0.82f, 0.86f, 0.9f * hud_alpha);

    PlatformKey drop_key = input_binding_get(INPUT_ACTION_DROP_WEAPON);
    const char *drop_key_name = input_key_display_name(drop_key);
    if (!drop_key_name || drop_key_name[0] == '\0') {
        drop_key_name = "C";
    }

    snprintf(buffer,
             sizeof(buffer),
             "Recoil: %.1f  Rate: %.1f/s  Drop: [%s]",
             weapon->recoil,
             weapon->fire_rate,
             drop_key_name);
    renderer_draw_ui_text(renderer, weapon_panel_x + 16.0f, weapon_panel_y + 114.0f, buffer, 0.78f, 0.86f, 0.98f, 0.88f * hud_alpha);

    if (game->pickup_in_range && !game->paused) {
        const WeaponDefinition *pickup_def = weapon_definition(game->highlighted_pickup_id);
        if (pickup_def) {
            PlatformKey interact_key = input_binding_get(INPUT_ACTION_INTERACT);
            const char *key_name = input_key_display_name(interact_key);
            if (!key_name || key_name[0] == '\0') {
                key_name = "F";
            }
            snprintf(buffer, sizeof(buffer), "Press %s to pick up %s", key_name, pickup_def->name);
            const float prompt_width = 360.0f;
            const float prompt_height = 36.0f;
            const float prompt_x = (width - prompt_width) * 0.5f;
            const float prompt_y = height * 0.55f;
            renderer_draw_ui_rect(renderer, prompt_x, prompt_y, prompt_width, prompt_height, 0.04f, 0.04f, 0.08f, 0.65f * hud_alpha);
            renderer_draw_ui_text(renderer, prompt_x + 18.0f, prompt_y + 10.0f, buffer, 0.95f, 0.95f, 0.95f, 0.94f * hud_alpha);
        }
    }

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

bool game_connect_to_master_entry(GameState *game, const MasterServerEntry *entry)
{
    if (!game || !entry) {
        return false;
    }

    if (!game_connect_to_entry(game, entry)) {
        game_notify(game, "Failed to initialize network client.");
        return false;
    }
    return true;
}

bool game_request_open_server_browser(GameState *game)
{
    if (!game) {
        return false;
    }

    game->paused = true;
    game->options_open = false;
    return game_server_browser_open(game);
}













































