#include "../include/math_util.h"
#include <math.h>
#include <float.h>
#include <immintrin.h>
#include <string.h>

/* ============================================================================
 * VEC3 OPERATIONS
 * ============================================================================ */

float vec3_length(vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

vec3 vec3_normalize(vec3 v) {
    float len = vec3_length(v);
    if (len < 1e-6f) {
        return (vec3){0.0f, 0.0f, 0.0f};
    }
    float inv_len = 1.0f / len;
    return (vec3){v.x * inv_len, v.y * inv_len, v.z * inv_len};
}

/* ============================================================================
 * MAT4 OPERATIONS
 * ============================================================================ */

mat4 mat4_identity(void) {
    mat4 m = {0};
    m.m[0]  = 1.0f;
    m.m[5]  = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

mat4 mat4_multiply(mat4 a, mat4 b) {
    mat4 result = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                result.m[i*4+j] += a.m[i*4+k] * b.m[k*4+j];
            }
        }
    }
    return result;
}

mat4 mat4_perspective(float fov_y, float aspect, float near, float far) {
    /* Vulkan clip space: y-flip, z in [0, 1] */
    mat4 m = {0};
    float f = 1.0f / tanf(fov_y * 0.5f);
    
    m.m[0]  = f / aspect;
    m.m[5]  = -f;  /* Negative for y-flip in Vulkan */
    m.m[10] = far / (near - far);
    m.m[11] = -1.0f;
    m.m[14] = (far * near) / (near - far);
    
    return m;
}

mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = vec3_normalize(vec3_sub(center, eye));
    vec3 s = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);
    
    mat4 m = mat4_identity();
    
    m.m[0]  = s.x;
    m.m[4]  = s.y;
    m.m[8]  = s.z;
    m.m[12] = -vec3_dot(s, eye);
    
    m.m[1]  = u.x;
    m.m[5]  = u.y;
    m.m[9]  = u.z;
    m.m[13] = -vec3_dot(u, eye);
    
    m.m[2]  = -f.x;
    m.m[6]  = -f.y;
    m.m[10] = -f.z;
    m.m[14] = vec3_dot(f, eye);
    
    return m;
}

mat4 mat4_inverse(mat4 m) {
    float* v = m.m;
    mat4 inv = {0};
    float* iv = inv.m;
    
    /* Compute cofactors */
    iv[0] = v[5] * v[10] * v[15] - v[5] * v[11] * v[14] - v[9] * v[6] * v[15] + 
            v[9] * v[7] * v[14] + v[13] * v[6] * v[11] - v[13] * v[7] * v[10];
    
    iv[4] = -v[4] * v[10] * v[15] + v[4] * v[11] * v[14] + v[8] * v[6] * v[15] - 
            v[8] * v[7] * v[14] - v[12] * v[6] * v[11] + v[12] * v[7] * v[10];
    
    iv[8] = v[4] * v[9] * v[15] - v[4] * v[11] * v[13] - v[8] * v[5] * v[15] + 
            v[8] * v[7] * v[13] + v[12] * v[5] * v[11] - v[12] * v[7] * v[9];
    
    iv[12] = -v[4] * v[9] * v[14] + v[4] * v[10] * v[13] + v[8] * v[5] * v[14] - 
             v[8] * v[6] * v[13] - v[12] * v[5] * v[10] + v[12] * v[6] * v[9];
    
    iv[1] = -v[1] * v[10] * v[15] + v[1] * v[11] * v[14] + v[9] * v[2] * v[15] - 
            v[9] * v[3] * v[14] - v[13] * v[2] * v[11] + v[13] * v[3] * v[10];
    
    iv[5] = v[0] * v[10] * v[15] - v[0] * v[11] * v[14] - v[8] * v[2] * v[15] + 
            v[8] * v[3] * v[14] + v[12] * v[2] * v[11] - v[12] * v[3] * v[10];
    
    iv[9] = -v[0] * v[9] * v[15] + v[0] * v[11] * v[13] + v[8] * v[1] * v[15] - 
            v[8] * v[3] * v[13] - v[12] * v[1] * v[11] + v[12] * v[3] * v[9];
    
    iv[13] = v[0] * v[9] * v[14] - v[0] * v[10] * v[13] - v[8] * v[1] * v[14] + 
             v[8] * v[2] * v[13] + v[12] * v[1] * v[10] - v[12] * v[2] * v[9];
    
    iv[2] = v[1] * v[6] * v[15] - v[1] * v[7] * v[14] - v[5] * v[2] * v[15] + 
            v[5] * v[3] * v[14] + v[13] * v[2] * v[7] - v[13] * v[3] * v[6];
    
    iv[6] = -v[0] * v[6] * v[15] + v[0] * v[7] * v[14] + v[4] * v[2] * v[15] - 
            v[4] * v[3] * v[14] - v[12] * v[2] * v[7] + v[12] * v[3] * v[6];
    
    iv[10] = v[0] * v[5] * v[15] - v[0] * v[7] * v[13] - v[4] * v[1] * v[15] + 
             v[4] * v[3] * v[13] + v[12] * v[1] * v[7] - v[12] * v[3] * v[5];
    
    iv[14] = -v[0] * v[5] * v[14] + v[0] * v[6] * v[13] + v[4] * v[1] * v[14] - 
             v[4] * v[2] * v[13] - v[12] * v[1] * v[6] + v[12] * v[2] * v[5];
    
    iv[3] = -v[1] * v[6] * v[11] + v[1] * v[7] * v[10] + v[5] * v[2] * v[11] - 
            v[5] * v[3] * v[10] - v[9] * v[2] * v[7] + v[9] * v[3] * v[6];
    
    iv[7] = v[0] * v[6] * v[11] - v[0] * v[7] * v[10] - v[4] * v[2] * v[11] + 
            v[4] * v[3] * v[10] + v[8] * v[2] * v[7] - v[8] * v[3] * v[6];
    
    iv[11] = -v[0] * v[5] * v[11] + v[0] * v[7] * v[9] + v[4] * v[1] * v[11] - 
             v[4] * v[3] * v[9] - v[8] * v[1] * v[7] + v[8] * v[3] * v[5];
    
    iv[15] = v[0] * v[5] * v[10] - v[0] * v[6] * v[9] - v[4] * v[1] * v[10] + 
             v[4] * v[2] * v[9] + v[8] * v[1] * v[6] - v[8] * v[2] * v[5];
    
    /* Compute determinant and divide */
    float det = v[0] * iv[0] + v[1] * iv[4] + v[2] * iv[8] + v[3] * iv[12];
    
    if (fabsf(det) > 1e-6f) {
        float inv_det = 1.0f / det;
        for (int i = 0; i < 16; i++) {
            iv[i] *= inv_det;
        }
    }
    
    return inv;
}

vec4 mat4_transform_vec4(mat4 m, vec4 v) {
    vec4 result;
    result.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z + m.m[12] * v.w;
    result.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z + m.m[13] * v.w;
    result.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    result.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return result;
}

vec3 mat4_transform_point(mat4 m, vec3 p) {
    vec4 v = {p.x, p.y, p.z, 1.0f};
    vec4 result = mat4_transform_vec4(m, v);
    float inv_w = (fabsf(result.w) > 1e-6f) ? 1.0f / result.w : 1.0f;
    return (vec3){result.x * inv_w, result.y * inv_w, result.z * inv_w};
}

vec3 mat4_transform_dir(mat4 m, vec3 d) {
    vec4 v = {d.x, d.y, d.z, 0.0f};
    vec4 result = mat4_transform_vec4(m, v);
    return (vec3){result.x, result.y, result.z};
}

/* ============================================================================
 * AABB OPERATIONS
 * ============================================================================ */

AABB aabb_empty(void) {
    return (AABB){
        {FLT_MAX, FLT_MAX, FLT_MAX},
        {-FLT_MAX, -FLT_MAX, -FLT_MAX}
    };
}

AABB aabb_union(AABB a, AABB b) {
    return (AABB){
        {
            fminf(a.min.x, b.min.x),
            fminf(a.min.y, b.min.y),
            fminf(a.min.z, b.min.z)
        },
        {
            fmaxf(a.max.x, b.max.x),
            fmaxf(a.max.y, b.max.y),
            fmaxf(a.max.z, b.max.z)
        }
    };
}

AABB aabb_union_point(AABB a, vec3 p) {
    return (AABB){
        {
            fminf(a.min.x, p.x),
            fminf(a.min.y, p.y),
            fminf(a.min.z, p.z)
        },
        {
            fmaxf(a.max.x, p.x),
            fmaxf(a.max.y, p.y),
            fmaxf(a.max.z, p.z)
        }
    };
}

float aabb_surface_area(AABB aabb) {
    vec3 d = vec3_sub(aabb.max, aabb.min);
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}

vec3 aabb_center(AABB aabb) {
    return vec3_scale(vec3_add(aabb.min, aabb.max), 0.5f);
}

AABB aabb_transform(AABB aabb, mat4 m) {
    /* Transform all 8 corners and compute new AABB */
    vec3 corners[8] = {
        {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.max.z}
    };
    
    AABB result = aabb_empty();
    for (int i = 0; i < 8; i++) {
        vec3 transformed = mat4_transform_point(m, corners[i]);
        result = aabb_union_point(result, transformed);
    }
    
    return result;
}

/* ============================================================================
 * RAY-AABB INTERSECTION
 * ============================================================================ */

bool ray_aabb_intersect(const Ray* ray, const float aabb_min[3], const float aabb_max[3]) {
    float t_min = ray->tmin;
    float t_max = ray->tmax;
    
    for (int i = 0; i < 3; i++) {
        float org = (&ray->origin.x)[i];
        float inv_d = (&ray->inv_direction.x)[i];
        float t1 = (aabb_min[i] - org) * inv_d;
        float t2 = (aabb_max[i] - org) * inv_d;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        t_min = fmaxf(t_min, t1);
        t_max = fminf(t_max, t2);
    }
    
    return t_min <= t_max;
}

__m128 ray_aabb_intersect_sse4(
    __m128 ray_org_x, __m128 ray_org_y, __m128 ray_org_z,
    __m128 ray_inv_x, __m128 ray_inv_y, __m128 ray_inv_z,
    __m128 box_min_x, __m128 box_min_y, __m128 box_min_z,
    __m128 box_max_x, __m128 box_max_y, __m128 box_max_z,
    __m128 ray_tmin,  __m128 ray_tmax)
{
    /* X slab */
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(box_min_x, ray_org_x), ray_inv_x);
    __m128 t2 = _mm_mul_ps(_mm_sub_ps(box_max_x, ray_org_x), ray_inv_x);
    __m128 t_min = _mm_min_ps(t1, t2);
    __m128 t_max = _mm_max_ps(t1, t2);
    
    /* Y slab */
    t1 = _mm_mul_ps(_mm_sub_ps(box_min_y, ray_org_y), ray_inv_y);
    t2 = _mm_mul_ps(_mm_sub_ps(box_max_y, ray_org_y), ray_inv_y);
    t_min = _mm_max_ps(t_min, _mm_min_ps(t1, t2));
    t_max = _mm_min_ps(t_max, _mm_max_ps(t1, t2));
    
    /* Z slab */
    t1 = _mm_mul_ps(_mm_sub_ps(box_min_z, ray_org_z), ray_inv_z);
    t2 = _mm_mul_ps(_mm_sub_ps(box_max_z, ray_org_z), ray_inv_z);
    t_min = _mm_max_ps(t_min, _mm_min_ps(t1, t2));
    t_max = _mm_min_ps(t_max, _mm_max_ps(t1, t2));
    
    /* Clamp to ray interval */
    t_min = _mm_max_ps(t_min, ray_tmin);
    t_max = _mm_min_ps(t_max, ray_tmax);
    
    return _mm_cmple_ps(t_min, t_max);
}

/* ============================================================================
 * MÖLLER-TRUMBORE RAY-TRIANGLE INTERSECTION
 * ============================================================================ */

bool ray_triangle_intersect(const Ray* ray, vec3 v0, vec3 v1, vec3 v2,
                            float* out_t, float* out_u, float* out_v)
{
    const float epsilon = 1e-6f;
    
    vec3 edge1 = vec3_sub(v1, v0);
    vec3 edge2 = vec3_sub(v2, v0);
    
    vec3 h = vec3_cross(ray->direction, edge2);
    float a = vec3_dot(edge1, h);
    
    if (fabsf(a) < epsilon) {
        return false;  /* Ray parallel to triangle */
    }
    
    float f = 1.0f / a;
    vec3 s = vec3_sub(ray->origin, v0);
    float u = f * vec3_dot(s, h);
    
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    
    vec3 q = vec3_cross(s, edge1);
    float v = f * vec3_dot(ray->direction, q);
    
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    
    float t = f * vec3_dot(edge2, q);
    
    if (t > epsilon) {
        *out_t = t;
        *out_u = u;
        *out_v = v;
        return true;
    }
    
    return false;
}

/* ============================================================================
 * SAMPLING: HALTON SEQUENCE
 * ============================================================================ */

float halton_sequence(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float f = 1.0f / (float)base;
    uint32_t i = index + 1;
    
    while (i > 0) {
        result += f * (float)(i % base);
        i /= base;
        f /= (float)base;
    }
    
    return result;
}

vec2 halton_2d(uint32_t index) {
    return (vec2){halton_sequence(index, 2), halton_sequence(index, 3)};
}

/* ============================================================================
 * SAMPLING: SOBOL SEQUENCE
 * ============================================================================ */

float sobol_sequence(uint32_t index, uint32_t dimension) {
    /* Basic Sobol using bit reversal with dimension-based scramble */
    float result = 0.0f;
    float f = 0.5f;
    uint32_t scramble = dimension * 0x9E3779B9u;
    uint32_t v = index ^ scramble;
    
    for (int i = 0; i < 32 && v > 0; i++) {
        if (v & 1) {
            result += f;
        }
        f *= 0.5f;
        v >>= 1;
    }
    
    return result;
}

/* ============================================================================
 * MORTON CODES
 * ============================================================================ */

static uint32_t morton_expand(uint32_t x) {
    /* Expand a 10-bit integer to 30 bits by inserting 2 zeros after each bit */
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x << 8)) & 0x0300F00F;
    x = (x | (x << 4)) & 0x030C30C3;
    x = (x | (x << 2)) & 0x09249249;
    return x;
}

uint32_t morton_encode_3d(uint32_t x, uint32_t y, uint32_t z) {
    /* Interleave bits of x, y, z */
    return morton_expand(x) | (morton_expand(y) << 1) | (morton_expand(z) << 2);
}

uint32_t ray_direction_hash(float dx, float dy, float dz) {
    /* Quantize direction to octant + quantized minor components */
    float abs_dx = fabsf(dx), abs_dy = fabsf(dy), abs_dz = fabsf(dz);
    
    /* Determine dominant axis (octant) */
    uint32_t octant = 0;
    if (dx < 0.0f) octant |= 1;
    if (dy < 0.0f) octant |= 2;
    if (dz < 0.0f) octant |= 4;
    
    /* Normalize to dominant axis and quantize */
    uint32_t u = 0, v = 0;
    
    if (abs_dx >= abs_dy && abs_dx >= abs_dz) {
        u = (uint32_t)((abs_dy / abs_dx) * 1023.0f);
        v = (uint32_t)((abs_dz / abs_dx) * 1023.0f);
    } else if (abs_dy >= abs_dz) {
        u = (uint32_t)((abs_dx / abs_dy) * 1023.0f);
        v = (uint32_t)((abs_dz / abs_dy) * 1023.0f);
    } else {
        u = (uint32_t)((abs_dx / abs_dz) * 1023.0f);
        v = (uint32_t)((abs_dy / abs_dz) * 1023.0f);
    }
    
    return (octant << 20) | (u << 10) | v;
}

/* ============================================================================
 * OCTAHEDRAL ENCODING
 * ============================================================================ */

vec2 octahedral_encode(vec3 n) {
    /* Map unit normal to octahedron, then to 2D */
    float l1_norm = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
    float x = n.x / l1_norm;
    float y = n.y / l1_norm;
    
    if (n.z < 0.0f) {
        float ox = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        float oy = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = ox;
        y = oy;
    }
    
    return (vec2){x, y};
}

vec3 octahedral_decode(vec2 enc) {
    /* Reverse octahedral mapping */
    float x = enc.x;
    float y = enc.y;
    
    vec3 n = {x, y, 1.0f - fabsf(x) - fabsf(y)};
    
    if (n.z < 0.0f) {
        float ox = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        float oy = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        n.x = ox;
        n.y = oy;
    }
    
    return vec3_normalize(n);
}

/* ============================================================================
 * SPHERICAL HARMONICS (L2, 9 coefficients)
 * ============================================================================ */

static const float SH_C0 = 0.282094791f;
static const float SH_C1 = 0.488602016f;
static const float SH_C2 = 1.092548431f;
static const float SH_C3 = 0.315391565f;
static const float SH_C4 = 0.546274215f;

void sh_evaluate_basis(vec3 dir, float basis[PHOTON_SH_COEFF_COUNT]) {
    /* Compute 9 SH L2 basis functions for a direction */
    float x = dir.x;
    float y = dir.y;
    float z = dir.z;
    
    float x2 = x * x;
    float y2 = y * y;
    float z2 = z * z;
    
    basis[0] = SH_C0;
    basis[1] = SH_C1 * y;
    basis[2] = SH_C1 * z;
    basis[3] = SH_C1 * x;
    basis[4] = SH_C2 * x * y;
    basis[5] = SH_C2 * y * z;
    basis[6] = SH_C3 * (3.0f * z2 - 1.0f);
    basis[7] = SH_C2 * x * z;
    basis[8] = SH_C4 * (x2 - y2);
}

void sh_project_sample(float coeffs[PHOTON_SH_COEFF_COUNT], vec3 dir, float value) {
    /* Add weighted sample to SH coefficients */
    float basis[9];
    sh_evaluate_basis(dir, basis);
    
    for (int i = 0; i < 9; i++) {
        coeffs[i] += basis[i] * value;
    }
}

float sh_reconstruct(const float coeffs[PHOTON_SH_COEFF_COUNT], vec3 dir) {
    /* Dot product of coefficients with basis functions */
    float basis[9];
    sh_evaluate_basis(dir, basis);
    
    float result = 0.0f;
    for (int i = 0; i < 9; i++) {
        result += coeffs[i] * basis[i];
    }
    
    return result;
}
