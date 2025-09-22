#include "engine/master_server.h"
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
    MasterServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port = 27050;              // par défaut
    cfg.max_servers = 128;
    cfg.heartbeat_timeout = 20.0f;
    cfg.cleanup_interval = 1.0f;

    // args: --port 27050
    for (int i=1; i+1<argc; ++i){
        if (strcmp(argv[i], "--port")==0) cfg.port = (unsigned short)atoi(argv[++i]);
    }

    MasterServer* ms = master_server_create(&cfg);
    if (!ms){
        fprintf(stderr, "master_server_create failed on port %u\n", cfg.port);
        return 1;
    }

    printf("[master] listening on %u\n", cfg.port);

    const float dt = 1.0f/60.0f;
    for (;;){
        master_server_update(ms, dt);
        sleep_ms(16);
    }

    // (jamais atteint)
    // master_server_destroy(ms);
    // return 0;
}
