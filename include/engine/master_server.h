#pragma once

#include <stddef.h>
#include <stdint.h>

#include "engine/master_protocol.h"

typedef struct MasterServer MasterServer;

typedef struct MasterServerConfig {
    uint16_t port;
    uint32_t max_servers;
    float heartbeat_timeout;
    float cleanup_interval;
} MasterServerConfig;

typedef struct MasterServerStats {
    uint32_t active_servers;
    uint32_t max_servers;
    float uptime_seconds;
    uint32_t register_messages;
    uint32_t heartbeat_messages;
    uint32_t unregister_messages;
    uint32_t list_requests;
    uint32_t dropped_servers;
} MasterServerStats;

MasterServer *master_server_create(const MasterServerConfig *config);
void master_server_destroy(MasterServer *server);

void master_server_update(MasterServer *server, float dt);
size_t master_server_entries(const MasterServer *server, MasterServerEntry *out_entries, size_t max_entries);
const MasterServerStats *master_server_stats(const MasterServer *server);
