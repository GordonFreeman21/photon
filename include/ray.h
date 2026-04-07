/*
 * Photon - Ray Structures and Utilities
 */
#ifndef PHOTON_RAY_H
#define PHOTON_RAY_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Creation                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

Ray     ray_create(vec3 origin, vec3 direction);
Ray     ray_create_range(vec3 origin, vec3 direction, float tmin, float tmax);
vec3    ray_at(const Ray* r, float t);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Generation from Camera                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Generate primary ray for pixel (x, y) with sub-pixel offset */
Ray ray_generate_primary(const RTCamera* camera, float px, float py,
                         uint32_t screen_width, uint32_t screen_height);

/* Generate shadow ray from surface point toward light */
Ray ray_generate_shadow(vec3 surface_pos, vec3 surface_normal, vec3 light_dir, float max_dist);

/* Generate reflection ray */
Ray ray_generate_reflection(vec3 surface_pos, vec3 surface_normal,
                            vec3 incoming_dir, float roughness, vec2 random_sample);

/* Generate hemisphere sample for GI */
Ray ray_generate_hemisphere(vec3 surface_pos, vec3 surface_normal,
                            vec2 random_sample);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Packet Operations                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Create packet from 4 individual rays */
RayPacket4 ray_packet4_from_rays(const Ray rays[4]);

/* Sort rays by direction for coherence */
void ray_sort_by_direction(Ray* rays, uint32_t count);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Buffer (for compute shader dispatch)                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    Ray*        rays;
    HitInfo*    hits;
    uint32_t    count;
    uint32_t    capacity;
} RayBuffer;

RayBuffer   ray_buffer_create(uint32_t capacity);
void        ray_buffer_destroy(RayBuffer* buf);
void        ray_buffer_clear(RayBuffer* buf);
void        ray_buffer_push(RayBuffer* buf, Ray ray);

#endif /* PHOTON_RAY_H */
