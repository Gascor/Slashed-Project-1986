#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "engine/network_master.h"

typedef struct NetworkClient NetworkClient;

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

NetworkClient *network_client_create(const NetworkClientConfig *config);
void network_client_destroy(NetworkClient *client);

void network_client_connect(NetworkClient *client);
void network_client_disconnect(NetworkClient *client);
void network_client_update(NetworkClient *client, float dt);

bool network_client_is_connected(const NetworkClient *client);
const NetworkClientStats *network_client_stats(const NetworkClient *client);

bool network_fetch_master_list(const MasterClientConfig *config,
                               MasterServerEntry *out_entries,
                               size_t max_entries,
                               size_t *out_count);
