#include "engine/network.h"

bool network_fetch_master_list(const MasterClientConfig *config,
                               MasterServerEntry *out_entries,
                               size_t max_entries,
                               size_t *out_count)
{
    size_t count = 0;
    bool success = false;

    if (!master_client_global_init()) {
        if (out_count) {
            *out_count = 0;
        }
        return false;
    }

    MasterClient *client = master_client_create(config);
    if (client) {
        success = master_client_request_list(client, out_entries, max_entries, &count);
        master_client_destroy(client);
    } else {
        success = master_client_request_list(NULL, out_entries, max_entries, &count);
    }

    master_client_global_shutdown();

    if (out_count) {
        *out_count = count;
    }

    return success;
}
