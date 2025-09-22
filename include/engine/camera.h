#pragma once

#include "engine/math.h"

#define CAMERA_DEFAULT_FOV_DEG 70.0f
#define CAMERA_DEFAULT_NEAR 0.1f
#define CAMERA_DEFAULT_FAR 1000.0f

typedef struct Camera {
    vec3 position;
    float yaw;   /* radians */
    float pitch; /* radians */
    float fov_y; /* radians */
    float aspect;
    float near_plane;
    float far_plane;
    float min_pitch;
    float max_pitch;
} Camera;

Camera camera_create(vec3 position,
                     float yaw,
                     float pitch,
                     float fov_y_radians,
                     float aspect,
                     float near_plane,
                     float far_plane);

void camera_set_aspect(Camera *camera, float aspect);
void camera_add_yaw(Camera *camera, float delta_radians);
void camera_add_pitch(Camera *camera, float delta_radians);
void camera_set_pitch_limits(Camera *camera, float min_pitch, float max_pitch);

vec3 camera_forward(const Camera *camera);
vec3 camera_right(const Camera *camera);
vec3 camera_up(const Camera *camera);

mat4 camera_view_matrix(const Camera *camera);
mat4 camera_projection_matrix(const Camera *camera);
mat4 camera_view_projection_matrix(const Camera *camera);
