#pragma once

typedef struct vec3 {
    float x;
    float y;
    float z;
} vec3;

vec3 vec3_add(vec3 a, vec3 b);
vec3 vec3_sub(vec3 a, vec3 b);
