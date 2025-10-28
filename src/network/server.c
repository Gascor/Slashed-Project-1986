#include "engine/network_server.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enet.h"

#include "engine/master_protocol.h"
#include "engine/network.h"

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
typedef SOCKET master_socket_t;
#else
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
typedef int master_socket_t;
#    define INVALID_SOCKET (-1)
#    define SOCKET_ERROR (-1)
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

#define NETWORK_SERVER_SNAPSHOT_INTERVAL 0.05f
#define MASTER_DEFAULT_HEARTBEAT 5.0f

#define NETWORK_VOICE_RANGE 22.0f

typedef struct NetworkServerMaster {
    int enabled;
    master_socket_t socket;
    struct sockaddr_in master_addr;
    MasterServerEntry entry;
    float heartbeat_timer;
    float heartbeat_interval;
    float retry_timer;
    int registered;
} NetworkServerMaster;

typedef struct NetworkServerClient {
    ENetPeer *peer;
    uint8_t id;
    char name[NETWORK_MAX_PLAYER_NAME];
    float position[3];
    float yaw;
    int connected;
    int has_state;
} NetworkServerClient;

typedef struct NetworkServer {
    NetworkServerConfig config;
    ENetHost *host;
    NetworkServerStats stats;
    NetworkServerMaster master;
    NetworkServerClient *clients;
    uint32_t client_capacity;
    uint8_t next_client_id;
    float snapshot_timer;
} NetworkServer;

static int g_enet_server_refcount = 0;

static void network_server_increment_ref(void)
{
    if (g_enet_server_refcount == 0) {
        if (enet_initialize() != 0) {
            fprintf(stderr, "[network] enet_initialize failed\n");
        }
    }
    ++g_enet_server_refcount;
}

static void network_server_decrement_ref(void)
{
    if (g_enet_server_refcount <= 0) {
        return;
    }
    --g_enet_server_refcount;
    if (g_enet_server_refcount == 0) {
        enet_deinitialize();
    }
}
static NetworkServerClient *network_server_find_client(NetworkServer *server, ENetPeer *peer)
{
    if (!server || !server->clients || !peer) {
        return NULL;
    }

    for (uint32_t i = 0; i < server->client_capacity; ++i) {
        NetworkServerClient *client = &server->clients[i];
        if (client->connected && client->peer == peer) {
            return client;
        }
    }
    return NULL;
}

static uint8_t network_server_generate_id(NetworkServer *server)
{
    if (!server) {
        return 0;
    }

    for (uint16_t attempt = 0; attempt < 256; ++attempt) {
        uint8_t candidate = server->next_client_id++;
        if (candidate == 0xFF) {
            continue;
        }

        int in_use = 0;
        if (server->clients) {
            for (uint32_t i = 0; i < server->client_capacity; ++i) {
                const NetworkServerClient *client = &server->clients[i];
                if (client->connected && client->id == candidate) {
                    in_use = 1;
                    break;
                }
            }
        }

        if (!in_use) {
            return candidate;
        }
    }

    return 0;
}

static NetworkServerClient *network_server_acquire_client(NetworkServer *server, ENetPeer *peer)
{
    if (!server || !server->clients || !peer) {
        return NULL;
    }

    for (uint32_t i = 0; i < server->client_capacity; ++i) {
        NetworkServerClient *client = &server->clients[i];
        if (!client->connected) {
            memset(client, 0, sizeof(*client));
            client->connected = 1;
            client->peer = peer;
            client->id = network_server_generate_id(server);
            snprintf(client->name, sizeof(client->name), "Player %02u", (unsigned)(client->id + 1U));
            client->position[0] = 0.0f;
            client->position[1] = 0.0f;
            client->position[2] = 0.0f;
            client->yaw = 0.0f;
            client->has_state = 0;
            return client;
        }
    }

    return NULL;
}

static void network_server_release_client(NetworkServer *server, ENetPeer *peer)
{
    if (!server || !server->clients || !peer) {
        return;
    }

    for (uint32_t i = 0; i < server->client_capacity; ++i) {
        NetworkServerClient *client = &server->clients[i];
        if (client->connected && client->peer == peer) {
            memset(client, 0, sizeof(*client));
            break;
        }
    }
}

static enet_uint8 network_server_remote_count(const NetworkServer *server)
{
    if (!server || server->stats.connected_clients == 0U) {
        return 0;
    }

    uint32_t connected = server->stats.connected_clients;
    if (connected <= 1U) {
        return 0;
    }

    connected -= 1U;
    if (connected > 255U) {
        connected = 255U;
    }
    return (enet_uint8)connected;
}

static ENetPacket *network_server_create_snapshot_packet(NetworkServer *server)
{
    if (!server || !server->clients || server->client_capacity == 0U) {
        return NULL;
    }

    size_t active = 0;
    for (uint32_t i = 0; i < server->client_capacity; ++i) {
        const NetworkServerClient *client = &server->clients[i];
        if (client->connected && client->has_state) {
            ++active;
        }
    }

    if (active == 0) {
        return NULL;
    }

    if (active > NETWORK_MAX_REMOTE_PLAYERS) {
        active = NETWORK_MAX_REMOTE_PLAYERS;
    }

    const size_t stride = 1 + (sizeof(float) * 4) + NETWORK_MAX_PLAYER_NAME;
    const size_t payload_size = 2 + stride * active;

    ENetPacket *packet = enet_packet_create(NULL, payload_size, ENET_PACKET_FLAG_RELIABLE);
    if (!packet || !packet->data) {
        return NULL;
    }

    memset(packet->data, 0, payload_size);
    enet_uint8 *write = packet->data;
    write[0] = NETWORK_MESSAGE_SERVER_SNAPSHOT;

    size_t offset = 2;
    size_t written = 0;
    for (uint32_t i = 0; i < server->client_capacity && written < active; ++i) {
        const NetworkServerClient *client = &server->clients[i];
        if (!client->connected || !client->has_state) {
            continue;
        }

        enet_uint8 *entry = write + offset;
        entry[0] = client->id;
        memcpy(entry + 1, client->position, sizeof(float) * 3);
        memcpy(entry + 1 + sizeof(float) * 3, &client->yaw, sizeof(float));

        size_t name_offset = 1 + sizeof(float) * 4;
        memset(entry + name_offset, 0, NETWORK_MAX_PLAYER_NAME);
        size_t name_len = strlen(client->name);
        if (name_len >= NETWORK_MAX_PLAYER_NAME) {
            name_len = NETWORK_MAX_PLAYER_NAME - 1;
        }
        memcpy(entry + name_offset, client->name, name_len);

        offset += stride;
        ++written;
    }

    write[1] = (enet_uint8)written;
    return packet;
}

static void network_server_broadcast_snapshot(NetworkServer *server)
{
    if (!server || !server->host) {
        return;
    }

    ENetPacket *packet = network_server_create_snapshot_packet(server);
    if (!packet) {
        return;
    }

    enet_host_broadcast(server->host, 0, packet);
}

static void network_server_send_snapshot_to(NetworkServer *server, ENetPeer *peer)
{
    if (!server || !peer) {
        return;
    }

    ENetPacket *packet = network_server_create_snapshot_packet(server);
    if (!packet) {
        return;
    }

    enet_peer_send(peer, 0, packet);
}

static void network_server_master_refresh_entry(NetworkServer *server)
{
    if (!server) {
        return;
    }

    NetworkServerMaster *master = &server->master;
    master->entry.max_players = (uint8_t)(server->stats.max_clients > 255 ? 255 : server->stats.max_clients);
    if (master->entry.max_players == 0) {
        master->entry.max_players = 1;
    }

    uint32_t players = server->stats.connected_clients;
    if (players > master->entry.max_players) {
        players = master->entry.max_players;
    }

    master->entry.players = (uint8_t)players;
}
static void network_server_close_socket(master_socket_t socket)
{
    if (socket == INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

static int network_server_resolve_ipv4(const char *host, uint16_t port, struct sockaddr_in *out)
{
    if (!out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);

    if (!host || host[0] == '\0') {
        out->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return 0;
    }

#if defined(_WIN32)
    if (InetPtonA(AF_INET, host, &out->sin_addr) == 1) {
        return 0;
    }
#else
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) {
        return 0;
    }
#endif

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *info = NULL;
    int result = getaddrinfo(host, NULL, &hints, &info);
    if (result != 0 || !info) {
        if (info) {
            freeaddrinfo(info);
        }
        return -1;
    }

    struct sockaddr_in *ipv4 = (struct sockaddr_in *)info->ai_addr;
    out->sin_addr = ipv4->sin_addr;
    freeaddrinfo(info);
    return 0;
}

static int network_server_master_send(NetworkServer *server, uint8_t message_type)
{
    NetworkServerMaster *master = &server->master;
    if (!master->enabled || master->socket == INVALID_SOCKET) {
        return 0;
    }

    network_server_master_refresh_entry(server);

    MasterServerEntry entry = master->entry;
    entry.port = htons(entry.port);

    MasterRegisterMessage packet;
    packet.type = message_type;
    packet.entry = entry;

#if defined(_WIN32)
    int sent = sendto(master->socket,
                      (const char *)&packet,
                      (int)sizeof(packet),
                      0,
                      (const struct sockaddr *)&master->master_addr,
                      sizeof(master->master_addr));
    return sent != SOCKET_ERROR;
#else
    ssize_t sent = sendto(master->socket,
                          &packet,
                          sizeof(packet),
                          0,
                          (const struct sockaddr *)&master->master_addr,
                          sizeof(master->master_addr));
    return sent == (ssize_t)sizeof(packet);
#endif
}
static void network_server_master_shutdown(NetworkServer *server)
{
    NetworkServerMaster *master = &server->master;
    if (!master->enabled) {
        return;
    }

    if (master->socket != INVALID_SOCKET) {
        MasterRegisterMessage packet;
        MasterServerEntry entry = master->entry;
        uint32_t players = server->stats.connected_clients;
        if (players > entry.max_players) {
            players = entry.max_players;
        }
        entry.players = (uint8_t)players;
        entry.port = htons(entry.port);
        packet.type = MASTER_MSG_UNREGISTER;
        packet.entry = entry;

#if defined(_WIN32)
        sendto(master->socket,
               (const char *)&packet,
               (int)sizeof(packet),
               0,
               (const struct sockaddr *)&master->master_addr,
               sizeof(master->master_addr));
#else
        sendto(master->socket,
               &packet,
               sizeof(packet),
               0,
               (const struct sockaddr *)&master->master_addr,
               sizeof(master->master_addr));
#endif
        network_server_close_socket(master->socket);
    }

    master->socket = INVALID_SOCKET;
    master->enabled = 0;
    master->registered = 0;
    server->stats.master_registered = false;
}

static void network_server_master_init(NetworkServer *server)
{
    NetworkServerMaster *master = &server->master;
    memset(master, 0, sizeof(*master));
    master->socket = INVALID_SOCKET;

    if (!server->config.advertise) {
        return;
    }

    master->enabled = 1;
    master->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (master->socket == INVALID_SOCKET) {
        master->enabled = 0;
        server->stats.master_failures += 1;
        return;
    }

    const char *master_host = server->config.master_host;
    if (!master_host || master_host[0] == '\0') {
        master_host = "127.0.0.1";
    }

    uint16_t master_port = server->config.master_port ? server->config.master_port : 27050;
    if (network_server_resolve_ipv4(master_host, master_port, &master->master_addr) != 0) {
        network_server_close_socket(master->socket);
        master->socket = INVALID_SOCKET;
        master->enabled = 0;
        server->stats.master_failures += 1;
        return;
    }

    master->heartbeat_interval = server->config.master_heartbeat_interval > 0.0f
                                     ? server->config.master_heartbeat_interval
                                     : MASTER_DEFAULT_HEARTBEAT;
    master->heartbeat_timer = master->heartbeat_interval;
    master->retry_timer = 0.0f;
    master->registered = 0;

    memset(&master->entry, 0, sizeof(master->entry));
    const char *name = server->config.name ? server->config.name : "Slashed Project 1986 Server";
    if (name) {
        size_t copy_len = strlen(name);
        if (copy_len >= MASTER_SERVER_NAME_MAX) {
            copy_len = MASTER_SERVER_NAME_MAX - 1U;
        }
        memcpy(master->entry.name, name, copy_len);
        master->entry.name[copy_len] = '\0';
    }

    const char *address = server->config.public_address;
    if (!address || address[0] == '\0') {
        address = "127.0.0.1";
    }
    size_t addr_len = strlen(address);
    if (addr_len >= MASTER_SERVER_ADDR_MAX) {
        addr_len = MASTER_SERVER_ADDR_MAX - 1U;
    }
    memcpy(master->entry.address, address, addr_len);
    master->entry.address[addr_len] = '\0';
    master->entry.port = server->config.port;
    master->entry.mode = server->config.advertised_mode;
    master->entry.players = 0;
    master->entry.max_players = (uint8_t)(server->stats.max_clients > 255 ? 255 : server->stats.max_clients);
    if (master->entry.max_players == 0) {
        master->entry.max_players = 1;
    }

    network_server_master_refresh_entry(server);
    if (network_server_master_send(server, MASTER_MSG_REGISTER)) {
        master->registered = 1;
        master->heartbeat_timer = 0.0f;
        server->stats.master_registered = true;
        server->stats.master_time_since_contact = 0.0f;
    } else {
        master->registered = 0;
        server->stats.master_registered = false;
        server->stats.master_failures += 1;
        master->retry_timer = master->heartbeat_interval;
    }
}

static void network_server_master_update(NetworkServer *server, float dt)
{
    NetworkServerMaster *master = &server->master;
    if (!master->enabled || master->socket == INVALID_SOCKET) {
        return;
    }

    server->stats.master_time_since_contact += dt;
    if (master->retry_timer > 0.0f) {
        master->retry_timer -= dt;
        if (master->retry_timer > 0.0f) {
            return;
        }
    }

    master->heartbeat_timer += dt;
    if (!master->registered || master->heartbeat_timer >= master->heartbeat_interval) {
        uint8_t type = master->registered ? MASTER_MSG_HEARTBEAT : MASTER_MSG_REGISTER;
        if (network_server_master_send(server, type)) {
            master->heartbeat_timer = 0.0f;
            master->retry_timer = 0.0f;
            server->stats.master_registered = true;
            server->stats.master_time_since_contact = 0.0f;
            master->registered = 1;
        } else {
            master->registered = 0;
            server->stats.master_registered = false;
            server->stats.master_failures += 1;
            master->retry_timer = master->heartbeat_interval;
        }
    }
}

NetworkServer *network_server_create(const NetworkServerConfig *config)
{
    if (!config) {
        return NULL;
    }

    network_server_increment_ref();

    NetworkServer *server = (NetworkServer *)calloc(1, sizeof(NetworkServer));
    if (!server) {
        network_server_decrement_ref();
        return NULL;
    }

    server->config = *config;
    if (server->config.port == 0) {
        server->config.port = 26015;
    }
    if (server->config.max_clients == 0) {
        server->config.max_clients = 8;
    }
    if (!server->config.name || server->config.name[0] == '\0') {
        server->config.name = "Slashed Project 1986 Server";
    }
    if (!server->config.public_address || server->config.public_address[0] == '\0') {
        server->config.public_address = "127.0.0.1";
    }
    if (!server->config.master_host || server->config.master_host[0] == '\0') {
        server->config.master_host = "127.0.0.1";
    }
    if (server->config.master_port == 0) {
        server->config.master_port = 27050;
    }
    if (server->config.master_heartbeat_interval <= 0.0f) {
        server->config.master_heartbeat_interval = MASTER_DEFAULT_HEARTBEAT;
    }

    server->stats.max_clients = server->config.max_clients;
    server->stats.connected_clients = 0;
    server->stats.uptime_seconds = 0.0f;
    server->stats.master_registered = false;
    server->stats.master_time_since_contact = 0.0f;
    server->stats.master_failures = 0;

    ENetAddress address;
    address.host = htonl(INADDR_ANY);
    address.port = server->config.port;

    server->host = enet_host_create(&address, server->stats.max_clients, 1, 0, 0);
    if (!server->host) {
        fprintf(stderr, "[network] failed to create server host\n");
        free(server);
        network_server_decrement_ref();
        return NULL;
    }

    server->client_capacity = server->stats.max_clients ? server->stats.max_clients : 1U;
    server->clients = (NetworkServerClient *)calloc(server->client_capacity, sizeof(NetworkServerClient));
    if (!server->clients) {
        fprintf(stderr, "[network] failed to allocate client slots\n");
        enet_host_destroy(server->host);
        free(server);
        network_server_decrement_ref();
        return NULL;
    }
    server->next_client_id = 0;
    server->snapshot_timer = 0.0f;

    printf("[network] server listening on port %u\n", server->config.port);

    network_server_master_init(server);

    return server;
}

void network_server_destroy(NetworkServer *server)
{
    if (!server) {
        return;
    }

    network_server_master_shutdown(server);

    if (server->host) {
        enet_host_destroy(server->host);
        server->host = NULL;
    }

    free(server->clients);
    server->clients = NULL;
    server->client_capacity = 0;

    free(server);
    network_server_decrement_ref();
}

static void network_server_send_welcome(NetworkServer *server, NetworkServerClient *client)
{
    if (!server || !client || !client->peer) {
        return;
    }

    enet_uint8 payload[4];
    payload[0] = NETWORK_MESSAGE_WELCOME;
    payload[1] = network_server_remote_count(server);
    payload[2] = (enet_uint8)(server->stats.max_clients & 0xFF);
    payload[3] = client->id;

    ENetPacket *packet = enet_packet_create(payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    if (packet) {
        enet_peer_send(client->peer, 0, packet);
    }
}

static void network_server_broadcast_player_count(NetworkServer *server)
{
    if (!server || !server->host) {
        return;
    }

    enet_uint8 payload[2];
    payload[0] = NETWORK_MESSAGE_PLAYER_COUNT;
    payload[1] = network_server_remote_count(server);

    ENetPacket *packet = enet_packet_create(payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    if (packet) {
        enet_host_broadcast(server->host, 0, packet);
    }
}

static void network_server_master_push(NetworkServer *server)
{
    if (!server) {
        return;
    }

    NetworkServerMaster *master = &server->master;
    if (!master->enabled || master->socket == INVALID_SOCKET) {
        return;
    }

    uint8_t type = master->registered ? MASTER_MSG_HEARTBEAT : MASTER_MSG_REGISTER;
    if (network_server_master_send(server, type)) {
        master->heartbeat_timer = 0.0f;
        master->retry_timer = 0.0f;
        server->stats.master_registered = true;
        server->stats.master_time_since_contact = 0.0f;
        master->registered = 1;
    }
}
void network_server_update(NetworkServer *server, float dt)
{
    if (!server || !server->host) {
        return;
    }

    server->stats.uptime_seconds += dt;

    ENetEvent event;
    while (enet_host_service(server->host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            if (server->stats.connected_clients >= server->stats.max_clients) {
                printf("[network] rejecting connection: server full\n");
                enet_peer_disconnect(event.peer, 0);
                break;
            }

            NetworkServerClient *slot = network_server_acquire_client(server, event.peer);
            if (!slot) {
                printf("[network] rejecting connection: no free slot\n");
                enet_peer_disconnect(event.peer, 0);
                break;
            }

            server->stats.connected_clients += 1;
            printf("[network] client connected (%u/%u) - awaiting hello (id=%u)\n",
                   server->stats.connected_clients,
                   server->stats.max_clients,
                   (unsigned)slot->id);
            network_server_broadcast_player_count(server);
            network_server_master_push(server);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            if (event.packet && event.packet->dataLength > 0) {
                NetworkServerClient *client_slot = network_server_find_client(server, event.peer);
                enet_uint8 type = event.packet->data[0];

                if (!client_slot) {
                    printf("[network] WARNING: packet from unknown peer\n");
                } else if (type == NETWORK_MESSAGE_HELLO) {
                    network_server_send_welcome(server, client_slot);
                    network_server_broadcast_player_count(server);
                    network_server_send_snapshot_to(server, event.peer);
                    network_server_master_push(server);
                } else if (type == NETWORK_MESSAGE_CLIENT_STATE && event.packet->dataLength >= 1 + sizeof(float) * 4) {
                    const float *payload = (const float *)(event.packet->data + 1);
                    memcpy(client_slot->position, payload, sizeof(float) * 3);
                    memcpy(&client_slot->yaw, payload + 3, sizeof(float));
                    client_slot->has_state = 1;
                    server->snapshot_timer = 0.0f;
                    network_server_broadcast_snapshot(server);
                } else if (type == NETWORK_MESSAGE_CLIENT_WEAPON_EVENT && event.packet->dataLength >= 1 + NETWORK_WEAPON_EVENT_DATA_SIZE) {
                    if (client_slot) {
                        size_t payload_size = event.packet->dataLength - 1;
                        if (payload_size > NETWORK_WEAPON_EVENT_DATA_SIZE) {
                            payload_size = NETWORK_WEAPON_EVENT_DATA_SIZE;
                        }

                        enet_uint8 buffer[2 + NETWORK_WEAPON_EVENT_DATA_SIZE];
                        buffer[0] = NETWORK_MESSAGE_WEAPON_EVENT;
                        buffer[1] = client_slot->id;
                        memset(buffer + 2, 0, NETWORK_WEAPON_EVENT_DATA_SIZE);
                        memcpy(buffer + 2, event.packet->data + 1, payload_size);

                        ENetPacket *relay = enet_packet_create(buffer,
                                                               sizeof(buffer),
                                                               ENET_PACKET_FLAG_RELIABLE);
                        if (relay) {
                            enet_host_broadcast(server->host, 0, relay);
                        }
                    }
                } else if (type == NETWORK_MESSAGE_CLIENT_VOICE_DATA && event.packet->dataLength > 1 + 7) {
                    if (client_slot && client_slot->has_state) {
                        const enet_uint8 *payload = event.packet->data;
                        uint8_t codec = payload[1];
                        uint8_t channels = payload[2];
                        uint16_t sample_rate = (uint16_t)(payload[3] | ((uint16_t)payload[4] << 8));
                        uint16_t frame_count = (uint16_t)(payload[5] | ((uint16_t)payload[6] << 8));
                        uint8_t gain_byte = payload[7];
                        size_t voice_bytes = event.packet->dataLength - 8;

                        if (codec != NETWORK_VOICE_CODEC_PCM16 ||
                            channels == 0U || channels > NETWORK_VOICE_MAX_CHANNELS ||
                            frame_count == 0U ||
                            voice_bytes > NETWORK_VOICE_MAX_DATA ||
                            voice_bytes != (size_t)frame_count * channels * sizeof(int16_t)) {
                            /* ignore malformed voice packets */
                            printf("[network] ignoring invalid voice packet from %u\n", (unsigned)client_slot->id);
                        } else {
                            float emitter_gain = (float)gain_byte / 255.0f;
                            if (emitter_gain <= 0.0f) {
                                emitter_gain = 1.0f;
                            } else if (emitter_gain > 1.0f) {
                                emitter_gain = 1.0f;
                            }

                            NetworkVoiceChatMode voice_mode = server->config.voice_mode;
                            float voice_range = server->config.voice_range > 0.0f ? server->config.voice_range : NETWORK_VOICE_RANGE;

                            for (uint32_t i = 0; i < server->client_capacity; ++i) {
                                NetworkServerClient *target = &server->clients[i];
                                if (!target->connected || !target->has_state || target->peer == client_slot->peer) {
                                    continue;
                                }

                                float volume_scale = emitter_gain;
                                if (voice_mode != NETWORK_VOICE_CHAT_GLOBAL) {
                                    float dx = target->position[0] - client_slot->position[0];
                                    float dy = target->position[1] - client_slot->position[1];
                                    float dz = target->position[2] - client_slot->position[2];
                                    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
                                    if (distance > voice_range) {
                                        continue;
                                    }

                                    float attenuation = 1.0f - (distance / voice_range);
                                    if (attenuation <= 0.0f) {
                                        continue;
                                    }
                                    volume_scale *= attenuation;
                                }

                                if (volume_scale <= 0.0f) {
                                    continue;
                                }
                                if (volume_scale > 1.0f) {
                                    volume_scale = 1.0f;
                                }

                                enet_uint8 volume_byte = (enet_uint8)(volume_scale * 255.0f);
                                if (volume_byte == 0U) {
                                    continue;
                                }

                                enet_uint8 buffer[9 + NETWORK_VOICE_MAX_DATA];
                                buffer[0] = NETWORK_MESSAGE_VOICE_DATA;
                                buffer[1] = client_slot->id;
                                buffer[2] = codec;
                                buffer[3] = channels;
                                buffer[4] = (enet_uint8)(sample_rate & 0xFF);
                                buffer[5] = (enet_uint8)((sample_rate >> 8) & 0xFF);
                                buffer[6] = (enet_uint8)(frame_count & 0xFF);
                                buffer[7] = (enet_uint8)((frame_count >> 8) & 0xFF);
                                buffer[8] = volume_byte;
                                memcpy(buffer + 9, payload + 8, voice_bytes);

                                ENetPacket *voice_packet =
                                    enet_packet_create(buffer,
                                                       9 + voice_bytes,
                                                       ENET_PACKET_FLAG_UNSEQUENCED | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
                                if (voice_packet) {
                                    enet_peer_send(target->peer, 0, voice_packet);
                                }
                            }
                        }
                    }
                } else {
                    printf("[network] unknown message type: 0x%02X\n", type);
                }
            }
            if (event.packet) {
                enet_packet_destroy(event.packet);
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            NetworkServerClient *slot = network_server_find_client(server, event.peer);
            if (slot && slot->connected) {
                network_server_release_client(server, event.peer);
                if (server->stats.connected_clients > 0) {
                    server->stats.connected_clients -= 1;
                }
            }

            printf("[network] client disconnected (%u/%u) - reason: %u\n",
                   server->stats.connected_clients,
                   server->stats.max_clients,
                   (unsigned)event.data);
            network_server_broadcast_player_count(server);
            network_server_master_push(server);
            network_server_broadcast_snapshot(server);
            break;
        }
        default:
            break;
        }
    }

    if (server->stats.connected_clients > 0U) {
        server->snapshot_timer += dt;
        if (server->snapshot_timer >= NETWORK_SERVER_SNAPSHOT_INTERVAL) {
            server->snapshot_timer = 0.0f;
            network_server_broadcast_snapshot(server);
        }
    } else {
        server->snapshot_timer = 0.0f;
    }

    network_server_master_update(server, dt);
}

const NetworkServerStats *network_server_stats(const NetworkServer *server)
{
    if (!server) {
        return NULL;
    }
    return &server->stats;
}
