#pragma once

#include "engine/master_protocol.h"

#include <stdbool.h>
#include <stddef.h>

#define GAME_MAX_SERVER_LIST 64
#define GAME_SERVER_STATUS_MAX 128

typedef struct MasterClientConfig MasterClientConfig;

typedef struct ServerBrowserState {
    MasterServerEntry entries[GAME_MAX_SERVER_LIST];
    size_t entry_count;
    int selection;
    bool open;
    bool last_request_success;
    char status[GAME_SERVER_STATUS_MAX];
    double last_refresh_time;
} ServerBrowserState;

void server_browser_init(ServerBrowserState *browser);
bool server_browser_open(ServerBrowserState *browser,
                         const MasterClientConfig *config,
                         double time_seconds);
void server_browser_close(ServerBrowserState *browser);
bool server_browser_refresh(ServerBrowserState *browser,
                            const MasterClientConfig *config,
                            double time_seconds);
void server_browser_move_selection(ServerBrowserState *browser, int delta);
void server_browser_set_selection(ServerBrowserState *browser, int selection);
bool server_browser_has_entries(const ServerBrowserState *browser);
const MasterServerEntry *server_browser_selected(const ServerBrowserState *browser);
