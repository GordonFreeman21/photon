#include "../include/camera.h"
#include "../include/math_util.h"
#include <math.h>

RTCamera camera_create(vec3 position, vec3 target, float fov_y, float aspect)
{
    RTCamera cam = {0};

    cam.position = position;
    cam.fov_y = fov_y;
    cam.aspect_ratio = aspect;
    cam.near_plane = 0.1f;
    cam.far_plane = 1000.0f;
    cam.jitter_x = 0.0f;
    cam.jitter_y = 0.0f;

    cam.forward = vec3_normalize(vec3_sub(target, position));

    vec3 world_up = vec3_new(0.0f, 1.0f, 0.0f);
    cam.right = vec3_normalize(vec3_cross(cam.forward, world_up));
    cam.up = vec3_normalize(vec3_cross(cam.right, cam.forward));

    camera_update(&cam);

    cam.prev_view_projection = cam.view_projection;

    return cam;
}

void camera_update(RTCamera *cam)
{
    if (cam == NULL) return;

    /* Compute look-at target from position + forward */
    vec3 target = vec3_add(cam->position, cam->forward);

    cam->view = mat4_look_at(cam->position, target, cam->up);

    cam->projection = mat4_perspective(
        cam->fov_y,
        cam->aspect_ratio,
        cam->near_plane,
        cam->far_plane
    );

    /* Apply sub-pixel jitter for TAA */
    cam->projection.m[8]  += cam->jitter_x;
    cam->projection.m[9]  += cam->jitter_y;

    cam->view_projection = mat4_multiply(cam->projection, cam->view);
    cam->inv_view_projection = mat4_inverse(cam->view_projection);
}

void camera_set_position(RTCamera *cam, vec3 position)
{
    if (cam == NULL) return;
    cam->position = position;
    camera_update(cam);
}

void camera_set_target(RTCamera *cam, vec3 target)
{
    if (cam == NULL) return;

    /* Recompute basis vectors from new target */
    cam->forward = vec3_normalize(vec3_sub(target, cam->position));

    vec3 world_up = vec3_new(0.0f, 1.0f, 0.0f);
    cam->right = vec3_normalize(vec3_cross(cam->forward, world_up));
    cam->up = vec3_normalize(vec3_cross(cam->right, cam->forward));

    camera_update(cam);
}

void camera_set_fov(RTCamera *cam, float fov_y)
{
    if (cam == NULL) return;
    cam->fov_y = fov_y;
    camera_update(cam);
}

void camera_set_aspect(RTCamera *cam, float aspect)
{
    if (cam == NULL) return;
    cam->aspect_ratio = aspect;
    camera_update(cam);
}

void camera_set_jitter(RTCamera *cam, float jx, float jy)
{
    if (cam == NULL) return;
    cam->jitter_x = jx;
    cam->jitter_y = jy;
    camera_update(cam);
}

void camera_compute_taa_jitter(RTCamera *cam, uint32_t frame_index)
{
    if (cam == NULL) return;

    float width = 1920.0f;
    float height = 1080.0f;

    vec2 halton = halton_2d(frame_index);

    float jx = halton.x / width;
    float jy = halton.y / height;

    camera_set_jitter(cam, jx, jy);
}

void camera_save_previous(RTCamera *cam)
{
    if (cam == NULL) return;
    cam->prev_view_projection = cam->view_projection;
}

Frustum frustum_extract(const mat4 *view_proj)
{
    Frustum frustum = {0};
    const float *m = view_proj->m;

    /* Left plane: row3 + row0 */
    frustum.planes[0].x = m[12] + m[0];
    frustum.planes[0].y = m[13] + m[1];
    frustum.planes[0].z = m[14] + m[2];
    frustum.planes[0].w = m[15] + m[3];

    /* Right plane: row3 - row0 */
    frustum.planes[1].x = m[12] - m[0];
    frustum.planes[1].y = m[13] - m[1];
    frustum.planes[1].z = m[14] - m[2];
    frustum.planes[1].w = m[15] - m[3];

    /* Bottom plane: row3 + row1 */
    frustum.planes[2].x = m[12] + m[4];
    frustum.planes[2].y = m[13] + m[5];
    frustum.planes[2].z = m[14] + m[6];
    frustum.planes[2].w = m[15] + m[7];

    /* Top plane: row3 - row1 */
    frustum.planes[3].x = m[12] - m[4];
    frustum.planes[3].y = m[13] - m[5];
    frustum.planes[3].z = m[14] - m[6];
    frustum.planes[3].w = m[15] - m[7];

    /* Near plane: row2 */
    frustum.planes[4].x = m[8];
    frustum.planes[4].y = m[9];
    frustum.planes[4].z = m[10];
    frustum.planes[4].w = m[11];

    /* Far plane: row3 - row2 */
    frustum.planes[5].x = m[12] - m[8];
    frustum.planes[5].y = m[13] - m[9];
    frustum.planes[5].z = m[14] - m[10];
    frustum.planes[5].w = m[15] - m[11];

    /* Normalize each plane */
    for (int i = 0; i < 6; i++) {
        vec3 normal = vec3_new(frustum.planes[i].x, frustum.planes[i].y, frustum.planes[i].z);
        float length = vec3_length(normal);

        if (length > 0.0f) {
            frustum.planes[i].x /= length;
            frustum.planes[i].y /= length;
            frustum.planes[i].z /= length;
            frustum.planes[i].w /= length;
        }
    }

    return frustum;
}

bool frustum_test_aabb(const Frustum *f, AABB box)
{
    if (f == NULL) return false;

    for (int i = 0; i < 6; i++) {
        vec3 plane_normal = vec3_new(f->planes[i].x, f->planes[i].y, f->planes[i].z);
        float plane_distance = f->planes[i].w;

        vec3 p_vertex = box.min;

        if (plane_normal.x >= 0.0f) p_vertex.x = box.max.x;
        if (plane_normal.y >= 0.0f) p_vertex.y = box.max.y;
        if (plane_normal.z >= 0.0f) p_vertex.z = box.max.z;

        if (vec3_dot(plane_normal, p_vertex) + plane_distance < 0.0f) {
            return false;
        }
    }

    return true;
}

bool frustum_test_sphere(const Frustum *f, vec3 center, float radius)
{
    if (f == NULL) return false;

    for (int i = 0; i < 6; i++) {
        vec3 plane_normal = vec3_new(f->planes[i].x, f->planes[i].y, f->planes[i].z);
        float plane_distance = f->planes[i].w;

        if (vec3_dot(plane_normal, center) + plane_distance < -radius) {
            return false;
        }
    }

    return true;
}
