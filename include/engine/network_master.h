#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "engine/master_protocol.h"

typedef struct MasterClient MasterClient;

typedef struct MasterClientConfig {
    const char *host;
    uint16_t port;
} MasterClientConfig;

bool master_client_global_init(void);
void master_client_global_shutdown(void);

MasterClient *master_client_create(const MasterClientConfig *config);
void master_client_destroy(MasterClient *client);
void master_client_set_timeout(MasterClient *client, uint32_t timeout_ms);

bool master_client_request_list(MasterClient *client,
                                MasterServerEntry *out_entries,
                                size_t max_entries,
                                size_t *out_count);
