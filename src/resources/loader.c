#include "engine/resources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char base_directory[260];

void resources_init(const char *base_path)
{
    if (!base_path) {
        base_directory[0] = '\0';
        return;
    }

    size_t len = strlen(base_path);
    if (len >= sizeof(base_directory)) {
        len = sizeof(base_directory) - 1;
    }

    memcpy(base_directory, base_path, len);
    base_directory[len] = '\0';
}

void resources_shutdown(void)
{
}

const void *resources_load_file(const char *relative_path, size_t *out_size)
{
    (void)relative_path;
    if (out_size) {
        *out_size = 0;
    }
    return NULL;
}

void resources_free_file(const void *data)
{
    (void)data;
}
