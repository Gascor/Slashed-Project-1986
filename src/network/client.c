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

typedef struct NetworkClient {
    NetworkClientConfig config;
    ENetHost *host;
    ENetPeer *peer;
    NetworkClientStats stats;
    double time_since_last_packet;
    double handshake_timer;
    double handshake_start;
    int connecting;
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
        printf("[network] failed to create ENet peer\n");
        return;
    }

    client->connecting = 1;
    client->stats.connected = false;
    client->stats.time_since_last_packet = 0.0f;
    client->stats.simulated_ping_ms = 0.0f;
    client->stats.remote_player_count = 0;
    client->handshake_start = network_get_time_seconds();
}

void network_client_disconnect(NetworkClient *client)
{
    if (!client || !client->host || !client->peer) {
        return;
    }

    enet_peer_disconnect(client->peer, 0);
    enet_peer_reset(client->peer);
    client->peer = NULL;
    client->connecting = 0;
    client->stats.connected = false;
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
    if (message_type == NETWORK_MESSAGE_WELCOME) {
        client->stats.connected = true;
        client->connecting = 0;
        if (size >= 3) {
            client->stats.remote_player_count = data[1];
        }
        double now = network_get_time_seconds();
        double rtt = now - client->handshake_start;
        client->stats.simulated_ping_ms = (float)(rtt * 1000.0);
    } else if (message_type == NETWORK_MESSAGE_PLAYER_COUNT) {
        if (size >= 2) {
            client->stats.remote_player_count = data[1];
        }
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
            enet_peer_send(event.peer, 0, packet);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            network_client_handle_packet(client, &event);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            client->stats.connected = false;
            client->connecting = 0;
            client->peer = NULL;
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




