#include "engine/network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enet.h"

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#endif

#define NETWORK_MESSAGE_HELLO 0x01
#define NETWORK_MESSAGE_WELCOME 0x02
#define NETWORK_MESSAGE_PLAYER_COUNT 0x03
#define NETWORK_MESSAGE_CLIENT_STATE 0x04
#define NETWORK_MESSAGE_SERVER_SNAPSHOT 0x05
#define NETWORK_MESSAGE_WEAPON_EVENT 0x06
#define NETWORK_MESSAGE_CLIENT_WEAPON_EVENT 0x07
#define NETWORK_MESSAGE_CLIENT_VOICE_DATA 0x08
#define NETWORK_MESSAGE_VOICE_DATA 0x09

#define NETWORK_WEAPON_EVENT_DATA_SIZE (1 + sizeof(uint16_t) + sizeof(int16_t) + sizeof(int16_t) + sizeof(uint32_t) + (sizeof(float) * 3))

#define NETWORK_CLIENT_WEAPON_EVENT_CAPACITY 64
#define NETWORK_CLIENT_VOICE_PACKET_CAPACITY 64

typedef struct NetworkClient {
    NetworkClientConfig config;
    ENetHost *host;
    ENetPeer *peer;
    NetworkClientStats stats;
    NetworkRemotePlayer remote_players[NETWORK_MAX_REMOTE_PLAYERS];
    size_t remote_player_count;
    uint8_t self_id;
    double time_since_last_packet;
    double handshake_timer;
    double handshake_start;
    int connecting;
    NetworkWeaponEvent weapon_events[NETWORK_CLIENT_WEAPON_EVENT_CAPACITY];
    size_t weapon_event_head;
    size_t weapon_event_count;
    NetworkVoicePacket voice_packets[NETWORK_CLIENT_VOICE_PACKET_CAPACITY];
    size_t voice_packet_head;
    size_t voice_packet_count;
} NetworkClient;

static int g_enet_client_refcount = 0;

static double network_get_time_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER freq;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&freq);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    return 0.0;
#endif
}

static uint32_t network_resolve_ipv4(const char *host)
{
    if (!host || host[0] == '\0') {
        return htonl(INADDR_LOOPBACK);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    int result = inet_pton(AF_INET, host, &addr.sin_addr);
    if (result == 1) {
        return ntohl(addr.sin_addr.s_addr);
    }

    struct addrinfo hints;
    struct addrinfo *info = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &info) == 0) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)info->ai_addr;
        uint32_t resolved = ntohl(ipv4->sin_addr.s_addr);
        freeaddrinfo(info);
        return resolved;
    }

    return htonl(INADDR_LOOPBACK);
}

static void network_client_clear_remote_players(NetworkClient *client)
{
    if (!client) {
        return;
    }

    memset(client->remote_players, 0, sizeof(client->remote_players));
    client->remote_player_count = 0;
    client->stats.remote_player_count = 0;
}

static void network_client_clear_weapon_events(NetworkClient *client)
{
    if (!client) {
        return;
    }

    client->weapon_event_head = 0;
    client->weapon_event_count = 0;
    memset(client->weapon_events, 0, sizeof(client->weapon_events));
}

static void network_client_clear_voice_packets(NetworkClient *client)
{
    if (!client) {
        return;
    }

    client->voice_packet_head = 0;
    client->voice_packet_count = 0;
    memset(client->voice_packets, 0, sizeof(client->voice_packets));
}

static void network_client_enqueue_voice_packet(NetworkClient *client, const NetworkVoicePacket *packet)
{
    if (!client || !packet) {
        return;
    }

    if (client->voice_packet_count >= NETWORK_CLIENT_VOICE_PACKET_CAPACITY) {
        client->voice_packet_head = (client->voice_packet_head + 1) % NETWORK_CLIENT_VOICE_PACKET_CAPACITY;
        client->voice_packet_count = NETWORK_CLIENT_VOICE_PACKET_CAPACITY - 1;
    }

    size_t index = (client->voice_packet_head + client->voice_packet_count) % NETWORK_CLIENT_VOICE_PACKET_CAPACITY;
    client->voice_packets[index] = *packet;
    ++client->voice_packet_count;
}

static void network_client_enqueue_weapon_event(NetworkClient *client, const NetworkWeaponEvent *event)
{
    if (!client || !event) {
        return;
    }

    if (client->weapon_event_count >= NETWORK_CLIENT_WEAPON_EVENT_CAPACITY) {
        client->weapon_event_head = (client->weapon_event_head + 1) % NETWORK_CLIENT_WEAPON_EVENT_CAPACITY;
        client->weapon_event_count = NETWORK_CLIENT_WEAPON_EVENT_CAPACITY - 1;
    }

    size_t index = (client->weapon_event_head + client->weapon_event_count) % NETWORK_CLIENT_WEAPON_EVENT_CAPACITY;
    client->weapon_events[index] = *event;
    ++client->weapon_event_count;
}

static void network_client_handle_snapshot(NetworkClient *client, const enet_uint8 *data, size_t size)
{
    if (!client || !data || size < 2) {
        return;
    }

    const size_t header_size = 2;
    const size_t stride = 1 + (sizeof(float) * 4) + NETWORK_MAX_PLAYER_NAME;
    uint8_t reported_count = data[1];
    size_t offset = header_size;

    network_client_clear_remote_players(client);

    size_t stored = 0;
    for (uint8_t i = 0; i < reported_count; ++i) {
        if (offset + stride > size) {
            break;
        }

        const enet_uint8 *entry = data + offset;
        offset += stride;

        if (stored >= NETWORK_MAX_REMOTE_PLAYERS) {
            continue;
        }

        NetworkRemotePlayer *dst = &client->remote_players[stored++];
        dst->id = entry[0];
        memcpy(dst->position, entry + 1, sizeof(float) * 3);
        memcpy(&dst->yaw, entry + 1 + sizeof(float) * 3, sizeof(float));
        memcpy(dst->name, entry + 1 + sizeof(float) * 4, NETWORK_MAX_PLAYER_NAME);
        dst->name[NETWORK_MAX_PLAYER_NAME - 1] = '\0';
        dst->active = true;
    }

    client->remote_player_count = stored;

    uint32_t remote_count = 0;
    for (size_t i = 0; i < stored; ++i) {
        const NetworkRemotePlayer *rp = &client->remote_players[i];
        if (rp->active && rp->id != client->self_id) {
            ++remote_count;
        }
    }
    client->stats.remote_player_count = remote_count;
}

NetworkClient *network_client_create(const NetworkClientConfig *config)
{
    if (!config) {
        return NULL;
    }

    if (g_enet_client_refcount == 0) {
        if (enet_initialize() != 0) {
            return NULL;
        }
    }
    ++g_enet_client_refcount;

    NetworkClient *client = (NetworkClient *)calloc(1, sizeof(NetworkClient));
    if (!client) {
        --g_enet_client_refcount;
        if (g_enet_client_refcount == 0) {
            enet_deinitialize();
        }
        return NULL;
    }

    client->config = *config;
    client->stats.connected = false;
    client->stats.time_since_last_packet = 0.0f;
    client->stats.simulated_ping_ms = 0.0f;
    client->stats.remote_player_count = 0;
    client->time_since_last_packet = 0.0;
    client->handshake_timer = 0.0;
    client->handshake_start = 0.0;
    client->connecting = 0;
    client->self_id = 0xFF;
    network_client_clear_remote_players(client);
    network_client_clear_weapon_events(client);
    network_client_clear_voice_packets(client);

    client->host = enet_host_create(NULL, 1, 1, 0, 0);
    if (!client->host) {
        free(client);
        --g_enet_client_refcount;
        if (g_enet_client_refcount == 0) {
            enet_deinitialize();
        }
        return NULL;
    }

    return client;
}

void network_client_destroy(NetworkClient *client)
{
    if (!client) {
        return;
    }

    network_client_disconnect(client);

    if (client->host) {
        enet_host_destroy(client->host);
        client->host = NULL;
    }

    free(client);

    --g_enet_client_refcount;
    if (g_enet_client_refcount == 0) {
        enet_deinitialize();
    }
}

void network_client_connect(NetworkClient *client)
{
    if (!client || !client->host) {
        return;
    }

    ENetAddress address;
    address.host = network_resolve_ipv4(client->config.host);
    address.port = client->config.port;

    client->peer = enet_host_connect(client->host, &address, 1, 0);
    if (!client->peer) {
        printf("[network_client] failed to create ENet peer\n");
        return;
    }

    client->connecting = 1;
    client->stats.connected = false;
    client->stats.time_since_last_packet = 0.0f;
    client->stats.simulated_ping_ms = 0.0f;
    client->stats.remote_player_count = 0;
    client->handshake_start = network_get_time_seconds();
   client->self_id = 0xFF;
   network_client_clear_remote_players(client);
    network_client_clear_weapon_events(client);
    network_client_clear_voice_packets(client);
}

void network_client_disconnect(NetworkClient *client)
{
    if (!client) {
        return;
    }

    if (client->peer) {
        enet_peer_disconnect(client->peer, 0);
        enet_peer_reset(client->peer);
    }

    client->peer = NULL;
    client->connecting = 0;
    client->stats.connected = false;
    client->self_id = 0xFF;
    network_client_clear_remote_players(client);
    network_client_clear_weapon_events(client);
    network_client_clear_voice_packets(client);
}

static void network_client_handle_packet(NetworkClient *client, const ENetEvent *event)
{
    if (!client || !event || !event->packet) {
        return;
    }

    const enet_uint8 *data = event->packet->data;
    size_t size = event->packet->dataLength;
    if (size == 0) {
        enet_packet_destroy(event->packet);
        return;
    }

    enet_uint8 message_type = data[0];
    switch (message_type) {
    case NETWORK_MESSAGE_WELCOME:
        client->stats.connected = true;
        client->connecting = 0;
        if (size >= 4) {
            client->stats.remote_player_count = data[1];
            client->stats.simulated_ping_ms = (float)((network_get_time_seconds() - client->handshake_start) * 1000.0);
            client->self_id = data[3];
        } else if (size >= 3) {
            client->stats.remote_player_count = data[1];
            client->stats.simulated_ping_ms = (float)((network_get_time_seconds() - client->handshake_start) * 1000.0);
        }
        break;
    case NETWORK_MESSAGE_PLAYER_COUNT:
        if (size >= 2) {
            client->stats.remote_player_count = data[1];
        }
        break;
    case NETWORK_MESSAGE_SERVER_SNAPSHOT:
        network_client_handle_snapshot(client, data, size);
        break;
    case NETWORK_MESSAGE_WEAPON_EVENT:
        if (size >= 2 + NETWORK_WEAPON_EVENT_DATA_SIZE) {
            NetworkWeaponEvent weapon_event = {0};
            weapon_event.actor_id = data[1];
            const enet_uint8 *payload = data + 2;
            size_t offset = 0;

            weapon_event.type = (NetworkWeaponEventType)payload[offset];
            offset += 1;

            uint16_t weapon_raw = 0;
            memcpy(&weapon_raw, payload + offset, sizeof(uint16_t));
            weapon_event.weapon_id = weapon_raw;
            offset += sizeof(uint16_t);

            int16_t clip_raw = 0;
            memcpy(&clip_raw, payload + offset, sizeof(int16_t));
            weapon_event.ammo_in_clip = clip_raw;
            offset += sizeof(int16_t);

            int16_t reserve_raw = 0;
            memcpy(&reserve_raw, payload + offset, sizeof(int16_t));
            weapon_event.ammo_reserve = reserve_raw;
            offset += sizeof(int16_t);

            memcpy(&weapon_event.pickup_id, payload + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + sizeof(float) * 3 <= NETWORK_WEAPON_EVENT_DATA_SIZE) {
                memcpy(weapon_event.position, payload + offset, sizeof(float) * 3);
            }

            network_client_enqueue_weapon_event(client, &weapon_event);
        }
        break;
    case NETWORK_MESSAGE_VOICE_DATA:
        if (size > 9U) {
            NetworkVoicePacket packet = {0};
            packet.speaker_id = data[1];
            packet.codec = (NetworkVoiceCodec)data[2];
            packet.channels = data[3];
            packet.sample_rate = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
            packet.frame_count = (uint16_t)(data[6] | ((uint16_t)data[7] << 8));
            uint8_t volume_byte = data[8];
            packet.volume = (float)volume_byte / 255.0f;

            if (packet.channels == 0U || packet.channels > NETWORK_VOICE_MAX_CHANNELS) {
                break;
            }

            size_t payload_size = size - 9U;
            size_t expected_size = (size_t)packet.frame_count * packet.channels * sizeof(int16_t);
            if (packet.codec != NETWORK_VOICE_CODEC_PCM16 || payload_size != expected_size ||
                payload_size > NETWORK_VOICE_MAX_DATA) {
                break;
            }

            memcpy(packet.data, data + 9, payload_size);
            packet.data_size = payload_size;
            network_client_enqueue_voice_packet(client, &packet);
        }
        break;
    default:
        break;
    }

    client->stats.time_since_last_packet = 0.0f;

    enet_packet_destroy(event->packet);
}

void network_client_update(NetworkClient *client, float dt)
{
    if (!client || !client->host) {
        return;
    }

    client->stats.time_since_last_packet += dt;

    ENetEvent event;
    while (enet_host_service(client->host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            enet_uint8 hello = NETWORK_MESSAGE_HELLO;
            ENetPacket *packet = enet_packet_create(&hello, 1, ENET_PACKET_FLAG_RELIABLE);
            if (packet) {
                enet_peer_send(event.peer, 0, packet);
            }
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            network_client_handle_packet(client, &event);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            client->stats.connected = false;
            client->connecting = 0;
            client->peer = NULL;
            client->self_id = 0xFF;
            network_client_clear_remote_players(client);
            break;
        default:
            break;
        }
    }
}

bool network_client_is_connected(const NetworkClient *client)
{
    return client ? client->stats.connected : false;
}

const NetworkClientStats *network_client_stats(const NetworkClient *client)
{
    if (!client) {
        return NULL;
    }
    return &client->stats;
}

uint8_t network_client_self_id(const NetworkClient *client)
{
    return client ? client->self_id : 0xFF;
}

const NetworkRemotePlayer *network_client_remote_players(const NetworkClient *client, size_t *out_count)
{
    if (!client) {
        if (out_count) {
            *out_count = 0;
        }
        return NULL;
    }

    if (out_count) {
        *out_count = client->remote_player_count;
    }
    return client->remote_players;
}

bool network_client_send_player_state(NetworkClient *client, const NetworkClientPlayerState *state)
{
    if (!client || !state || !client->peer) {
        return false;
    }
    if (!client->stats.connected || client->self_id == 0xFF) {
        return false;
    }

    enet_uint8 payload[1 + sizeof(float) * 4];
    payload[0] = NETWORK_MESSAGE_CLIENT_STATE;
    memcpy(payload + 1, state->position, sizeof(float) * 3);
    memcpy(payload + 1 + sizeof(float) * 3, &state->yaw, sizeof(float));

    ENetPacket *packet = enet_packet_create(payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    if (!packet) {
        return false;
    }

    return enet_peer_send(client->peer, 0, packet) == 0;
}

bool network_client_send_weapon_event(NetworkClient *client, const NetworkWeaponEvent *event)
{
    if (!client || !event || !client->peer) {
        return false;
    }
    if (!client->stats.connected || client->self_id == 0xFF) {
        return false;
    }

    enet_uint8 payload[1 + NETWORK_WEAPON_EVENT_DATA_SIZE];
    payload[0] = NETWORK_MESSAGE_CLIENT_WEAPON_EVENT;
    enet_uint8 *write = payload + 1;
    size_t offset = 0;

    write[offset] = (enet_uint8)event->type;
    offset += 1;

    uint16_t weapon_raw = event->weapon_id;
    memcpy(write + offset, &weapon_raw, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    int16_t clip_raw = event->ammo_in_clip;
    memcpy(write + offset, &clip_raw, sizeof(int16_t));
    offset += sizeof(int16_t);

    int16_t reserve_raw = event->ammo_reserve;
    memcpy(write + offset, &reserve_raw, sizeof(int16_t));
    offset += sizeof(int16_t);

    memcpy(write + offset, &event->pickup_id, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    memcpy(write + offset, event->position, sizeof(float) * 3);

    ENetPacket *packet = enet_packet_create(payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    if (!packet) {
        return false;
    }

    return enet_peer_send(client->peer, 0, packet) == 0;
}

bool network_client_send_voice_packet(NetworkClient *client, const NetworkVoicePacket *packet)
{
    if (!client || !packet || !client->peer) {
        return false;
    }
    if (!client->stats.connected || client->self_id == 0xFF) {
        return false;
    }
    if (packet->codec != NETWORK_VOICE_CODEC_PCM16) {
        return false;
    }
    if (packet->channels == 0U || packet->channels > NETWORK_VOICE_MAX_CHANNELS) {
        return false;
    }

    size_t expected_size = (size_t)packet->frame_count * packet->channels * sizeof(int16_t);
    if (packet->data_size == 0U || packet->data_size != expected_size || packet->data_size > NETWORK_VOICE_MAX_DATA) {
        return false;
    }

    float gain = packet->volume;
    if (gain < 0.0f) {
        gain = 0.0f;
    } else if (gain > 1.0f) {
        gain = 1.0f;
    }
    if (gain <= 0.0f) {
        gain = 1.0f;
    }

    enet_uint8 payload[1 + 8 + NETWORK_VOICE_MAX_DATA];
    payload[0] = NETWORK_MESSAGE_CLIENT_VOICE_DATA;
    payload[1] = (enet_uint8)packet->codec;
    payload[2] = packet->channels;

    uint16_t sample_rate = packet->sample_rate;
    payload[3] = (enet_uint8)(sample_rate & 0xFF);
    payload[4] = (enet_uint8)((sample_rate >> 8) & 0xFF);

    uint16_t frame_count = packet->frame_count;
    payload[5] = (enet_uint8)(frame_count & 0xFF);
    payload[6] = (enet_uint8)((frame_count >> 8) & 0xFF);

    payload[7] = (enet_uint8)(gain * 255.0f);

    memcpy(payload + 8, packet->data, packet->data_size);
    size_t packet_size = 1 + 7 + packet->data_size;

    ENetPacket *enet_packet =
        enet_packet_create(payload, packet_size, ENET_PACKET_FLAG_UNSEQUENCED | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
    if (!enet_packet) {
        return false;
    }

    return enet_peer_send(client->peer, 0, enet_packet) == 0;
}

size_t network_client_dequeue_weapon_events(NetworkClient *client,
                                            NetworkWeaponEvent *out_events,
                                            size_t max_events)
{
    if (!client || !out_events || max_events == 0) {
        return 0;
    }

    size_t popped = 0;
    while (popped < max_events && client->weapon_event_count > 0) {
        out_events[popped++] = client->weapon_events[client->weapon_event_head];
        client->weapon_event_head = (client->weapon_event_head + 1) % NETWORK_CLIENT_WEAPON_EVENT_CAPACITY;
        --client->weapon_event_count;
    }

    return popped;
}

size_t network_client_dequeue_voice_packets(NetworkClient *client,
                                            NetworkVoicePacket *out_packets,
                                            size_t max_packets)
{
    if (!client || !out_packets || max_packets == 0U) {
        return 0;
    }

    size_t popped = 0;
    while (popped < max_packets && client->voice_packet_count > 0U) {
        out_packets[popped++] = client->voice_packets[client->voice_packet_head];
        client->voice_packet_head = (client->voice_packet_head + 1) % NETWORK_CLIENT_VOICE_PACKET_CAPACITY;
        --client->voice_packet_count;
    }

    return popped;
}

