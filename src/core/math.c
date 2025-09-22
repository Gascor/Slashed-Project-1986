#include "engine/math.h"

#include <math.h>

vec3 vec3_make(float x, float y, float z)
{
    vec3 v = {x, y, z};
    return v;
}

vec3 vec3_add(vec3 a, vec3 b)
{
    return vec3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

vec3 vec3_sub(vec3 a, vec3 b)
{
    return vec3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

vec3 vec3_scale(vec3 v, float s)
{
    return vec3_make(v.x * s, v.y * s, v.z * s);
}

float vec3_dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_cross(vec3 a, vec3 b)
{
    return vec3_make(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

float vec3_length(vec3 v)
{
    return sqrtf(vec3_dot(v, v));
}

vec3 vec3_normalize(vec3 v)
{
    float len = vec3_length(v);
    if (len <= 0.000001f) {
        return vec3_make(0.0f, 0.0f, 0.0f);
    }
    float inv_len = 1.0f / len;
    return vec3_scale(v, inv_len);
}

static mat4 mat4_make(const float values[16])
{
    mat4 result;
    for (int i = 0; i < 16; ++i) {
        result.m[i] = values[i];
    }
    return result;
}

mat4 mat4_identity(void)
{
    const float values[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return mat4_make(values);
}

mat4 mat4_multiply(mat4 a, mat4 b)
{
    mat4 result = {0};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }
    return result;
}

mat4 mat4_translate(vec3 t)
{
    mat4 m = mat4_identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

mat4 mat4_scale(vec3 s)
{
    mat4 m = mat4_identity();
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    return m;
}

mat4 mat4_rotate_y(float radians)
{
    float c = cosf(radians);
    float s = sinf(radians);

    const float values[16] = {
        c,   0.0f, -s,  0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        s,   0.0f, c,   0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    return mat4_make(values);
}

mat4 mat4_perspective(float fovy_radians, float aspect, float z_near, float z_far)
{
    float f = 1.0f / tanf(fovy_radians * 0.5f);
    mat4 m = {0};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (z_far + z_near) / (z_near - z_far);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * z_far * z_near) / (z_near - z_far);
    return m;
}

mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up)
{
    vec3 f = vec3_normalize(vec3_sub(target, eye));
    vec3 s = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);

    mat4 m = mat4_identity();
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;

    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;

    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;

    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);

    return m;
}

void mat4_to_float_array(const mat4 *m, float out[16])
{
    if (!m || !out) {
        return;
    }

    for (int i = 0; i < 16; ++i) {
        out[i] = m->m[i];
    }
}
