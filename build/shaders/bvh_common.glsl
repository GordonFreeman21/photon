/*
 * Photon - Shared BVH traversal code for compute shaders
 * Included by all ray tracing compute shaders
 */

#ifndef BVH_COMMON_GLSL
#define BVH_COMMON_GLSL

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Node Structure (matches C-side BVHNode, 64 bytes)                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

struct BVHNode {
    vec4 aabb_min;      // xyz = min bounds, w = padding
    vec4 aabb_max;      // xyz = max bounds, w = padding
    uint left_child;    // or first_prim for leaf
    uint right_child;   // or prim_count for leaf
    uint parent_idx;
    uint flags;         // bit 0: leaf, bits 1-2: split axis
};

#define BVH_FLAG_LEAF   0x01u
#define INVALID_IDX     0xFFFFFFFFu
#define MAX_STACK_DEPTH 64

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Structure                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

struct Ray {
    vec3 origin;
    float tmin;
    vec3 direction;
    float tmax;
    vec3 inv_direction;
    uint pixel_index;
};

struct HitInfo {
    float t;
    float u, v;
    uint prim_id;
    uint inst_id;
    uint mesh_id;
    uint mat_id;
    bool hit;
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Triangle Structure                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

struct Triangle {
    vec3 v0, v1, v2;
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Buffer Bindings                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

layout(std430, binding = 0) readonly buffer BVHNodes {
    BVHNode bvh_nodes[];
};

layout(std430, binding = 1) readonly buffer Vertices {
    float vertices[];   // packed vec3 positions
};

layout(std430, binding = 2) readonly buffer Indices {
    uint indices[];
};

layout(std430, binding = 3) readonly buffer PrimIndices {
    uint prim_indices[];
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray-AABB Intersection (slab test)                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool ray_aabb_intersect(Ray ray, vec3 aabb_min, vec3 aabb_max) {
    vec3 t1 = (aabb_min - ray.origin) * ray.inv_direction;
    vec3 t2 = (aabb_max - ray.origin) * ray.inv_direction;
    
    vec3 tmin_v = min(t1, t2);
    vec3 tmax_v = max(t1, t2);
    
    float tmin = max(max(tmin_v.x, tmin_v.y), max(tmin_v.z, ray.tmin));
    float tmax = min(min(tmax_v.x, tmax_v.y), min(tmax_v.z, ray.tmax));
    
    return tmin <= tmax;
}

float ray_aabb_intersect_dist(Ray ray, vec3 aabb_min, vec3 aabb_max) {
    vec3 t1 = (aabb_min - ray.origin) * ray.inv_direction;
    vec3 t2 = (aabb_max - ray.origin) * ray.inv_direction;
    
    vec3 tmin_v = min(t1, t2);
    vec3 tmax_v = max(t1, t2);
    
    float tmin = max(max(tmin_v.x, tmin_v.y), max(tmin_v.z, ray.tmin));
    float tmax = min(min(tmax_v.x, tmax_v.y), min(tmax_v.z, ray.tmax));
    
    return (tmin <= tmax) ? tmin : 1e30;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray-Triangle Intersection (Möller-Trumbore)                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool ray_triangle_intersect(Ray ray, vec3 v0, vec3 v1, vec3 v2,
                            out float out_t, out float out_u, out float out_v) {
    const float EPSILON = 1e-6;
    
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;
    vec3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    
    if (abs(a) < EPSILON) return false;
    
    float f = 1.0 / a;
    vec3 s = ray.origin - v0;
    out_u = f * dot(s, h);
    
    if (out_u < 0.0 || out_u > 1.0) return false;
    
    vec3 q = cross(s, edge1);
    out_v = f * dot(ray.direction, q);
    
    if (out_v < 0.0 || out_u + out_v > 1.0) return false;
    
    out_t = f * dot(edge2, q);
    
    return out_t > ray.tmin && out_t < ray.tmax;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Fetch triangle vertices from buffer                                       */
/* ═══════════════════════════════════════════════════════════════════════════ */

void fetch_triangle(uint tri_idx, out vec3 v0, out vec3 v1, out vec3 v2) {
    uint i0 = indices[tri_idx * 3 + 0];
    uint i1 = indices[tri_idx * 3 + 1];
    uint i2 = indices[tri_idx * 3 + 2];
    
    v0 = vec3(vertices[i0*3], vertices[i0*3+1], vertices[i0*3+2]);
    v1 = vec3(vertices[i1*3], vertices[i1*3+1], vertices[i1*3+2]);
    v2 = vec3(vertices[i2*3], vertices[i2*3+1], vertices[i2*3+2]);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Traversal (iterative with explicit stack)                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

HitInfo traverse_bvh(Ray ray) {
    HitInfo hit;
    hit.t = ray.tmax;
    hit.hit = false;
    
    uint stack[MAX_STACK_DEPTH];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0u; // root node
    
    while (stack_ptr > 0) {
        uint node_idx = stack[--stack_ptr];
        BVHNode node = bvh_nodes[node_idx];
        
        if (!ray_aabb_intersect(ray, node.aabb_min.xyz, node.aabb_max.xyz))
            continue;
        
        if ((node.flags & BVH_FLAG_LEAF) != 0u) {
            // Leaf node: test triangles
            uint first = node.left_child;   // first_prim
            uint count = node.right_child;  // prim_count
            
            for (uint i = 0u; i < count; i++) {
                uint prim_idx = prim_indices[first + i];
                vec3 v0, v1, v2;
                fetch_triangle(prim_idx, v0, v1, v2);
                
                float t, u, v;
                if (ray_triangle_intersect(ray, v0, v1, v2, t, u, v)) {
                    if (t < hit.t) {
                        hit.t = t;
                        hit.u = u;
                        hit.v = v;
                        hit.prim_id = prim_idx;
                        hit.hit = true;
                        ray.tmax = t; // tighten ray
                    }
                }
            }
        } else {
            // Interior node: push children (far child first for front-to-back)
            float d_left = ray_aabb_intersect_dist(ray, 
                bvh_nodes[node.left_child].aabb_min.xyz,
                bvh_nodes[node.left_child].aabb_max.xyz);
            float d_right = ray_aabb_intersect_dist(ray,
                bvh_nodes[node.right_child].aabb_min.xyz,
                bvh_nodes[node.right_child].aabb_max.xyz);
            
            if (d_left < d_right) {
                if (d_right < 1e30 && stack_ptr < MAX_STACK_DEPTH)
                    stack[stack_ptr++] = node.right_child;
                if (d_left < 1e30 && stack_ptr < MAX_STACK_DEPTH)
                    stack[stack_ptr++] = node.left_child;
            } else {
                if (d_left < 1e30 && stack_ptr < MAX_STACK_DEPTH)
                    stack[stack_ptr++] = node.left_child;
                if (d_right < 1e30 && stack_ptr < MAX_STACK_DEPTH)
                    stack[stack_ptr++] = node.right_child;
            }
        }
    }
    
    return hit;
}

/* Shadow ray: any-hit traversal (early termination) */
bool traverse_bvh_any(Ray ray) {
    uint stack[MAX_STACK_DEPTH];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0u;
    
    while (stack_ptr > 0) {
        uint node_idx = stack[--stack_ptr];
        BVHNode node = bvh_nodes[node_idx];
        
        if (!ray_aabb_intersect(ray, node.aabb_min.xyz, node.aabb_max.xyz))
            continue;
        
        if ((node.flags & BVH_FLAG_LEAF) != 0u) {
            uint first = node.left_child;
            uint count = node.right_child;
            
            for (uint i = 0u; i < count; i++) {
                uint prim_idx = prim_indices[first + i];
                vec3 v0, v1, v2;
                fetch_triangle(prim_idx, v0, v1, v2);
                
                float t, u, v;
                if (ray_triangle_intersect(ray, v0, v1, v2, t, u, v)) {
                    return true; // any hit found
                }
            }
        } else {
            if (stack_ptr < MAX_STACK_DEPTH - 1) {
                stack[stack_ptr++] = node.left_child;
                stack[stack_ptr++] = node.right_child;
            }
        }
    }
    
    return false;
}

#endif // BVH_COMMON_GLSL
