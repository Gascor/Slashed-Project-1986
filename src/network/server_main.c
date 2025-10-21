#include "engine/network_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef _WIN32
#  include <windows.h>
static void sleep_ms(unsigned ms){ Sleep(ms); }
#else
#  include <unistd.h>
static void sleep_ms(unsigned ms){ usleep(ms*1000); }
#endif

#define SERVER_DEFAULT_VOICE_RANGE 22.0f

static void server_trim(char *str)
{
    if (!str) {
        return;
    }
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        --len;
    }
}

static int server_iequal(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void server_load_config(NetworkServerConfig *cfg)
{
    if (!cfg) {
        return;
    }
    FILE *fp = fopen("config/server.cfg", "r");
    if (!fp) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        server_trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        server_trim(key);
        server_trim(value);

        if (server_iequal(key, "voice_mode")) {
            if (server_iequal(value, "global")) {
                cfg->voice_mode = NETWORK_VOICE_CHAT_GLOBAL;
            } else if (server_iequal(value, "proximity")) {
                cfg->voice_mode = NETWORK_VOICE_CHAT_PROXIMITY;
            }
        } else if (server_iequal(key, "voice_range")) {
            float parsed = (float)strtod(value, NULL);
            if (parsed > 0.0f) {
                cfg->voice_range = parsed;
            }
        }
    }

    fclose(fp);
}

int main(int argc, char** argv)
{
    NetworkServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    // Valeurs par défaut
    cfg.port = 26015;
    cfg.max_clients = 8;
    cfg.name = "Slashed Project 1986 Server";
    cfg.public_address = "127.0.0.1";

    // Publicité sur master (désactivée tant que tu n’as pas de master)
    cfg.advertise = 0;                 // mets 1 quand le master sera prêt
    cfg.master_host = "127.0.0.1";
    cfg.master_port = 27050;
    cfg.master_heartbeat_interval = 5.0f;
    cfg.advertised_mode = 1;
    cfg.voice_mode = NETWORK_VOICE_CHAT_PROXIMITY;
    cfg.voice_range = SERVER_DEFAULT_VOICE_RANGE;

    server_load_config(&cfg);

    // Parsing ultra simple des args: --port 26015 --name "xxx"
    for (int i=1; i+1<argc; ++i){
        if (strcmp(argv[i], "--port")==0) cfg.port = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--max")==0) cfg.max_clients = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--name")==0) cfg.name = argv[++i];
        else if (strcmp(argv[i], "--public")==0) cfg.public_address = argv[++i];
        else if (strcmp(argv[i], "--advertise")==0) cfg.advertise = atoi(argv[++i]) != 0;
        else if (strcmp(argv[i], "--master-host")==0) cfg.master_host = argv[++i];
        else if (strcmp(argv[i], "--master-port")==0) cfg.master_port = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--voice-mode")==0) {
            const char *mode = argv[++i];
            if (server_iequal(mode, "global")) {
                cfg.voice_mode = NETWORK_VOICE_CHAT_GLOBAL;
            } else if (server_iequal(mode, "proximity")) {
                cfg.voice_mode = NETWORK_VOICE_CHAT_PROXIMITY;
            }
        }
        else if (strcmp(argv[i], "--voice-range")==0) cfg.voice_range = (float)atof(argv[++i]);
    }

    NetworkServer* server = network_server_create(&cfg);
    if (!server){
        fprintf(stderr, "Failed to start server on port %u\n", cfg.port);
        return 1;
    }

    printf("Server started on %u. Press Ctrl+C to quit.\n", cfg.port);

    // Boucle principale ~60 Hz
    const float dt = 1.0f/60.0f;
    for (;;){
        network_server_update(server, dt);
        sleep_ms(16);
    }

    // (jamais atteint)
    // network_server_destroy(server);
    // return 0;
}
