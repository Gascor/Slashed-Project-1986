#pragma once

#include <stdint.h>

#define MASTER_MSG_REGISTER     0x01
#define MASTER_MSG_HEARTBEAT    0x02
#define MASTER_MSG_UNREGISTER   0x03
#define MASTER_MSG_LIST_REQUEST 0x04
#define MASTER_MSG_LIST_RESPONSE 0x05

#define MASTER_SERVER_NAME_MAX   64
#define MASTER_SERVER_ADDR_MAX   64

#pragma pack(push, 1)
typedef struct MasterServerEntry {
    char name[MASTER_SERVER_NAME_MAX];
    char address[MASTER_SERVER_ADDR_MAX];
    uint16_t port;
    uint8_t mode;
    uint8_t players;
    uint8_t max_players;
} MasterServerEntry;

typedef struct MasterRegisterMessage {
    uint8_t type;
    MasterServerEntry entry;
} MasterRegisterMessage;

typedef struct MasterHeartbeatMessage {
    uint8_t type;
    MasterServerEntry entry;
} MasterHeartbeatMessage;

typedef struct MasterListRequest {
    uint8_t type;
} MasterListRequest;

typedef struct MasterListResponseHeader {
    uint8_t type;
    uint8_t count;
} MasterListResponseHeader;
#pragma pack(pop)
