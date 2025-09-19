#pragma once

typedef unsigned int EntityId;

void ecs_init(void);
void ecs_shutdown(void);
EntityId ecs_create_entity(void);
void ecs_destroy_entity(EntityId id);
