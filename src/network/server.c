#include "engine/network_server.h"

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

#define MASTER_DEFAULT_HEARTBEAT 5.0f

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

typedef struct NetworkServer {
    NetworkServerConfig config;
    ENetHost *host;
    NetworkServerStats stats;
    NetworkServerMaster master;
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
    strncpy(master->entry.name, name, MASTER_SERVER_NAME_MAX - 1);

    const char *address = server->config.public_address;
    if (!address || address[0] == '\0') {
        address = "127.0.0.1";
    }
    strncpy(master->entry.address, address, MASTER_SERVER_ADDR_MAX - 1);
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

    free(server);
    network_server_decrement_ref();
}

static void network_server_send_welcome(ENetPeer *peer, const NetworkServerStats *stats)
{
    if (!peer || !stats) {
        return;
    }

    enet_uint8 payload[3];
    payload[0] = NETWORK_MESSAGE_WELCOME;
    enet_uint8 remote_count = 0;
    if (stats->connected_clients > 0U) {
        remote_count = (enet_uint8)((stats->connected_clients - 1U) & 0xFF);
    }
    payload[1] = remote_count;
    payload[2] = (enet_uint8)(stats->max_clients & 0xFF);

    ENetPacket *packet = enet_packet_create(payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, packet);
}

static void network_server_broadcast_player_count(NetworkServer *server)
{
    if (!server || !server->host) {
        return;
    }

    enet_uint8 remote_count = 0;
    if (server->stats.connected_clients > 0U) {
        remote_count = (enet_uint8)((server->stats.connected_clients - 1U) & 0xFF);
    }

    enet_uint8 payload[2];
    payload[0] = NETWORK_MESSAGE_PLAYER_COUNT;
    payload[1] = remote_count;

    ENetPacket *packet = enet_packet_create(payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server->host, 0, packet);
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
        case ENET_EVENT_TYPE_CONNECT:
            if (server->stats.connected_clients < server->stats.max_clients) {
                server->stats.connected_clients += 1;
            }
            printf("[network] client connected (%u/%u)\n",
                   server->stats.connected_clients,
                   server->stats.max_clients);
            network_server_broadcast_player_count(server);
            network_server_master_push(server);
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (event.packet && event.packet->dataLength > 0) {
                enet_uint8 type = event.packet->data[0];
                if (type == NETWORK_MESSAGE_HELLO) {
                    network_server_send_welcome(event.peer, &server->stats);
                    network_server_broadcast_player_count(server);
                    network_server_master_push(server);
                }
            }
            if (event.packet) {
                enet_packet_destroy(event.packet);
            }
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (server->stats.connected_clients > 0) {
                server->stats.connected_clients -= 1;
            }
            printf("[network] client disconnected (%u/%u)\n",
                   server->stats.connected_clients,
                   server->stats.max_clients);
            network_server_broadcast_player_count(server);
            network_server_master_push(server);
            break;
        default:
            break;
        }
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
