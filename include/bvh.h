/*
 * Photon - BVH Construction and Traversal
 */
#ifndef PHOTON_BVH_H
#define PHOTON_BVH_H

#include "photon_types.h"
#include "memory.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Build Options                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    max_leaf_prims;     /* max triangles per leaf (default: 4) */
    uint32_t    max_depth;          /* max tree depth (default: 64) */
    float       sah_traversal_cost; /* SAH traversal cost (default: 1.0) */
    float       sah_intersect_cost; /* SAH intersection cost (default: 1.0) */
    uint32_t    sah_bin_count;      /* number of SAH bins (default: 32) */
    bool        parallel_build;     /* use multithreaded build */
    uint32_t    thread_count;       /* 0 = auto-detect */
} BVHBuildOptions;

BVHBuildOptions bvh_default_options(void);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Structure                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    BVHNode*    nodes;
    uint32_t    node_count;
    uint32_t    node_capacity;
    uint32_t*   prim_indices;       /* remapped primitive indices */
    uint32_t    prim_count;
    uint32_t    depth;
    LinearAllocator* arena;         /* optional: external arena for allocation */
} BVH;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BLAS (Bottom-Level Acceleration Structure)                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Build BLAS from triangle soup */
BVH bvh_build_triangles(const vec3* vertices, const uint32_t* indices,
                        uint32_t triangle_count, const BVHBuildOptions* options);

/* Build BLAS from indexed mesh */
BVH bvh_build_mesh(const RTMeshData* mesh, const BVHBuildOptions* options);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TLAS (Top-Level Acceleration Structure)                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Build TLAS from instances with world-space AABBs */
BVH bvh_build_tlas(const RTInstance* instances, uint32_t instance_count,
                   const BVHBuildOptions* options);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Operations                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Refit BVH bottom-up (for animated objects, cheaper than rebuild) */
void bvh_refit(BVH* bvh, const vec3* vertices, const uint32_t* indices);

/* Refit TLAS from updated instance bounds */
void bvh_refit_tlas(BVH* bvh, const RTInstance* instances, uint32_t instance_count);

/* Destroy BVH and free memory */
void bvh_destroy(BVH* bvh);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Traversal                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Single ray traversal */
bool bvh_intersect(const BVH* bvh, const Ray* ray, HitInfo* hit,
                   const vec3* vertices, const uint32_t* indices);

/* Shadow ray (any-hit, early termination) */
bool bvh_intersect_any(const BVH* bvh, const Ray* ray,
                       const vec3* vertices, const uint32_t* indices);

/* Packet traversal (4 rays) */
void bvh_intersect_packet4(const BVH* bvh, RayPacket4* packet, HitInfo hits[4],
                           const vec3* vertices, const uint32_t* indices);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Two-Level Traversal (TLAS → BLAS)                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool bvh_intersect_scene(const BVH* tlas, const RTMesh* meshes,
                         const RTInstance* instances,
                         const Ray* ray, HitInfo* hit);

bool bvh_intersect_scene_any(const BVH* tlas, const RTMesh* meshes,
                             const RTInstance* instances, const Ray* ray);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GPU Buffer Packing                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Pack BVH into flat buffer suitable for GPU upload (SSBO layout) */
typedef struct {
    void*       data;
    size_t      size;
    uint32_t    node_count;
} BVHGPUBuffer;

BVHGPUBuffer bvh_pack_for_gpu(const BVH* bvh);
void bvh_gpu_buffer_destroy(BVHGPUBuffer* buf);

/* Statistics */
typedef struct {
    uint32_t    total_nodes;
    uint32_t    leaf_nodes;
    uint32_t    max_depth;
    float       avg_leaf_prims;
    float       sah_cost;
} BVHStats;

BVHStats bvh_compute_stats(const BVH* bvh);

#endif /* PHOTON_BVH_H */
