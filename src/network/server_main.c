#include "engine/network_server.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
static void sleep_ms(unsigned ms){ Sleep(ms); }
#else
#  include <unistd.h>
static void sleep_ms(unsigned ms){ usleep(ms*1000); }
#endif

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

    // Parsing ultra simple des args: --port 26015 --name "xxx"
    for (int i=1; i+1<argc; ++i){
        if (strcmp(argv[i], "--port")==0) cfg.port = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--max")==0) cfg.max_clients = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--name")==0) cfg.name = argv[++i];
        else if (strcmp(argv[i], "--public")==0) cfg.public_address = argv[++i];
        else if (strcmp(argv[i], "--advertise")==0) cfg.advertise = atoi(argv[++i]) != 0;
        else if (strcmp(argv[i], "--master-host")==0) cfg.master_host = argv[++i];
        else if (strcmp(argv[i], "--master-port")==0) cfg.master_port = (uint16_t)atoi(argv[++i]);
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
