/*
 * Photon - SIMD Math Utilities
 */
#ifndef PHOTON_MATH_UTIL_H
#define PHOTON_MATH_UTIL_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Scalar math                                                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

PHOTON_INLINE float photon_minf(float a, float b) { return a < b ? a : b; }
PHOTON_INLINE float photon_maxf(float a, float b) { return a > b ? a : b; }
PHOTON_INLINE float photon_clampf(float v, float lo, float hi) { return photon_maxf(lo, photon_minf(v, hi)); }
PHOTON_INLINE float photon_lerpf(float a, float b, float t) { return a + t * (b - a); }
PHOTON_INLINE float photon_saturate(float v) { return photon_clampf(v, 0.0f, 1.0f); }

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Vec3 operations                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

PHOTON_INLINE vec3 vec3_new(float x, float y, float z) { return (vec3){x, y, z}; }
PHOTON_INLINE vec3 vec3_add(vec3 a, vec3 b) { return (vec3){a.x+b.x, a.y+b.y, a.z+b.z}; }
PHOTON_INLINE vec3 vec3_sub(vec3 a, vec3 b) { return (vec3){a.x-b.x, a.y-b.y, a.z-b.z}; }
PHOTON_INLINE vec3 vec3_mul(vec3 a, vec3 b) { return (vec3){a.x*b.x, a.y*b.y, a.z*b.z}; }
PHOTON_INLINE vec3 vec3_scale(vec3 v, float s) { return (vec3){v.x*s, v.y*s, v.z*s}; }
PHOTON_INLINE vec3 vec3_neg(vec3 v) { return (vec3){-v.x, -v.y, -v.z}; }
PHOTON_INLINE float vec3_dot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
PHOTON_INLINE vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
PHOTON_INLINE float vec3_length_sq(vec3 v) { return vec3_dot(v, v); }
float vec3_length(vec3 v);
vec3  vec3_normalize(vec3 v);
PHOTON_INLINE vec3 vec3_lerp(vec3 a, vec3 b, float t) {
    return (vec3){photon_lerpf(a.x,b.x,t), photon_lerpf(a.y,b.y,t), photon_lerpf(a.z,b.z,t)};
}
PHOTON_INLINE vec3 vec3_min(vec3 a, vec3 b) {
    return (vec3){photon_minf(a.x,b.x), photon_minf(a.y,b.y), photon_minf(a.z,b.z)};
}
PHOTON_INLINE vec3 vec3_max(vec3 a, vec3 b) {
    return (vec3){photon_maxf(a.x,b.x), photon_maxf(a.y,b.y), photon_maxf(a.z,b.z)};
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Vec4 / Mat4 operations                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

mat4    mat4_identity(void);
mat4    mat4_multiply(mat4 a, mat4 b);
mat4    mat4_perspective(float fov_y, float aspect, float near, float far);
mat4    mat4_look_at(vec3 eye, vec3 center, vec3 up);
mat4    mat4_inverse(mat4 m);
vec3    mat4_transform_point(mat4 m, vec3 p);
vec3    mat4_transform_dir(mat4 m, vec3 d);
vec4    mat4_transform_vec4(mat4 m, vec4 v);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  AABB operations                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

AABB    aabb_empty(void);
AABB    aabb_union(AABB a, AABB b);
AABB    aabb_union_point(AABB a, vec3 p);
float   aabb_surface_area(AABB a);
vec3    aabb_center(AABB a);
AABB    aabb_transform(AABB a, mat4 m);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  SSE Ray-AABB intersection (4 boxes vs 1 ray)                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

__m128 ray_aabb_intersect_sse4(
    __m128 ray_org_x, __m128 ray_org_y, __m128 ray_org_z,
    __m128 ray_inv_x, __m128 ray_inv_y, __m128 ray_inv_z,
    __m128 box_min_x, __m128 box_min_y, __m128 box_min_z,
    __m128 box_max_x, __m128 box_max_y, __m128 box_max_z,
    __m128 ray_tmin,  __m128 ray_tmax);

/* Single ray vs single AABB */
bool ray_aabb_intersect(const Ray* ray, const float aabb_min[3], const float aabb_max[3]);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Möller-Trumbore ray-triangle intersection                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool ray_triangle_intersect(const Ray* ray, vec3 v0, vec3 v1, vec3 v2,
                            float* out_t, float* out_u, float* out_v);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Sampling & low-discrepancy sequences                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

float   halton_sequence(uint32_t index, uint32_t base);
vec2    halton_2d(uint32_t index);
float   sobol_sequence(uint32_t index, uint32_t dimension);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Morton codes for spatial hashing                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t morton_encode_3d(uint32_t x, uint32_t y, uint32_t z);
uint32_t ray_direction_hash(float dx, float dy, float dz);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Normal encoding (octahedral)                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

vec2    octahedral_encode(vec3 n);
vec3    octahedral_decode(vec2 e);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Spherical Harmonics L2                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

void    sh_evaluate_basis(vec3 dir, float basis[PHOTON_SH_COEFF_COUNT]);
void    sh_project_sample(float coeffs[PHOTON_SH_COEFF_COUNT], vec3 dir, float value);
float   sh_reconstruct(const float coeffs[PHOTON_SH_COEFF_COUNT], vec3 dir);

#endif /* PHOTON_MATH_UTIL_H */
