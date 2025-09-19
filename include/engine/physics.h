#pragma once

typedef struct PhysicsWorld PhysicsWorld;

typedef struct PhysicsWorldDesc {
    float gravity_y;
} PhysicsWorldDesc;

PhysicsWorld *physics_world_create(const PhysicsWorldDesc *desc);
void physics_world_destroy(PhysicsWorld *world);
void physics_world_step(PhysicsWorld *world, float dt);
