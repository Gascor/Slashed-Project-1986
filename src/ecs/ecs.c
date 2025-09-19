#include "engine/ecs.h"

static EntityId next_id = 1;

void ecs_init(void)
{
    next_id = 1;
}

void ecs_shutdown(void)
{
}

EntityId ecs_create_entity(void)
{
    return next_id++;
}

void ecs_destroy_entity(EntityId id)
{
    (void)id;
}
