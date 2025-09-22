#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct NetworkServer NetworkServer;

typedef struct NetworkServerConfig {
    uint16_t port;
    uint32_t max_clients;
    const char *name;
    const char *public_address;
    const char *master_host;
    uint16_t master_port;
    float master_heartbeat_interval;
    uint8_t advertised_mode;
    bool advertise;
} NetworkServerConfig;

typedef struct NetworkServerStats {
    uint32_t connected_clients;
    uint32_t max_clients;
    float uptime_seconds;
    bool master_registered;
    float master_time_since_contact;
    uint32_t master_failures;
} NetworkServerStats;

NetworkServer *network_server_create(const NetworkServerConfig *config);
void network_server_destroy(NetworkServer *server);

void network_server_update(NetworkServer *server, float dt);
const NetworkServerStats *network_server_stats(const NetworkServer *server);
