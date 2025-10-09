#include "engine/server_browser.h"

#include "engine/network.h"

#include <stdio.h>
#include <string.h>

static void server_browser_update_status(ServerBrowserState *browser,
                                         bool success,
                                         size_t count)
{
    if (!browser) {
        return;
    }

    if (success) {
        if (count > 0) {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "Found %zu server%s.",
                     count,
                     (count == 1) ? "" : "s");
        } else {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "No servers currently available.");
        }
    } else {
        if (count > 0) {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "Master unreachable; showing fallback list (%zu).",
                     count);
        } else {
            snprintf(browser->status,
                     sizeof(browser->status),
                     "Failed to contact master server.");
        }
    }
}

void server_browser_init(ServerBrowserState *browser)
{
    if (!browser) {
        return;
    }

    memset(browser, 0, sizeof(*browser));
    browser->selection = 0;
    browser->open = false;
    browser->last_request_success = false;
    browser->status[0] = '\0';
    browser->last_refresh_time = 0.0;
}

bool server_browser_open(ServerBrowserState *browser,
                         const MasterClientConfig *config,
                         double time_seconds)
{
    if (!browser) {
        return false;
    }

    browser->selection = 0;
    browser->open = true;
    return server_browser_refresh(browser, config, time_seconds);
}

void server_browser_close(ServerBrowserState *browser)
{
    if (browser) {
        browser->open = false;
    }
}

bool server_browser_refresh(ServerBrowserState *browser,
                            const MasterClientConfig *config,
                            double time_seconds)
{
    if (!browser || !config) {
        return false;
    }

    size_t count = 0;
    bool success = network_fetch_master_list(config,
                                             browser->entries,
                                             GAME_MAX_SERVER_LIST,
                                             &count);
    if (count > GAME_MAX_SERVER_LIST) {
        count = GAME_MAX_SERVER_LIST;
    }

    browser->entry_count = count;
    if (count == 0) {
        browser->selection = 0;
    } else if (browser->selection >= (int)count) {
        browser->selection = (int)count - 1;
    } else if (browser->selection < 0) {
        browser->selection = 0;
    }
    browser->last_request_success = success;
    browser->last_refresh_time = time_seconds;

    server_browser_update_status(browser, success, count);
    return success;
}

void server_browser_move_selection(ServerBrowserState *browser, int delta)
{
    if (!browser || browser->entry_count == 0) {
        return;
    }

    int count = (int)browser->entry_count;
    int selection = browser->selection + delta;
    while (selection < 0) {
        selection += count;
    }
    selection %= count;
    browser->selection = selection;
}

void server_browser_set_selection(ServerBrowserState *browser, int selection)
{
    if (!browser || browser->entry_count == 0) {
        browser->selection = 0;
        return;
    }

    if (selection < 0) {
        selection = 0;
    }
    if (selection >= (int)browser->entry_count) {
        selection = (int)browser->entry_count - 1;
    }
    browser->selection = selection;
}

bool server_browser_has_entries(const ServerBrowserState *browser)
{
    return browser && browser->entry_count > 0;
}

const MasterServerEntry *server_browser_selected(const ServerBrowserState *browser)
{
    if (!browser || browser->entry_count == 0) {
        return NULL;
    }

    int index = browser->selection;
    if (index < 0 || index >= (int)browser->entry_count) {
        return NULL;
    }
    return &browser->entries[index];
}

