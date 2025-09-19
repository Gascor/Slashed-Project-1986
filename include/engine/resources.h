#pragma once

#include <stddef.h>

void resources_init(const char *base_path);
void resources_shutdown(void);
const void *resources_load_file(const char *relative_path, size_t *out_size);
void resources_free_file(const void *data);
