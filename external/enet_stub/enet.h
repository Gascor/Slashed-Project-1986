#ifndef ENET_STUB_H
#define ENET_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef uint32_t enet_uint32;
typedef uint16_t enet_uint16;
typedef uint8_t enet_uint8;

/* enet.h (stub) — make ENetPacket fields visible to the engine code */
typedef unsigned char enet_uint8;
typedef unsigned int  enet_uint32;

typedef struct _ENetAddress {
    enet_uint32 host;
    enet_uint16 port;
} ENetAddress;

typedef enum _ENetEventType {
    ENET_EVENT_TYPE_NONE = 0,
    ENET_EVENT_TYPE_CONNECT,
    ENET_EVENT_TYPE_DISCONNECT,
    ENET_EVENT_TYPE_RECEIVE
} ENetEventType;

struct _ENetPeer;
struct _ENetPacket;
struct _ENetHost;

struct _ENetPacket {
    size_t      dataLength;
    enet_uint8* data;
    enet_uint32 flags;      /* optional, but harmless */
};

typedef struct _ENetPeer ENetPeer;
typedef struct _ENetPacket ENetPacket;
typedef struct _ENetHost ENetHost;

typedef struct _ENetEvent {
    ENetEventType type;
    ENetPeer *peer;
    ENetPacket *packet;
    enet_uint32 data;
} ENetEvent;

#define ENET_PACKET_FLAG_RELIABLE 1

int enet_initialize(void);
void enet_deinitialize(void);

ENetHost *enet_host_create(const ENetAddress *address,
                           size_t peerCount,
                           size_t channelLimit,
                           enet_uint32 incomingBandwidth,
                           enet_uint32 outgoingBandwidth);
void enet_host_destroy(ENetHost *host);

int enet_host_service(ENetHost *host, ENetEvent *event, enet_uint32 timeout_ms);

ENetPeer *enet_host_connect(ENetHost *host, const ENetAddress *address, size_t channelCount, enet_uint32 data);
void enet_peer_disconnect(ENetPeer *peer, enet_uint32 data);
void enet_peer_reset(ENetPeer *peer);

int enet_peer_send(ENetPeer *peer, enet_uint8 channelID, ENetPacket *packet);
void enet_host_broadcast(ENetHost *host, enet_uint8 channelID, ENetPacket *packet);

ENetPacket *enet_packet_create(const void *data, size_t dataLength, enet_uint32 flags);
void enet_packet_destroy(ENetPacket *packet);

enet_uint32 enet_time_get(void);

#ifdef __cplusplus
}
#endif

#endif /* ENET_STUB_H */
