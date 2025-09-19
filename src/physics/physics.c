#include "engine/physics.h"

#include <stdlib.h>

struct PhysicsWorld {
    PhysicsWorldDesc desc;
};

PhysicsWorld *physics_world_create(const PhysicsWorldDesc *desc)
{
    if (!desc) {
        return NULL;
    }

    PhysicsWorld *world = (PhysicsWorld *)malloc(sizeof(PhysicsWorld));
    if (!world) {
        return NULL;
    }

    world->desc = *desc;
    return world;
}

void physics_world_destroy(PhysicsWorld *world)
{
    free(world);
}

void physics_world_step(PhysicsWorld *world, float dt)
{
    (void)world;
    (void)dt;
}
