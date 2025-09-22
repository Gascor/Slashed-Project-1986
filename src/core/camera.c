#include "engine/camera.h"

#include <math.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#    define M_PI_2 (M_PI / 2.0)
#endif

static void camera_clamp_pitch(Camera *camera)
{
    if (!camera) {
        return;
    }

    if (camera->pitch > camera->max_pitch) {
        camera->pitch = camera->max_pitch;
    } else if (camera->pitch < camera->min_pitch) {
        camera->pitch = camera->min_pitch;
    }
}

Camera camera_create(vec3 position,
                     float yaw,
                     float pitch,
                     float fov_y_radians,
                     float aspect,
                     float near_plane,
                     float far_plane)
{
    Camera camera;
    camera.position = position;
    camera.yaw = yaw;
    camera.pitch = pitch;
    camera.fov_y = fov_y_radians;
    camera.aspect = aspect;
    camera.near_plane = near_plane;
    camera.far_plane = far_plane;
    camera.min_pitch = -(float)M_PI_2 + 0.01f;
    camera.max_pitch = (float)M_PI_2 - 0.01f;
    camera_clamp_pitch(&camera);
    return camera;
}

void camera_set_aspect(Camera *camera, float aspect)
{
    if (!camera) {
        return;
    }
    camera->aspect = aspect;
}

void camera_add_yaw(Camera *camera, float delta_radians)
{
    if (!camera) {
        return;
    }
    camera->yaw += delta_radians;
}

void camera_add_pitch(Camera *camera, float delta_radians)
{
    if (!camera) {
        return;
    }
    camera->pitch += delta_radians;
    camera_clamp_pitch(camera);
}

void camera_set_pitch_limits(Camera *camera, float min_pitch, float max_pitch)
{
    if (!camera) {
        return;
    }
    camera->min_pitch = min_pitch;
    camera->max_pitch = max_pitch;
    camera_clamp_pitch(camera);
}

static vec3 camera_direction_from_angles(float yaw, float pitch)
{
    float cos_pitch = cosf(pitch);
    vec3 forward = vec3_make(cos_pitch * cosf(yaw), sinf(pitch), cos_pitch * sinf(yaw));
    return vec3_normalize(forward);
}

vec3 camera_forward(const Camera *camera)
{
    if (!camera) {
        return vec3_make(0.0f, 0.0f, -1.0f);
    }
    return camera_direction_from_angles(camera->yaw, camera->pitch);
}

vec3 camera_right(const Camera *camera)
{
    if (!camera) {
        return vec3_make(1.0f, 0.0f, 0.0f);
    }
    vec3 forward = camera_forward(camera);
    vec3 world_up = vec3_make(0.0f, 1.0f, 0.0f);
    return vec3_normalize(vec3_cross(forward, world_up));
}

vec3 camera_up(const Camera *camera)
{
    if (!camera) {
        return vec3_make(0.0f, 1.0f, 0.0f);
    }
    vec3 right = camera_right(camera);
    vec3 forward = camera_forward(camera);
    return vec3_normalize(vec3_cross(right, forward));
}

mat4 camera_view_matrix(const Camera *camera)
{
    if (!camera) {
        return mat4_identity();
    }
    vec3 forward = camera_forward(camera);
    vec3 target = vec3_add(camera->position, forward);
    vec3 up = camera_up(camera);
    return mat4_look_at(camera->position, target, up);
}

mat4 camera_projection_matrix(const Camera *camera)
{
    if (!camera) {
        return mat4_identity();
    }
    return mat4_perspective(camera->fov_y, camera->aspect, camera->near_plane, camera->far_plane);
}

mat4 camera_view_projection_matrix(const Camera *camera)
{
    mat4 proj = camera_projection_matrix(camera);
    mat4 view = camera_view_matrix(camera);
    return mat4_multiply(proj, view);
}
