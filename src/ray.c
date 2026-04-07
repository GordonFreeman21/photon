#include "../include/ray.h"
#include "../include/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <immintrin.h>

/* ============================================================================
 * Ray Creation
 * ============================================================================ */

Ray ray_create(vec3 origin, vec3 dir)
{
    Ray ray;
    ray.origin = origin;
    ray.direction = dir;
    
    /* Compute inverse direction with safe division */
    ray.inv_direction.x = (dir.x != 0.0f) ? 1.0f / dir.x : 1e30f;
    ray.inv_direction.y = (dir.y != 0.0f) ? 1.0f / dir.y : 1e30f;
    ray.inv_direction.z = (dir.z != 0.0f) ? 1.0f / dir.z : 1e30f;
    
    ray.tmin = PHOTON_RAY_MIN_T;
    ray.tmax = PHOTON_RAY_MAX_T;
    
    return ray;
}

Ray ray_create_range(vec3 origin, vec3 dir, float tmin, float tmax)
{
    Ray ray;
    ray.origin = origin;
    ray.direction = dir;
    
    /* Compute inverse direction with safe division */
    ray.inv_direction.x = (dir.x != 0.0f) ? 1.0f / dir.x : 1e30f;
    ray.inv_direction.y = (dir.y != 0.0f) ? 1.0f / dir.y : 1e30f;
    ray.inv_direction.z = (dir.z != 0.0f) ? 1.0f / dir.z : 1e30f;
    
    ray.tmin = tmin;
    ray.tmax = tmax;
    
    return ray;
}

vec3 ray_at(const Ray *ray, float t)
{
    vec3 result;
    result.x = ray->origin.x + t * ray->direction.x;
    result.y = ray->origin.y + t * ray->direction.y;
    result.z = ray->origin.z + t * ray->direction.z;
    return result;
}

/* ============================================================================
 * Ray Generation
 * ============================================================================ */

Ray ray_generate_primary(const RTCamera *camera, float px, float py,
                         uint32_t screen_width, uint32_t screen_height)
{
    /* Convert pixel coordinates to NDC space [-1, 1] with camera jitter */
    float ndc_x = (2.0f * (px + 0.5f + camera->jitter_x)) / (float)screen_width - 1.0f;
    float ndc_y = 1.0f - (2.0f * (py + 0.5f + camera->jitter_y)) / (float)screen_height;
    
    /* Create homogeneous clip space coordinates */
    vec4 clip;
    clip.x = ndc_x;
    clip.y = ndc_y;
    clip.z = 1.0f;
    clip.w = 1.0f;
    
    /* Transform clip -> world via inv_view_projection (row-major mat4) */
    const float *M = camera->inv_view_projection.m;
    vec4 world;
    world.x = M[0]*clip.x + M[1]*clip.y + M[2]*clip.z  + M[3]*clip.w;
    world.y = M[4]*clip.x + M[5]*clip.y + M[6]*clip.z  + M[7]*clip.w;
    world.z = M[8]*clip.x + M[9]*clip.y + M[10]*clip.z + M[11]*clip.w;
    world.w = M[12]*clip.x + M[13]*clip.y + M[14]*clip.z + M[15]*clip.w;
    
    /* Perspective divide */
    if (fabsf(world.w) > 1e-6f) {
        world.x /= world.w;
        world.y /= world.w;
        world.z /= world.w;
    }
    
    /* Compute ray direction from camera position to world point */
    vec3 direction;
    direction.x = world.x - camera->position.x;
    direction.y = world.y - camera->position.y;
    direction.z = world.z - camera->position.z;
    
    /* Normalize direction */
    float len = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 1e-6f) {
        direction.x /= len;
        direction.y /= len;
        direction.z /= len;
    }
    
    return ray_create(camera->position, direction);
}

Ray ray_generate_shadow(vec3 pos, vec3 normal, vec3 light_dir, float max_dist)
{
    /* Offset origin along normal to avoid self-intersection (ray bias) */
    const float RAY_BIAS = 1e-4f;
    vec3 origin;
    origin.x = pos.x + RAY_BIAS * normal.x;
    origin.y = pos.y + RAY_BIAS * normal.y;
    origin.z = pos.z + RAY_BIAS * normal.z;
    
    /* Normalize light direction */
    float len = sqrtf(light_dir.x * light_dir.x + light_dir.y * light_dir.y + light_dir.z * light_dir.z);
    if (len < 1e-6f) {
        light_dir.x = 0.0f;
        light_dir.y = 1.0f;
        light_dir.z = 0.0f;
    } else {
        light_dir.x /= len;
        light_dir.y /= len;
        light_dir.z /= len;
    }
    
    /* Create shadow ray with custom max distance */
    return ray_create_range(origin, light_dir, PHOTON_RAY_MIN_T, max_dist);
}

Ray ray_generate_reflection(vec3 pos, vec3 normal, vec3 incoming,
                            float roughness, vec2 random_sample)
{
    /* Reflect incoming direction around normal: R = incoming - 2(incoming·normal)normal */
    float dot = incoming.x * normal.x + incoming.y * normal.y + incoming.z * normal.z;
    vec3 reflected;
    reflected.x = incoming.x - 2.0f * dot * normal.x;
    reflected.y = incoming.y - 2.0f * dot * normal.y;
    reflected.z = incoming.z - 2.0f * dot * normal.z;
    
    /* Apply GGX importance sampling perturbation based on roughness */
    if (roughness > 0.0f) {
        /* Generate random hemisphere direction for roughness */
        float theta = 2.0f * 3.14159265f * random_sample.x;
        float r = sqrtf(random_sample.y) * roughness;
        
        /* Tangent space perturbation */
        vec3 tangent, bitangent;
        if (fabsf(normal.x) < 0.9f) {
            tangent.x = 0.0f;
            tangent.y = normal.z;
            tangent.z = -normal.y;
        } else {
            tangent.x = normal.y;
            tangent.y = -normal.x;
            tangent.z = 0.0f;
        }
        
        /* Normalize tangent */
        float tlen = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
        if (tlen > 1e-6f) {
            tangent.x /= tlen;
            tangent.y /= tlen;
            tangent.z /= tlen;
        }
        
        /* Compute bitangent = normal × tangent */
        bitangent.x = normal.y * tangent.z - normal.z * tangent.y;
        bitangent.y = normal.z * tangent.x - normal.x * tangent.z;
        bitangent.z = normal.x * tangent.y - normal.y * tangent.x;
        
        /* Perturb reflection with roughness */
        float cos_theta = cosf(theta);
        float sin_theta = sinf(theta);
        reflected.x += r * (cos_theta * tangent.x + sin_theta * bitangent.x);
        reflected.y += r * (cos_theta * tangent.y + sin_theta * bitangent.y);
        reflected.z += r * (cos_theta * tangent.z + sin_theta * bitangent.z);
    }
    
    /* Normalize reflected direction */
    float len = sqrtf(reflected.x * reflected.x + reflected.y * reflected.y + reflected.z * reflected.z);
    if (len > 1e-6f) {
        reflected.x /= len;
        reflected.y /= len;
        reflected.z /= len;
    }
    
    /* Offset origin along normal to avoid self-intersection */
    const float RAY_BIAS = 1e-4f;
    vec3 origin;
    origin.x = pos.x + RAY_BIAS * normal.x;
    origin.y = pos.y + RAY_BIAS * normal.y;
    origin.z = pos.z + RAY_BIAS * normal.z;
    
    return ray_create(origin, reflected);
}

Ray ray_generate_hemisphere(vec3 pos, vec3 normal, vec2 random_sample)
{
    /* Cosine-weighted hemisphere sampling */
    float theta = 2.0f * 3.14159265f * random_sample.x;
    float r = sqrtf(random_sample.y);
    
    /* Compute tangent and bitangent for hemisphere basis */
    vec3 tangent, bitangent;
    if (fabsf(normal.x) < 0.9f) {
        tangent.x = 0.0f;
        tangent.y = normal.z;
        tangent.z = -normal.y;
    } else {
        tangent.x = normal.y;
        tangent.y = -normal.x;
        tangent.z = 0.0f;
    }
    
    /* Normalize tangent */
    float tlen = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
    if (tlen > 1e-6f) {
        tangent.x /= tlen;
        tangent.y /= tlen;
        tangent.z /= tlen;
    }
    
    /* Compute bitangent = normal × tangent */
    bitangent.x = normal.y * tangent.z - normal.z * tangent.y;
    bitangent.y = normal.z * tangent.x - normal.x * tangent.z;
    bitangent.z = normal.x * tangent.y - normal.y * tangent.x;
    
    /* Normalize bitangent */
    float blen = sqrtf(bitangent.x * bitangent.x + bitangent.y * bitangent.y + bitangent.z * bitangent.z);
    if (blen > 1e-6f) {
        bitangent.x /= blen;
        bitangent.y /= blen;
        bitangent.z /= blen;
    }
    
    /* Sample hemisphere */
    float cos_theta = cosf(theta);
    float sin_theta = sinf(theta);
    float cos_phi = sqrtf(1.0f - random_sample.y);  /* Cosine-weighted */
    float sin_phi = sqrtf(random_sample.y);
    
    vec3 direction;
    direction.x = sin_phi * (cos_theta * tangent.x + sin_theta * bitangent.x) + cos_phi * normal.x;
    direction.y = sin_phi * (cos_theta * tangent.y + sin_theta * bitangent.y) + cos_phi * normal.y;
    direction.z = sin_phi * (cos_theta * tangent.z + sin_theta * bitangent.z) + cos_phi * normal.z;
    
    /* Normalize direction */
    float len = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 1e-6f) {
        direction.x /= len;
        direction.y /= len;
        direction.z /= len;
    }
    
    /* Offset origin along normal to avoid self-intersection */
    const float RAY_BIAS = 1e-4f;
    vec3 origin;
    origin.x = pos.x + RAY_BIAS * normal.x;
    origin.y = pos.y + RAY_BIAS * normal.y;
    origin.z = pos.z + RAY_BIAS * normal.z;
    
    return ray_create(origin, direction);
}

/* ============================================================================
 * Ray Packet Operations
 * ============================================================================ */

RayPacket4 ray_packet4_from_rays(const Ray rays[4])
{
    RayPacket4 packet;
    
    /* Convert from Array of Structures (AoS) to Structure of Arrays (SoA) */
    /* using SSE to pack 4 values per component */
    
    packet.origin_x = _mm_set_ps(
        rays[3].origin.x,
        rays[2].origin.x,
        rays[1].origin.x,
        rays[0].origin.x
    );
    packet.origin_y = _mm_set_ps(
        rays[3].origin.y,
        rays[2].origin.y,
        rays[1].origin.y,
        rays[0].origin.y
    );
    packet.origin_z = _mm_set_ps(
        rays[3].origin.z,
        rays[2].origin.z,
        rays[1].origin.z,
        rays[0].origin.z
    );
    
    packet.dir_x = _mm_set_ps(
        rays[3].direction.x,
        rays[2].direction.x,
        rays[1].direction.x,
        rays[0].direction.x
    );
    packet.dir_y = _mm_set_ps(
        rays[3].direction.y,
        rays[2].direction.y,
        rays[1].direction.y,
        rays[0].direction.y
    );
    packet.dir_z = _mm_set_ps(
        rays[3].direction.z,
        rays[2].direction.z,
        rays[1].direction.z,
        rays[0].direction.z
    );
    
    packet.inv_dir_x = _mm_set_ps(
        rays[3].inv_direction.x,
        rays[2].inv_direction.x,
        rays[1].inv_direction.x,
        rays[0].inv_direction.x
    );
    packet.inv_dir_y = _mm_set_ps(
        rays[3].inv_direction.y,
        rays[2].inv_direction.y,
        rays[1].inv_direction.y,
        rays[0].inv_direction.y
    );
    packet.inv_dir_z = _mm_set_ps(
        rays[3].inv_direction.z,
        rays[2].inv_direction.z,
        rays[1].inv_direction.z,
        rays[0].inv_direction.z
    );
    
    packet.tmin = _mm_set_ps(rays[3].tmin, rays[2].tmin, rays[1].tmin, rays[0].tmin);
    packet.tmax = _mm_set_ps(rays[3].tmax, rays[2].tmax, rays[1].tmax, rays[0].tmax);
    
    return packet;
}

/* Comparison function for qsort */
static int ray_compare_direction(const void *a, const void *b)
{
    const Ray *ray_a = (const Ray *)a;
    const Ray *ray_b = (const Ray *)b;
    
    uint32_t hash_a = ray_direction_hash(ray_a->direction.x, ray_a->direction.y, ray_a->direction.z);
    uint32_t hash_b = ray_direction_hash(ray_b->direction.x, ray_b->direction.y, ray_b->direction.z);
    
    if (hash_a < hash_b) return -1;
    if (hash_a > hash_b) return 1;
    return 0;
}

void ray_sort_by_direction(Ray *rays, uint32_t count)
{
    if (rays == NULL || count < 2) {
        return;
    }
    
    qsort(rays, count, sizeof(Ray), ray_compare_direction);
}

/* ============================================================================
 * Ray Buffer
 * ============================================================================ */

RayBuffer ray_buffer_create(uint32_t capacity)
{
    RayBuffer buffer = {0};
    
    if (capacity == 0) {
        capacity = 256;  /* Default capacity */
    }
    
    buffer.rays = (Ray *)malloc(capacity * sizeof(Ray));
    if (buffer.rays == NULL) {
        return buffer;
    }
    
    buffer.hits = (HitInfo *)malloc(capacity * sizeof(HitInfo));
    if (buffer.hits == NULL) {
        free(buffer.rays);
        buffer.rays = NULL;
        return buffer;
    }
    
    buffer.capacity = capacity;
    buffer.count = 0;
    
    return buffer;
}

void ray_buffer_destroy(RayBuffer *buf)
{
    if (buf == NULL) {
        return;
    }
    
    if (buf->rays != NULL) {
        free(buf->rays);
        buf->rays = NULL;
    }
    
    if (buf->hits != NULL) {
        free(buf->hits);
        buf->hits = NULL;
    }
    
    buf->count = 0;
    buf->capacity = 0;
}

void ray_buffer_clear(RayBuffer *buf)
{
    if (buf != NULL) {
        buf->count = 0;
    }
}

void ray_buffer_push(RayBuffer *buf, Ray ray)
{
    if (buf == NULL) {
        return;
    }
    
    /* Grow buffer if needed (2x capacity) */
    if (buf->count >= buf->capacity) {
        uint32_t new_capacity = buf->capacity * 2;
        
        Ray *new_rays = (Ray *)realloc(buf->rays, new_capacity * sizeof(Ray));
        if (new_rays == NULL) {
            return;
        }
        buf->rays = new_rays;
        
        HitInfo *new_hits = (HitInfo *)realloc(buf->hits, new_capacity * sizeof(HitInfo));
        if (new_hits == NULL) {
            return;
        }
        buf->hits = new_hits;
        
        buf->capacity = new_capacity;
    }
    
    buf->rays[buf->count] = ray;
    buf->count++;
}
