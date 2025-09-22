#include "enet.h"

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    pragma comment(lib, "Ws2_32.lib")
#else
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <netdb.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <sys/time.h>
#    include <errno.h>
typedef int SOCKET;
#    define INVALID_SOCKET (-1)
#    define SOCKET_ERROR (-1)
#    define closesocket close
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ENET_STUB_MSG_HELLO 0x01
#define ENET_STUB_MSG_HELLO_ACK 0x02
#define ENET_STUB_MSG_PAYLOAD 0x03
#define ENET_STUB_MSG_DISCONNECT 0x04

#ifndef ENET_STUB_MAX_PACKET
#    define ENET_STUB_MAX_PACKET 1200
#endif


struct _ENetHost;

typedef struct ENetPeerImpl {
    int in_use;
    int connected;
    struct sockaddr_in address;
    enet_uint32 id;
    struct _ENetHost *host;
} ENetPeerImpl;

struct _ENetHost {
    SOCKET socket;
    int is_server;
    ENetPeerImpl *peers;
    size_t peer_count;
    ENetAddress address;
    enet_uint32 next_peer_id;
    struct sockaddr_in server_addr;
    ENetPeerImpl *server_peer;
};

static int g_enet_init_refcount = 0;

#if defined(_WIN32)
static int enet_stub_startup(void)
{
    if (g_enet_init_refcount == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return -1;
        }
    }
    ++g_enet_init_refcount;
    return 0;
}

static void enet_stub_cleanup(void)
{
    if (g_enet_init_refcount <= 0) {
        return;
    }
    --g_enet_init_refcount;
    if (g_enet_init_refcount == 0) {
        WSACleanup();
    }
}
#else
static int enet_stub_startup(void)
{
    ++g_enet_init_refcount;
    return 0;
}

static void enet_stub_cleanup(void)
{
    if (g_enet_init_refcount <= 0) {
        return;
    }
    --g_enet_init_refcount;
}
#endif

int enet_initialize(void)
{
    return enet_stub_startup();
}

void enet_deinitialize(void)
{
    enet_stub_cleanup();
}

static void enet_stub_address_to_sockaddr(const ENetAddress *address, struct sockaddr_in *out)
{
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    if (address) {
        out->sin_port = htons(address->port);
        if (address->host == 0) {
            out->sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            out->sin_addr.s_addr = htonl(address->host);
        }
    } else {
        out->sin_port = 0;
        out->sin_addr.s_addr = htonl(INADDR_ANY);
    }
}

static ENetPeerImpl *enet_stub_find_peer(ENetHost *host, const struct sockaddr_in *addr)
{
    if (!host || !addr) {
        return NULL;
    }
    for (size_t i = 0; i < host->peer_count; ++i) {
        ENetPeerImpl *peer = &host->peers[i];
        if (!peer->in_use) {
            continue;
        }
        if (peer->address.sin_addr.s_addr == addr->sin_addr.s_addr &&
            peer->address.sin_port == addr->sin_port) {
            return peer;
        }
    }
    return NULL;
}

static ENetPeerImpl *enet_stub_alloc_peer(ENetHost *host)
{
    if (!host) {
        return NULL;
    }
    for (size_t i = 0; i < host->peer_count; ++i) {
        ENetPeerImpl *peer = &host->peers[i];
        if (!peer->in_use) {
            memset(peer, 0, sizeof(*peer));
            peer->in_use = 1;
            peer->connected = 0;
            peer->id = ++host->next_peer_id;
            peer->host = host;
            return peer;
        }
    }
    return NULL;
}

static int enet_stub_set_nonblocking(SOCKET sock)
{
#if defined(_WIN32)
    u_long nb = 1;
    return ioctlsocket(sock, FIONBIO, &nb);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
#endif
}

ENetHost *enet_host_create(const ENetAddress *address,
                           size_t peerCount,
                           size_t channelLimit,
                           enet_uint32 incomingBandwidth,
                           enet_uint32 outgoingBandwidth)
{
    (void)channelLimit;
    (void)incomingBandwidth;
    (void)outgoingBandwidth;

    if (peerCount == 0) {
        peerCount = 1;
    }

    ENetHost *host = (ENetHost *)calloc(1, sizeof(ENetHost));
    if (!host) {
        return NULL;
    }

    if (enet_stub_startup() != 0) {
        free(host);
        return NULL;
    }

    host->peers = (ENetPeerImpl *)calloc(peerCount, sizeof(ENetPeerImpl));
    if (!host->peers) {
        free(host);
        enet_stub_cleanup();
        return NULL;
    }
    host->peer_count = peerCount;

    host->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (host->socket == INVALID_SOCKET) {
        free(host->peers);
        free(host);
        enet_stub_cleanup();
        return NULL;
    }

#if defined(_WIN32)
    int opt = 1;
    setsockopt(host->socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    int opt = 1;
    setsockopt(host->socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    if (address) {
        struct sockaddr_in bind_addr;
        enet_stub_address_to_sockaddr(address, &bind_addr);
        if (bind(host->socket, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
            closesocket(host->socket);
            free(host->peers);
            free(host);
            enet_stub_cleanup();
            return NULL;
        }
        host->is_server = 1;
        host->address = *address;
    } else {
        host->is_server = 0;
        host->address.host = 0;
        host->address.port = 0;
    }

    if (enet_stub_set_nonblocking(host->socket) != 0) {
        closesocket(host->socket);
        free(host->peers);
        free(host);
        enet_stub_cleanup();
        return NULL;
    }

    host->next_peer_id = 1;

    return host;
}

void enet_host_destroy(ENetHost *host)
{
    if (!host) {
        return;
    }

    if (host->socket != INVALID_SOCKET) {
        closesocket(host->socket);
    }
    free(host->peers);
    free(host);
    enet_stub_cleanup();
}

static void enet_stub_send_control(ENetHost *host, ENetPeerImpl *peer, enet_uint8 type)
{
    if (!host || !peer) {
        return;
    }
    enet_uint8 buffer[1] = {type};
    sendto(host->socket,
           (const char *)buffer,
           1,
           0,
           (const struct sockaddr *)&peer->address,
           sizeof(peer->address));
}

ENetPeer *enet_host_connect(ENetHost *host, const ENetAddress *address, size_t channelCount, enet_uint32 data)
{
    (void)channelCount;
    (void)data;
    if (!host || host->is_server) {
        return NULL;
    }

    ENetPeerImpl *peer = enet_stub_alloc_peer(host);
    if (!peer) {
        return NULL;
    }

    struct sockaddr_in addr;
    enet_stub_address_to_sockaddr(address, &addr);
    peer->address = addr;
    peer->connected = 0;
    host->server_addr = addr;
    host->server_peer = peer;

    enet_stub_send_control(host, peer, ENET_STUB_MSG_HELLO);

    return (ENetPeer *)peer;
}

void enet_peer_disconnect(ENetPeer *peer_ptr, enet_uint32 data)
{
    (void)data;
    ENetPeerImpl *peer = (ENetPeerImpl *)peer_ptr;
    if (!peer || !peer->in_use) {
        return;
    }
    enet_stub_send_control(peer->host, peer, ENET_STUB_MSG_DISCONNECT);
    peer->connected = 0;
}

void enet_peer_reset(ENetPeer *peer_ptr)
{
    ENetPeerImpl *peer = (ENetPeerImpl *)peer_ptr;
    if (!peer) {
        return;
    }
    peer->in_use = 0;
    peer->connected = 0;
}

static ENetPacket *enet_stub_packet_from_buffer(const enet_uint8 *buffer, size_t length)
{
    if (length == 0) {
        return NULL;
    }
    ENetPacket *packet = (ENetPacket *)calloc(1, sizeof(ENetPacket));
    if (!packet) {
        return NULL;
    }
    packet->data = (enet_uint8 *)malloc(length);
    if (!packet->data) {
        free(packet);
        return NULL;
    }
    memcpy(packet->data, buffer, length);
    packet->dataLength = length;
    packet->flags = 0;
    return packet;
}

static void enet_stub_fill_event(ENetEvent *event, ENetEventType type, ENetPeerImpl *peer, ENetPacket *packet)
{
    if (!event) {
        return;
    }
    event->type = type;
    event->peer = (ENetPeer *)peer;
    event->packet = packet;
    event->data = 0;
}

static int enet_stub_process_incoming(ENetHost *host, ENetEvent *event)
{
    enet_uint8 buffer[ENET_STUB_MAX_PACKET];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int len = (int)recvfrom(host->socket,
                            (char *)buffer,
                            (int)sizeof(buffer),
                            0,
                            (struct sockaddr *)&from,
                            &from_len);
    if (len <= 0) {
        return 0;
    }

    enet_uint8 message_type = buffer[0];
    ENetPeerImpl *peer = enet_stub_find_peer(host, &from);

    if (host->is_server) {
        if (!peer) {
            if (message_type == ENET_STUB_MSG_HELLO) {
                peer = enet_stub_alloc_peer(host);
                if (!peer) {
                    return 0;
                }
                peer->address = from;
                peer->connected = 1;
                enet_stub_send_control(host, peer, ENET_STUB_MSG_HELLO_ACK);
                enet_stub_fill_event(event, ENET_EVENT_TYPE_CONNECT, peer, NULL);
                return 1;
            }
            return 0;
        }

        if (message_type == ENET_STUB_MSG_HELLO) {
            peer->connected = 1;
            enet_stub_send_control(host, peer, ENET_STUB_MSG_HELLO_ACK);
            enet_stub_fill_event(event, ENET_EVENT_TYPE_CONNECT, peer, NULL);
            return 1;
        } else if (message_type == ENET_STUB_MSG_DISCONNECT) {
            peer->connected = 0;
            enet_stub_fill_event(event, ENET_EVENT_TYPE_DISCONNECT, peer, NULL);
            peer->in_use = 0;
            return 1;
        } else if (message_type == ENET_STUB_MSG_PAYLOAD) {
            ENetPacket *packet = enet_stub_packet_from_buffer(buffer + 1, (size_t)len - 1);
            enet_stub_fill_event(event, ENET_EVENT_TYPE_RECEIVE, peer, packet);
            return 1;
        }
    } else {
        peer = host->server_peer;
        if (!peer) {
            return 0;
        }
        if (message_type == ENET_STUB_MSG_HELLO_ACK) {
            peer->connected = 1;
            enet_stub_fill_event(event, ENET_EVENT_TYPE_CONNECT, peer, NULL);
            return 1;
        } else if (message_type == ENET_STUB_MSG_DISCONNECT) {
            peer->connected = 0;
            enet_stub_fill_event(event, ENET_EVENT_TYPE_DISCONNECT, peer, NULL);
            return 1;
        } else if (message_type == ENET_STUB_MSG_PAYLOAD) {
            ENetPacket *packet = enet_stub_packet_from_buffer(buffer + 1, (size_t)len - 1);
            enet_stub_fill_event(event, ENET_EVENT_TYPE_RECEIVE, peer, packet);
            return 1;
        }
    }

    return 0;
}

int enet_host_service(ENetHost *host, ENetEvent *event, enet_uint32 timeout_ms)
{
    if (!host || !event) {
        return -1;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(host->socket, &read_fds);

    struct timeval tv;
    struct timeval *tv_ptr = NULL;
    if (timeout_ms != 0) {
        tv.tv_sec = (long)(timeout_ms / 1000);
        tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
        tv_ptr = &tv;
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        tv_ptr = &tv;
    }

#if defined(_WIN32)
    int ready = select(0, &read_fds, NULL, NULL, tv_ptr);
#else
    int ready = select(host->socket + 1, &read_fds, NULL, NULL, tv_ptr);
#endif
    if (ready <= 0) {
        event->type = ENET_EVENT_TYPE_NONE;
        event->peer = NULL;
        event->packet = NULL;
        event->data = 0;
        return 0;
    }

    return enet_stub_process_incoming(host, event);
}

int enet_peer_send(ENetPeer *peer_ptr, enet_uint8 channelID, ENetPacket *packet)
{
    (void)channelID;
    ENetPeerImpl *peer = (ENetPeerImpl *)peer_ptr;
    if (!peer || !peer->in_use || !packet || packet->dataLength + 1 > ENET_STUB_MAX_PACKET) {
        return -1;
    }

    enet_uint8 buffer[ENET_STUB_MAX_PACKET];
    buffer[0] = ENET_STUB_MSG_PAYLOAD;
    memcpy(buffer + 1, packet->data, packet->dataLength);

    int sent = (int)sendto(peer->host->socket,
                           (const char *)buffer,
                           (int)(packet->dataLength + 1),
                           0,
                           (const struct sockaddr *)&peer->address,
                           sizeof(peer->address));
    return (sent == (int)(packet->dataLength + 1)) ? 0 : -1;
}

void enet_host_broadcast(ENetHost *host, enet_uint8 channelID, ENetPacket *packet)
{
    if (!host || !packet) {
        return;
    }
    for (size_t i = 0; i < host->peer_count; ++i) {
        ENetPeerImpl *peer = &host->peers[i];
        if (!peer->in_use || !peer->connected) {
            continue;
        }
        enet_peer_send((ENetPeer *)peer, channelID, packet);
    }
}

ENetPacket *enet_packet_create(const void *data, size_t dataLength, enet_uint32 flags)
{
    ENetPacket *packet = (ENetPacket *)calloc(1, sizeof(ENetPacket));
    if (!packet) {
        return NULL;
    }
    packet->data = (enet_uint8 *)malloc(dataLength);
    if (!packet->data) {
        free(packet);
        return NULL;
    }
    if (data && dataLength > 0) {
        memcpy(packet->data, data, dataLength);
    }
    packet->dataLength = dataLength;
    packet->flags = flags;
    return packet;
}

void enet_packet_destroy(ENetPacket *packet)
{
    if (!packet) {
        return;
    }
    free(packet->data);
    free(packet);
}

enet_uint32 enet_time_get(void)
{
#if defined(_WIN32)
    return (enet_uint32)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (enet_uint32)((tv.tv_sec * 1000u) + (tv.tv_usec / 1000u));
#endif
}

