#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "engine/master_protocol.h"
#include "engine/network_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ServerMode {
    SERVER_MODE_CTF = 0,
    SERVER_MODE_TDM,
    SERVER_MODE_SLENDER,
    SERVER_MODE_COUNT
} ServerMode;

typedef enum ServerSortColumn {
    SERVER_SORT_NAME = 0,
    SERVER_SORT_MODE,
    SERVER_SORT_PING,
    SERVER_SORT_PLAYERS
} ServerSortColumn;

#ifndef SERVER_BROWSER_MAX_ENTRIES
#    define SERVER_BROWSER_MAX_ENTRIES 32
#endif

typedef struct ServerEntry {
    MasterServerEntry master;
    ServerMode mode;
    int players;
    int max_players;
    int ping_ms;
    bool official;
    bool password;
} ServerEntry;

typedef struct ServerBrowserState {
    ServerEntry entries[SERVER_BROWSER_MAX_ENTRIES];
    size_t entry_count;
    MasterClientConfig master_config;
    bool mode_filter[SERVER_MODE_COUNT];
    ServerSortColumn sort_column;
    bool sort_descending;
    int visible_indices[SERVER_BROWSER_MAX_ENTRIES];
    size_t visible_count;
    int selected_entry;
    int hover_entry;
    bool needs_refresh;
} ServerBrowserState;

void server_browser_init(ServerBrowserState *browser, const MasterClientConfig *config);
void server_browser_set_master(ServerBrowserState *browser, const MasterClientConfig *config);
bool server_browser_fetch(ServerBrowserState *browser);
void server_browser_refresh(ServerBrowserState *browser);
void server_browser_toggle_sort(ServerBrowserState *browser, ServerSortColumn column);
bool server_browser_mode_filter_any(const ServerBrowserState *browser);
const char *server_mode_name(ServerMode mode);

#ifdef __cplusplus
}
#endif
