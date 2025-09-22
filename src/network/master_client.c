#include "engine/network_master.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
typedef SOCKET master_socket_t;
#else
#    include <arpa/inet.h>
#    include <errno.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/time.h>
#    include <sys/types.h>
#    include <unistd.h>
typedef int master_socket_t;
#    define INVALID_SOCKET (-1)
#    define SOCKET_ERROR (-1)
#endif

#ifndef MASTER_CLIENT_MAX_PACKET
#    define MASTER_CLIENT_MAX_PACKET 65536u
#endif

#define MASTER_CLIENT_DEFAULT_TIMEOUT_MS 1500u

struct MasterClient {
    MasterClientConfig config;
    master_socket_t socket;
    struct sockaddr_in master_addr;
    uint32_t timeout_ms;
    int owns_global_ref;
};

static const MasterServerEntry kFallbackServers[] = {
    {"Basilisk Stronghold", "127.0.0.1", 26015, 0, 12, 16},
    {"Aurora Station", "192.168.0.42", 26015, 1, 24, 32},
    {"Specter Woods", "203.0.113.12", 26015, 2, 6, 8},
    {"Forge Arena", "198.51.100.5", 26015, 1, 10, 12},
};

static int g_master_client_refs = 0;

static void master_client_close_socket(master_socket_t socket)
{
    if (socket == INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

static int master_client_resolve_ipv4(const char *host, uint16_t port, struct sockaddr_in *out)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);

    if (!host || host[0] == '\0') {
        out->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return 0;
    }

#if defined(_WIN32)
    if (InetPtonA(AF_INET, host, &out->sin_addr) == 1) {
        return 0;
    }
#else
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) {
        return 0;
    }
#endif

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *info = NULL;
    int result = getaddrinfo(host, NULL, &hints, &info);
    if (result != 0 || !info) {
        if (info) {
            freeaddrinfo(info);
        }
        return -1;
    }

    struct sockaddr_in *ipv4 = (struct sockaddr_in *)info->ai_addr;
    out->sin_addr = ipv4->sin_addr;
    freeaddrinfo(info);
    return 0;
}

static void master_client_apply_timeout(MasterClient *client)
{
    if (!client || client->socket == INVALID_SOCKET) {
        return;
    }

    uint32_t timeout_ms = client->timeout_ms ? client->timeout_ms : MASTER_CLIENT_DEFAULT_TIMEOUT_MS;
#if defined(_WIN32)
    DWORD value = timeout_ms;
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&value, sizeof(value));
    setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&value, sizeof(value));
#else
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static size_t master_client_copy_fallback(MasterServerEntry *out_entries, size_t max_entries)
{
    size_t count = sizeof(kFallbackServers) / sizeof(kFallbackServers[0]);
    if (out_entries && max_entries > 0) {
        size_t to_copy = count < max_entries ? count : max_entries;
        memcpy(out_entries, kFallbackServers, to_copy * sizeof(MasterServerEntry));
    }
    return count;
}

bool master_client_global_init(void)
{
#if defined(_WIN32)
    if (g_master_client_refs == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
    }
#endif
    ++g_master_client_refs;
    return true;
}

void master_client_global_shutdown(void)
{
    if (g_master_client_refs <= 0) {
        return;
    }
    --g_master_client_refs;
#if defined(_WIN32)
    if (g_master_client_refs == 0) {
        WSACleanup();
    }
#endif
}

MasterClient *master_client_create(const MasterClientConfig *config)
{
    int owns_ref = 0;
    if (g_master_client_refs <= 0) {
        if (!master_client_global_init()) {
            return NULL;
        }
        owns_ref = 1;
    }

    MasterClient *client = (MasterClient *)calloc(1, sizeof(MasterClient));
    if (!client) {
        if (owns_ref) {
            master_client_global_shutdown();
        }
        return NULL;
    }

    client->owns_global_ref = owns_ref;
    client->socket = INVALID_SOCKET;
    client->timeout_ms = MASTER_CLIENT_DEFAULT_TIMEOUT_MS;

    if (config) {
        client->config = *config;
    } else {
        client->config.host = "127.0.0.1";
        client->config.port = 27050;
    }

    if (!client->config.host || client->config.host[0] == '\0') {
        client->config.host = "127.0.0.1";
    }
    if (client->config.port == 0) {
        client->config.port = 27050;
    }

    client->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client->socket == INVALID_SOCKET) {
        if (owns_ref) {
            master_client_global_shutdown();
        }
        free(client);
        return NULL;
    }

    if (master_client_resolve_ipv4(client->config.host, client->config.port, &client->master_addr) != 0) {
        master_client_close_socket(client->socket);
        if (owns_ref) {
            master_client_global_shutdown();
        }
        free(client);
        return NULL;
    }

    master_client_apply_timeout(client);
    return client;
}

void master_client_destroy(MasterClient *client)
{
    if (!client) {
        return;
    }

    if (client->socket != INVALID_SOCKET) {
        master_client_close_socket(client->socket);
        client->socket = INVALID_SOCKET;
    }

    if (client->owns_global_ref) {
        master_client_global_shutdown();
    }

    free(client);
}

void master_client_set_timeout(MasterClient *client, uint32_t timeout_ms)
{
    if (!client) {
        return;
    }
    if (timeout_ms == 0) {
        timeout_ms = MASTER_CLIENT_DEFAULT_TIMEOUT_MS;
    }
    client->timeout_ms = timeout_ms;
    master_client_apply_timeout(client);
}

bool master_client_request_list(MasterClient *client,
                                MasterServerEntry *out_entries,
                                size_t max_entries,
                                size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }

    if (!client || client->socket == INVALID_SOCKET) {
        size_t count = master_client_copy_fallback(out_entries, max_entries);
        if (out_count) {
            *out_count = count;
        }
        return false;
    }

    MasterListRequest request;
    request.type = MASTER_MSG_LIST_REQUEST;

#if defined(_WIN32)
    int sent = sendto(client->socket,
                      (const char *)&request,
                      (int)sizeof(request),
                      0,
                      (const struct sockaddr *)&client->master_addr,
                      sizeof(client->master_addr));
    if (sent == SOCKET_ERROR) {
        goto fallback;
    }
#else
    ssize_t sent = sendto(client->socket,
                          &request,
                          sizeof(request),
                          0,
                          (const struct sockaddr *)&client->master_addr,
                          sizeof(client->master_addr));
    if (sent < 0) {
        goto fallback;
    }
#endif

    uint8_t buffer[MASTER_CLIENT_MAX_PACKET];
    struct sockaddr_in from;
    socklen_t from_len = (socklen_t)sizeof(from);

#if defined(_WIN32)
    int received = recvfrom(client->socket,
                            (char *)buffer,
                            (int)sizeof(buffer),
                            0,
                            (struct sockaddr *)&from,
                            &from_len);
    if (received == SOCKET_ERROR || received == 0) {
        goto fallback;
    }
    size_t payload_size = (size_t)received;
#else
    ssize_t received = recvfrom(client->socket,
                                buffer,
                                sizeof(buffer),
                                0,
                                (struct sockaddr *)&from,
                                &from_len);
    if (received <= 0) {
        goto fallback;
    }
    size_t payload_size = (size_t)received;
#endif

    if (payload_size < sizeof(MasterListResponseHeader)) {
        goto fallback;
    }

    MasterListResponseHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (header.type != MASTER_MSG_LIST_RESPONSE) {
        goto fallback;
    }

    size_t total_entries = header.count;
    size_t expected = sizeof(MasterListResponseHeader) + total_entries * sizeof(MasterServerEntry);
    if (payload_size < expected) {
        goto fallback;
    }

    if (out_entries && max_entries > 0) {
        size_t to_copy = total_entries;
        if (to_copy > max_entries) {
            to_copy = max_entries;
        }
        const MasterServerEntry *entries = (const MasterServerEntry *)(buffer + sizeof(MasterListResponseHeader));
        for (size_t i = 0; i < to_copy; ++i) {
            MasterServerEntry entry = entries[i];
            entry.name[MASTER_SERVER_NAME_MAX - 1] = '\0';
            entry.address[MASTER_SERVER_ADDR_MAX - 1] = '\0';
            entry.port = ntohs(entry.port);
            if (entry.players > entry.max_players) {
                entry.players = entry.max_players;
            }
            out_entries[i] = entry;
        }
    }

    if (out_count) {
        *out_count = total_entries;
    }
    return true;

fallback:
    {
        size_t count = master_client_copy_fallback(out_entries, max_entries);
        if (out_count) {
            *out_count = count;
        }
    }
    return false;
}
