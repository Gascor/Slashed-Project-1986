#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "engine/network_master.h"

#ifndef ENET_PACKET_FLAG_UNSEQUENCED
#define ENET_PACKET_FLAG_UNSEQUENCED (1 << 1)
#endif

#ifndef ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT
#define ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT (1 << 3)
#endif

typedef struct NetworkClient NetworkClient;

#define NETWORK_MAX_REMOTE_PLAYERS 16
#define NETWORK_MAX_PLAYER_NAME    16

#define NETWORK_VOICE_MAX_DATA        2048
#define NETWORK_VOICE_MAX_CHANNELS    2

typedef struct NetworkClientConfig {
    const char *host;
    uint16_t port;
    bool simulate_latency;
} NetworkClientConfig;

typedef struct NetworkClientStats {
    bool connected;
    float time_since_last_packet;
    float simulated_ping_ms;
    uint32_t remote_player_count;
} NetworkClientStats;

typedef struct NetworkRemotePlayer {
    uint8_t id;
    bool active;
    char name[NETWORK_MAX_PLAYER_NAME];
    float position[3];
    float yaw;
} NetworkRemotePlayer;

typedef struct NetworkClientPlayerState {
    float position[3];
    float yaw;
} NetworkClientPlayerState;

typedef enum NetworkWeaponEventType {
    NETWORK_WEAPON_EVENT_DROP = 0,
    NETWORK_WEAPON_EVENT_PICKUP = 1,
} NetworkWeaponEventType;

typedef struct NetworkWeaponEvent {
    NetworkWeaponEventType type;
    uint8_t actor_id;
    uint32_t pickup_id;
    uint16_t weapon_id;
    int16_t ammo_in_clip;
    int16_t ammo_reserve;
    float position[3];
} NetworkWeaponEvent;

typedef enum NetworkVoiceCodec {
    NETWORK_VOICE_CODEC_PCM16 = 0,
} NetworkVoiceCodec;

typedef struct NetworkVoicePacket {
    uint8_t speaker_id;
    NetworkVoiceCodec codec;
    uint8_t channels;
    uint16_t sample_rate;
    uint16_t frame_count;
    float volume;
    uint8_t data[NETWORK_VOICE_MAX_DATA];
    size_t data_size;
} NetworkVoicePacket;

NetworkClient *network_client_create(const NetworkClientConfig *config);
void network_client_destroy(NetworkClient *client);

void network_client_connect(NetworkClient *client);
void network_client_disconnect(NetworkClient *client);
void network_client_update(NetworkClient *client, float dt);

bool network_client_is_connected(const NetworkClient *client);
const NetworkClientStats *network_client_stats(const NetworkClient *client);
uint8_t network_client_self_id(const NetworkClient *client);
const NetworkRemotePlayer *network_client_remote_players(const NetworkClient *client, size_t *out_count);
bool network_client_send_player_state(NetworkClient *client, const NetworkClientPlayerState *state);
bool network_client_send_weapon_event(NetworkClient *client, const NetworkWeaponEvent *event);
size_t network_client_dequeue_weapon_events(NetworkClient *client,
                                            NetworkWeaponEvent *out_events,
                                            size_t max_events);
bool network_client_send_voice_packet(NetworkClient *client, const NetworkVoicePacket *packet);
size_t network_client_dequeue_voice_packets(NetworkClient *client,
                                            NetworkVoicePacket *out_packets,
                                            size_t max_packets);

bool network_fetch_master_list(const MasterClientConfig *config,
                               MasterServerEntry *out_entries,
                               size_t max_entries,
                               size_t *out_count);
