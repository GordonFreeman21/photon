/*
 * Photon - Camera and View Transforms
 */
#ifndef PHOTON_CAMERA_H
#define PHOTON_CAMERA_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Camera Operations                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

RTCamera    camera_create(vec3 position, vec3 target, float fov_y, float aspect);
void        camera_update(RTCamera* cam);
void        camera_set_position(RTCamera* cam, vec3 position);
void        camera_set_target(RTCamera* cam, vec3 target);
void        camera_set_fov(RTCamera* cam, float fov_y);
void        camera_set_aspect(RTCamera* cam, float aspect);

/* Apply sub-pixel jitter for temporal anti-aliasing */
void        camera_set_jitter(RTCamera* cam, float jx, float jy);

/* Generate Halton-sequence jitter for frame index */
void        camera_compute_taa_jitter(RTCamera* cam, uint32_t frame_index);

/* Store current matrices as "previous" for motion vectors */
void        camera_save_previous(RTCamera* cam);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Frustum Culling                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec4 planes[6]; /* left, right, bottom, top, near, far; xyz=normal, w=distance */
} Frustum;

Frustum frustum_extract(const mat4* view_proj);
bool    frustum_test_aabb(const Frustum* f, AABB box);
bool    frustum_test_sphere(const Frustum* f, vec3 center, float radius);

#endif /* PHOTON_CAMERA_H */
