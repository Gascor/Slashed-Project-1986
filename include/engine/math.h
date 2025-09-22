#pragma once

#include <stddef.h>

typedef struct vec3 {
    float x;
    float y;
    float z;
} vec3;

typedef struct vec4 {
    float x;
    float y;
    float z;
    float w;
} vec4;

typedef struct mat4 {
    float m[16]; /* column-major */
} mat4;

vec3 vec3_make(float x, float y, float z);
vec3 vec3_add(vec3 a, vec3 b);
vec3 vec3_sub(vec3 a, vec3 b);
vec3 vec3_scale(vec3 v, float s);
float vec3_dot(vec3 a, vec3 b);
vec3 vec3_cross(vec3 a, vec3 b);
float vec3_length(vec3 v);
vec3 vec3_normalize(vec3 v);

mat4 mat4_identity(void);
mat4 mat4_multiply(mat4 a, mat4 b);
mat4 mat4_translate(vec3 t);
mat4 mat4_scale(vec3 s);
mat4 mat4_rotate_y(float radians);
mat4 mat4_perspective(float fovy_radians, float aspect, float z_near, float z_far);
mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up);

void mat4_to_float_array(const mat4 *m, float out[16]);
