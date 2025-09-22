#include "engine/master_server.h"

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
typedef SOCKET master_socket_t;
#else
#    include <arpa/inet.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
typedef int master_socket_t;
#    define INVALID_SOCKET (-1)
#    define SOCKET_ERROR (-1)
#endif

#include <stdlib.h>
#include <string.h>

#ifndef MASTER_SERVER_DEFAULT_MAX
#    define MASTER_SERVER_DEFAULT_MAX 128u
#endif

#define MASTER_SERVER_DEFAULT_TIMEOUT 20.0f
#define MASTER_SERVER_DEFAULT_CLEANUP 1.0f
#define MASTER_SERVER_MAX_PACKET 2048u

typedef struct MasterServerSlot {
    int in_use;
    MasterServerEntry entry;
    double time_since_update;
    struct sockaddr_storage remote_addr;
    socklen_t remote_len;
} MasterServerSlot;

struct MasterServer {
    MasterServerConfig config;
    MasterServerStats stats;
    master_socket_t socket;
    MasterServerSlot *slots;
    size_t max_entries;
    double cleanup_timer;
};

static int g_master_socket_refs = 0;

static int master_socket_startup(void)
{
#if defined(_WIN32)
    if (g_master_socket_refs == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return -1;
        }
    }
#endif
    ++g_master_socket_refs;
    return 0;
}

static void master_socket_shutdown(void)
{
    if (g_master_socket_refs <= 0) {
        return;
    }
    --g_master_socket_refs;
#if defined(_WIN32)
    if (g_master_socket_refs == 0) {
        WSACleanup();
    }
#endif
}

static int master_socket_set_nonblocking(master_socket_t socket)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
#endif
}

static void master_server_address_to_string(const struct sockaddr *addr, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!addr) {
        return;
    }
#if defined(_WIN32)
    if (addr->sa_family == AF_INET) {
        InetNtopA(AF_INET, &((const struct sockaddr_in *)addr)->sin_addr, out, (DWORD)out_len);
    }
#else
    if (addr->sa_family == AF_INET) {
        inet_ntop(AF_INET, &((const struct sockaddr_in *)addr)->sin_addr, out, out_len);
    }
#endif
}

static MasterServerEntry master_server_normalize_entry(const MasterServerEntry *source,
                                                       const struct sockaddr *from)
{
    MasterServerEntry entry;
    memset(&entry, 0, sizeof(entry));
    if (source) {
        entry = *source;
    }
    entry.name[MASTER_SERVER_NAME_MAX - 1] = '\0';
    entry.address[MASTER_SERVER_ADDR_MAX - 1] = '\0';
    entry.port = ntohs(entry.port);
    if (from && entry.port == 0 && from->sa_family == AF_INET) {
        entry.port = ntohs(((const struct sockaddr_in *)from)->sin_port);
    }
    if (entry.address[0] == '\0') {
        master_server_address_to_string(from, entry.address, sizeof(entry.address));
    }
    if (entry.max_players == 0) {
        entry.max_players = entry.players > 0 ? entry.players : 1;
    }
    if (entry.players > entry.max_players) {
        entry.players = entry.max_players;
    }
    return entry;
}

static MasterServerSlot *master_server_find_slot(MasterServer *server, const MasterServerEntry *entry)
{
    if (!server || !entry) {
        return NULL;
    }
    for (size_t i = 0; i < server->max_entries; ++i) {
        MasterServerSlot *slot = &server->slots[i];
        if (!slot->in_use) {
            continue;
        }
        if (slot->entry.port != entry->port) {
            continue;
        }
        if (strncmp(slot->entry.address, entry->address, MASTER_SERVER_ADDR_MAX) != 0) {
            continue;
        }
        return slot;
    }
    return NULL;
}

static MasterServerSlot *master_server_acquire_slot(MasterServer *server)
{
    if (!server) {
        return NULL;
    }
    for (size_t i = 0; i < server->max_entries; ++i) {
        MasterServerSlot *slot = &server->slots[i];
        if (!slot->in_use) {
            memset(slot, 0, sizeof(*slot));
            return slot;
        }
    }
    return NULL;
}

static void master_server_store_remote(MasterServerSlot *slot,
                                       const struct sockaddr *from,
                                       socklen_t from_len)
{
    if (!slot) {
        return;
    }
    if (from && from_len > 0) {
        if ((size_t)from_len > sizeof(slot->remote_addr)) {
            from_len = (socklen_t)sizeof(slot->remote_addr);
        }
        memcpy(&slot->remote_addr, from, (size_t)from_len);
        slot->remote_len = from_len;
    } else {
        memset(&slot->remote_addr, 0, sizeof(slot->remote_addr));
        slot->remote_len = 0;
    }
}

static void master_server_register_entry(MasterServer *server,
                                         const MasterServerEntry *entry,
                                         const struct sockaddr *from,
                                         socklen_t from_len,
                                         int count_stat)
{
    if (!server || !entry) {
        return;
    }

    MasterServerSlot *slot = master_server_find_slot(server, entry);
    if (!slot) {
        slot = master_server_acquire_slot(server);
        if (!slot) {
            server->stats.dropped_servers += 1;
            return;
        }
        slot->in_use = 1;
        server->stats.active_servers += 1;
    }

    slot->entry = *entry;
    slot->time_since_update = 0.0;
    master_server_store_remote(slot, from, from_len);
    if (count_stat) {
        server->stats.register_messages += 1;
    }
}

static int master_server_update_entry(MasterServer *server,
                                      const MasterServerEntry *entry,
                                      const struct sockaddr *from,
                                      socklen_t from_len)
{
    if (!server || !entry) {
        return 0;
    }
    MasterServerSlot *slot = master_server_find_slot(server, entry);
    if (!slot) {
        return 0;
    }
    slot->entry = *entry;
    slot->time_since_update = 0.0;
    master_server_store_remote(slot, from, from_len);
    return 1;
}

static void master_server_remove_entry(MasterServer *server, const MasterServerEntry *entry)
{
    if (!server || !entry) {
        return;
    }
    MasterServerSlot *slot = master_server_find_slot(server, entry);
    if (!slot) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
    if (server->stats.active_servers > 0) {
        server->stats.active_servers -= 1;
    }
}

static void master_server_send_list(MasterServer *server,
                                    const struct sockaddr *to,
                                    socklen_t to_len)
{
    if (!server || !to) {
        return;
    }

    size_t active = 0;
    for (size_t i = 0; i < server->max_entries; ++i) {
        if (server->slots[i].in_use) {
            ++active;
        }
    }

    if (active > 255) {
        active = 255;
    }

    size_t payload_size = sizeof(MasterListResponseHeader) + active * sizeof(MasterServerEntry);
    uint8_t *payload = (uint8_t *)malloc(payload_size);
    if (!payload) {
        return;
    }

    MasterListResponseHeader *header = (MasterListResponseHeader *)payload;
    header->type = MASTER_MSG_LIST_RESPONSE;
    header->count = (uint8_t)active;

    MasterServerEntry *entries = (MasterServerEntry *)(payload + sizeof(MasterListResponseHeader));
    size_t written = 0;
    for (size_t i = 0; i < server->max_entries && written < active; ++i) {
        if (!server->slots[i].in_use) {
            continue;
        }
        MasterServerEntry copy = server->slots[i].entry;
        copy.port = htons(copy.port);
        entries[written++] = copy;
    }

#if defined(_WIN32)
    sendto(server->socket,
           (const char *)payload,
           (int)payload_size,
           0,
           to,
           to_len);
#else
    sendto(server->socket,
           payload,
           payload_size,
           0,
           to,
           to_len);
#endif

    free(payload);
}

static void master_server_process_packet(MasterServer *server,
                                         const uint8_t *data,
                                         size_t size,
                                         const struct sockaddr *from,
                                         socklen_t from_len)
{
    if (!server || !data || size == 0) {
        return;
    }

    uint8_t type = data[0];
    switch (type) {
    case MASTER_MSG_REGISTER:
        if (size >= sizeof(MasterRegisterMessage)) {
            MasterRegisterMessage message;
            memcpy(&message, data, sizeof(message));
            MasterServerEntry entry = master_server_normalize_entry(&message.entry, from);
            master_server_register_entry(server, &entry, from, from_len, 1);
        }
        break;
    case MASTER_MSG_HEARTBEAT:
        if (size >= sizeof(MasterHeartbeatMessage)) {
            MasterHeartbeatMessage message;
            memcpy(&message, data, sizeof(message));
            MasterServerEntry entry = master_server_normalize_entry(&message.entry, from);
            if (!master_server_update_entry(server, &entry, from, from_len)) {
                master_server_register_entry(server, &entry, from, from_len, 0);
            }
            server->stats.heartbeat_messages += 1;
        }
        break;
    case MASTER_MSG_UNREGISTER:
        if (size >= sizeof(MasterRegisterMessage)) {
            MasterRegisterMessage message;
            memcpy(&message, data, sizeof(message));
            MasterServerEntry entry = master_server_normalize_entry(&message.entry, from);
            master_server_remove_entry(server, &entry);
            server->stats.unregister_messages += 1;
        }
        break;
    case MASTER_MSG_LIST_REQUEST:
        server->stats.list_requests += 1;
        master_server_send_list(server, from, from_len);
        break;
    default:
        break;
    }
}

static void master_server_drain_socket(MasterServer *server)
{
    if (!server || server->socket == INVALID_SOCKET) {
        return;
    }

    uint8_t buffer[MASTER_SERVER_MAX_PACKET];
    struct sockaddr_storage from;
    for (;;) {
        socklen_t from_len = (socklen_t)sizeof(from);
#if defined(_WIN32)
        int received = recvfrom(server->socket,
                                (char *)buffer,
                                (int)sizeof(buffer),
                                0,
                                (struct sockaddr *)&from,
                                &from_len);
        if (received == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                break;
            }
            if (err == WSAEINTR) {
                continue;
            }
            break;
        }
        if (received <= 0) {
            break;
        }
        master_server_process_packet(server, buffer, (size_t)received, (struct sockaddr *)&from, from_len);
#else
        ssize_t received = recvfrom(server->socket,
                                    buffer,
                                    sizeof(buffer),
                                    0,
                                    (struct sockaddr *)&from,
                                    &from_len);
        if (received < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (received == 0) {
            break;
        }
        master_server_process_packet(server, buffer, (size_t)received, (struct sockaddr *)&from, from_len);
#endif
    }
}

MasterServer *master_server_create(const MasterServerConfig *config)
{
    if (master_socket_startup() != 0) {
        return NULL;
    }

    MasterServer *server = (MasterServer *)calloc(1, sizeof(MasterServer));
    if (!server) {
        master_socket_shutdown();
        return NULL;
    }
    server->socket = INVALID_SOCKET;

    MasterServerConfig cfg;
    if (config) {
        cfg = *config;
    } else {
        memset(&cfg, 0, sizeof(cfg));
    }

    if (cfg.max_servers == 0) {
        cfg.max_servers = MASTER_SERVER_DEFAULT_MAX;
    }
    if (cfg.heartbeat_timeout <= 0.0f) {
        cfg.heartbeat_timeout = MASTER_SERVER_DEFAULT_TIMEOUT;
    }
    if (cfg.cleanup_interval <= 0.0f) {
        cfg.cleanup_interval = MASTER_SERVER_DEFAULT_CLEANUP;
    }

    server->config = cfg;
    server->stats.max_servers = cfg.max_servers;
    server->max_entries = cfg.max_servers;
    server->cleanup_timer = 0.0;

    server->slots = (MasterServerSlot *)calloc(server->max_entries, sizeof(MasterServerSlot));
    if (!server->slots) {
        master_server_destroy(server);
        return NULL;
    }

    server->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server->socket == INVALID_SOCKET) {
        master_server_destroy(server);
        return NULL;
    }

    int reuse = 1;
    setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    if (master_socket_set_nonblocking(server->socket) != 0) {
        master_server_destroy(server);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg.port);

    if (bind(server->socket, (const struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        master_server_destroy(server);
        return NULL;
    }

    return server;
}

void master_server_destroy(MasterServer *server)
{
    if (!server) {
        return;
    }

    if (server->socket != INVALID_SOCKET) {
#if defined(_WIN32)
        closesocket(server->socket);
#else
        close(server->socket);
#endif
        server->socket = INVALID_SOCKET;
    }

    free(server->slots);
    free(server);
    master_socket_shutdown();
}

void master_server_update(MasterServer *server, float dt)
{
    if (!server) {
        return;
    }

    master_server_drain_socket(server);

    server->stats.uptime_seconds += dt;
    server->cleanup_timer += (double)dt;
    if (server->cleanup_timer < server->config.cleanup_interval) {
        return;
    }

    double elapsed = server->cleanup_timer;
    server->cleanup_timer = 0.0;

    for (size_t i = 0; i < server->max_entries; ++i) {
        MasterServerSlot *slot = &server->slots[i];
        if (!slot->in_use) {
            continue;
        }
        slot->time_since_update += elapsed;
        if (slot->time_since_update >= server->config.heartbeat_timeout) {
            memset(slot, 0, sizeof(*slot));
            if (server->stats.active_servers > 0) {
                server->stats.active_servers -= 1;
            }
            server->stats.dropped_servers += 1;
        }
    }
}

size_t master_server_entries(const MasterServer *server, MasterServerEntry *out_entries, size_t max_entries)
{
    if (!server) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < server->max_entries; ++i) {
        const MasterServerSlot *slot = &server->slots[i];
        if (!slot->in_use) {
            continue;
        }
        if (out_entries && count < max_entries) {
            out_entries[count] = slot->entry;
        }
        ++count;
    }
    return count;
}

const MasterServerStats *master_server_stats(const MasterServer *server)
{
    if (!server) {
        return NULL;
    }
    return &server->stats;
}
