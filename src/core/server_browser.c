#include "engine/server_browser.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine/network.h"

#ifndef APP_MASTER_DEFAULT_HOST
#    define APP_MASTER_DEFAULT_HOST "127.0.0.1"
#endif
#ifndef APP_MASTER_DEFAULT_PORT
#    define APP_MASTER_DEFAULT_PORT 27050
#endif

static ServerBrowserState *g_sort_browser = NULL;
static ServerSortColumn g_sort_column = SERVER_SORT_NAME;
static bool g_sort_descending = false;

static ServerMode server_mode_from_master(uint8_t mode)
{
    switch (mode) {
    case 0:
        return SERVER_MODE_CTF;
    case 1:
        return SERVER_MODE_TDM;
    case 2:
        return SERVER_MODE_SLENDER;
    default:
        return (ServerMode)(mode % SERVER_MODE_COUNT);
    }
}

static int server_compare_indices(const void *lhs, const void *rhs)
{
    if (!g_sort_browser) {
        return 0;
    }

    int a_index = *(const int *)lhs;
    int b_index = *(const int *)rhs;

    const ServerEntry *a = &g_sort_browser->entries[a_index];
    const ServerEntry *b = &g_sort_browser->entries[b_index];

    int result = 0;
    switch (g_sort_column) {
    case SERVER_SORT_NAME:
        result = _stricmp(a->master.name, b->master.name);
        break;
    case SERVER_SORT_MODE:
        result = (int)a->mode - (int)b->mode;
        break;
    case SERVER_SORT_PING:
        result = a->ping_ms - b->ping_ms;
        break;
    case SERVER_SORT_PLAYERS:
        result = (a->players - b->players);
        if (result == 0) {
            result = a->max_players - b->max_players;
        }
        break;
    default:
        break;
    }

    if (!g_sort_descending) {
        return result;
    }
    return -result;
}

static void server_browser_clear(ServerBrowserState *browser)
{
    if (!browser) {
        return;
    }
    memset(browser, 0, sizeof(*browser));
    for (int i = 0; i < SERVER_MODE_COUNT; ++i) {
        browser->mode_filter[i] = true;
    }
    browser->sort_column = SERVER_SORT_PING;
    browser->sort_descending = false;
    browser->selected_entry = -1;
    browser->hover_entry = -1;
    browser->needs_refresh = true;
}

static void server_browser_generate_fallback(ServerBrowserState *browser)
{
    static const struct {
        const char *name;
        ServerMode mode;
        int players;
        int max_players;
        int ping;
        bool official;
        bool password;
    } kFallback[] = {
        {"Basilisk Stronghold", SERVER_MODE_CTF, 12, 16, 42, true, false},
        {"Aurora Station", SERVER_MODE_TDM, 24, 32, 88, false, false},
        {"Specter Woods", SERVER_MODE_SLENDER, 6, 8, 110, false, true},
        {"Forge Arena", SERVER_MODE_TDM, 10, 12, 55, true, false},
        {"Echo Relay", SERVER_MODE_CTF, 8, 12, 33, true, false},
        {"Slender Co-op EU", SERVER_MODE_SLENDER, 4, 6, 190, false, false},
        {"Midnight Ops", SERVER_MODE_TDM, 16, 24, 75, false, false},
        {"Skyline Run", SERVER_MODE_CTF, 14, 20, 60, false, false},
        {"Haunt Protocol", SERVER_MODE_SLENDER, 7, 8, 145, false, true},
        {"Iron Citadel", SERVER_MODE_TDM, 18, 24, 95, true, false},
        {"Glacier Siege", SERVER_MODE_CTF, 5, 10, 30, false, false},
        {"Echo Labs QA", SERVER_MODE_TDM, 3, 10, 15, true, true},
    };

    browser->entry_count = sizeof(kFallback) / sizeof(kFallback[0]);
    for (size_t i = 0; i < browser->entry_count; ++i) {
        ServerEntry *dst = &browser->entries[i];
        memset(dst, 0, sizeof(*dst));
        dst->mode = kFallback[i].mode;
        dst->players = kFallback[i].players;
        dst->max_players = kFallback[i].max_players;
        dst->ping_ms = kFallback[i].ping;
        dst->official = kFallback[i].official;
        dst->password = kFallback[i].password;
        strncpy(dst->master.name, kFallback[i].name, MASTER_SERVER_NAME_MAX - 1);
        dst->master.name[MASTER_SERVER_NAME_MAX - 1] = '\0';
        strncpy(dst->master.address, "127.0.0.1", MASTER_SERVER_ADDR_MAX - 1);
        dst->master.address[MASTER_SERVER_ADDR_MAX - 1] = '\0';
        dst->master.port = 26015;
        dst->master.mode = (uint8_t)dst->mode;
        dst->master.players = (uint8_t)dst->players;
        dst->master.max_players = (uint8_t)dst->max_players;
    }
}

void server_browser_init(ServerBrowserState *browser, const MasterClientConfig *config)
{
    if (!browser) {
        return;
    }

    server_browser_clear(browser);
    if (config) {
        browser->master_config = *config;
    } else {
        browser->master_config.host = APP_MASTER_DEFAULT_HOST;
        browser->master_config.port = APP_MASTER_DEFAULT_PORT;
    }

    srand((unsigned int)time(NULL));
    server_browser_fetch(browser);
    server_browser_refresh(browser);
}

void server_browser_set_master(ServerBrowserState *browser, const MasterClientConfig *config)
{
    if (!browser || !config) {
        return;
    }
    browser->master_config = *config;
    browser->needs_refresh = true;
}

bool server_browser_fetch(ServerBrowserState *browser)
{
    if (!browser) {
        return false;
    }

    MasterClientConfig config = browser->master_config;
    if (!config.host || !config.host[0]) {
        config.host = APP_MASTER_DEFAULT_HOST;
    }
    if (config.port == 0) {
        config.port = APP_MASTER_DEFAULT_PORT;
    }

    MasterServerEntry master_entries[SERVER_BROWSER_MAX_ENTRIES];
    size_t count = 0;
    bool success = network_fetch_master_list(&config, master_entries, SERVER_BROWSER_MAX_ENTRIES, &count);

    if (count > SERVER_BROWSER_MAX_ENTRIES) {
        count = SERVER_BROWSER_MAX_ENTRIES;
    }

    if (!success && count == 0) {
        server_browser_generate_fallback(browser);
        browser->needs_refresh = false;
        return false;
    }

    browser->entry_count = count;
    for (size_t i = 0; i < count; ++i) {
        MasterServerEntry src = master_entries[i];
        ServerEntry *dst = &browser->entries[i];

        src.name[MASTER_SERVER_NAME_MAX - 1] = '\0';
        src.address[MASTER_SERVER_ADDR_MAX - 1] = '\0';

        dst->master = src;
        dst->mode = server_mode_from_master(src.mode);
        dst->players = (int)src.players;
        dst->max_players = src.max_players > 0 ? (int)src.max_players : (int)src.players;
        if (dst->max_players < dst->players) {
            dst->players = dst->max_players;
        }
        dst->ping_ms = 40 + (rand() % 90);
        dst->official = false;
        dst->password = false;
    }

    for (size_t i = count; i < SERVER_BROWSER_MAX_ENTRIES; ++i) {
        memset(&browser->entries[i], 0, sizeof(ServerEntry));
    }

    browser->needs_refresh = false;
    return success;
}

bool server_browser_mode_filter_any(const ServerBrowserState *browser)
{
    if (!browser) {
        return false;
    }
    for (int i = 0; i < SERVER_MODE_COUNT; ++i) {
        if (browser->mode_filter[i]) {
            return true;
        }
    }
    return false;
}

void server_browser_refresh(ServerBrowserState *browser)
{
    if (!browser) {
        return;
    }

    bool any_filter = server_browser_mode_filter_any(browser);

    browser->visible_count = 0;
    for (size_t i = 0; i < browser->entry_count && browser->visible_count < SERVER_BROWSER_MAX_ENTRIES; ++i) {
        const ServerEntry *entry = &browser->entries[i];
        if (!any_filter || browser->mode_filter[entry->mode]) {
            browser->visible_indices[browser->visible_count++] = (int)i;
        }
    }

    g_sort_browser = browser;
    g_sort_column = browser->sort_column;
    g_sort_descending = browser->sort_descending;
    qsort(browser->visible_indices, browser->visible_count, sizeof(int), server_compare_indices);

    if (browser->selected_entry >= 0) {
        bool still_visible = false;
        for (size_t i = 0; i < browser->visible_count; ++i) {
            if (browser->visible_indices[i] == browser->selected_entry) {
                still_visible = true;
                break;
            }
        }
        if (!still_visible) {
            browser->selected_entry = browser->visible_count > 0 ? browser->visible_indices[0] : -1;
        }
    }
    if (browser->selected_entry < 0 && browser->visible_count > 0) {
        browser->selected_entry = browser->visible_indices[0];
    }
    if (browser->visible_count == 0) {
        browser->selected_entry = -1;
    }
}

void server_browser_toggle_sort(ServerBrowserState *browser, ServerSortColumn column)
{
    if (!browser) {
        return;
    }

    if (browser->sort_column == column) {
        browser->sort_descending = !browser->sort_descending;
    } else {
        browser->sort_column = column;
        browser->sort_descending = (column == SERVER_SORT_PING) || (column == SERVER_SORT_PLAYERS);
    }
    server_browser_refresh(browser);
}

const char *server_mode_name(ServerMode mode)
{
    switch (mode) {
    case SERVER_MODE_CTF:
        return "Capture the Flag";
    case SERVER_MODE_TDM:
        return "Team Deathmatch";
    case SERVER_MODE_SLENDER:
        return "Slender Hunt";
    default:
        break;
    }
    return "Unknown";
}
