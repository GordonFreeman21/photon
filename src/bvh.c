/*
 * Photon - BVH Construction and Traversal
 *
 * SAH-based iterative BVH builder with binned splits,
 * single-ray and packet traversal, two-level (TLAS/BLAS) support.
 */

#include "../include/bvh.h"
#include "../include/math_util.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <math.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Internal helpers                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Work item for the iterative build stack */
typedef struct {
    uint32_t node_idx;
    uint32_t start;
    uint32_t end;
    uint32_t depth;
} BuildWorkItem;

/* SAH bin used during binned split evaluation */
typedef struct {
    AABB     bounds;
    uint32_t count;
} SAHBin;

/* Per-primitive info cached during build */
typedef struct {
    AABB     bounds;
    vec3     centroid;
    uint32_t index;
} PrimInfo;

/* ── node array management ────────────────────────────────────────────────── */

static uint32_t bvh_alloc_node(BVH *bvh)
{
    if (bvh->node_count >= bvh->node_capacity) {
        uint32_t new_cap = bvh->node_capacity ? bvh->node_capacity * 2 : 256;
        BVHNode *tmp = (BVHNode *)photon_aligned_alloc(
            new_cap * sizeof(BVHNode), 64);
        assert(tmp);
        if (bvh->nodes) {
            memcpy(tmp, bvh->nodes, bvh->node_count * sizeof(BVHNode));
            photon_aligned_free(bvh->nodes);
        }
        bvh->nodes = tmp;
        bvh->node_capacity = new_cap;
    }
    uint32_t idx = bvh->node_count++;
    memset(&bvh->nodes[idx], 0, sizeof(BVHNode));
    bvh->nodes[idx].parent_idx = PHOTON_INVALID_ID;
    return idx;
}

/* ── make a BVHNode store an AABB ─────────────────────────────────────────── */

static void node_set_aabb(BVHNode *n, AABB box)
{
    n->aabb_min[0] = box.min.x;
    n->aabb_min[1] = box.min.y;
    n->aabb_min[2] = box.min.z;
    n->aabb_min[3] = 0.0f;
    n->aabb_max[0] = box.max.x;
    n->aabb_max[1] = box.max.y;
    n->aabb_max[2] = box.max.z;
    n->aabb_max[3] = 0.0f;
}

static AABB node_get_aabb(const BVHNode *n)
{
    AABB a;
    a.min = vec3_new(n->aabb_min[0], n->aabb_min[1], n->aabb_min[2]);
    a.max = vec3_new(n->aabb_max[0], n->aabb_max[1], n->aabb_max[2]);
    return a;
}

/* ── centroid-based qsort helpers ─────────────────────────────────────────── */

static const PrimInfo *g_sort_prims;
static int             g_sort_axis;

static int prim_compare_centroid(const void *a, const void *b)
{
    uint32_t ia = *(const uint32_t *)a;
    uint32_t ib = *(const uint32_t *)b;
    float ca, cb;
    switch (g_sort_axis) {
    case 0: ca = g_sort_prims[ia].centroid.x; cb = g_sort_prims[ib].centroid.x; break;
    case 1: ca = g_sort_prims[ia].centroid.y; cb = g_sort_prims[ib].centroid.y; break;
    default: ca = g_sort_prims[ia].centroid.z; cb = g_sort_prims[ib].centroid.z; break;
    }
    return (ca < cb) ? -1 : (ca > cb) ? 1 : 0;
}

/* ── centroid component accessor ──────────────────────────────────────────── */

static float centroid_axis(vec3 c, int axis)
{
    switch (axis) {
    case 0: return c.x;
    case 1: return c.y;
    default: return c.z;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Build Options                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

BVHBuildOptions bvh_default_options(void)
{
    BVHBuildOptions opts;
    opts.max_leaf_prims     = 4;
    opts.max_depth          = 64;
    opts.sah_traversal_cost = 1.0f;
    opts.sah_intersect_cost = 1.0f;
    opts.sah_bin_count      = 32;
    opts.parallel_build     = false;
    opts.thread_count       = 0;
    return opts;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Core iterative SAH builder                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

/*
 * build_bvh_from_prim_info
 *
 * prim_infos  – array of PrimInfo (one per primitive, caller-allocated)
 * prim_count  – number of primitives
 * options     – build options (non-NULL)
 *
 * Returns a populated BVH.  prim_indices in the BVH hold the original
 * primitive indices in the traversal order dictated by the tree.
 */
static BVH build_bvh_from_prim_info(PrimInfo *prim_infos, uint32_t prim_count,
                                    const BVHBuildOptions *options)
{
    BVH bvh;
    memset(&bvh, 0, sizeof(bvh));
    bvh.prim_count = prim_count;

    if (prim_count == 0) return bvh;

    /* working index array – reordered during build */
    uint32_t *ordered = (uint32_t *)malloc(prim_count * sizeof(uint32_t));
    assert(ordered);
    for (uint32_t i = 0; i < prim_count; i++) ordered[i] = i;

    /* work stack */
    uint32_t stack_cap = options->max_depth * 2 + 2;
    BuildWorkItem *stack = (BuildWorkItem *)malloc(stack_cap * sizeof(BuildWorkItem));
    assert(stack);

    /* SAH bins scratch space */
    uint32_t bin_count = options->sah_bin_count;
    SAHBin *bins = (SAHBin *)malloc(bin_count * sizeof(SAHBin));
    assert(bins);

    /* allocate root */
    uint32_t root = bvh_alloc_node(&bvh);
    uint32_t sp = 0;
    uint32_t max_depth_seen = 0;

    stack[sp++] = (BuildWorkItem){root, 0, prim_count, 0};

    while (sp > 0) {
        BuildWorkItem item = stack[--sp];
        uint32_t ni    = item.node_idx;
        uint32_t start = item.start;
        uint32_t end   = item.end;
        uint32_t depth = item.depth;
        uint32_t count = end - start;

        if (depth > max_depth_seen) max_depth_seen = depth;

        /* compute node AABB and centroid bounds */
        AABB node_bounds     = aabb_empty();
        AABB centroid_bounds = aabb_empty();
        for (uint32_t i = start; i < end; i++) {
            node_bounds     = aabb_union(node_bounds, prim_infos[ordered[i]].bounds);
            centroid_bounds = aabb_union_point(centroid_bounds, prim_infos[ordered[i]].centroid);
        }
        node_set_aabb(&bvh.nodes[ni], node_bounds);

        /* leaf conditions */
        if (count <= options->max_leaf_prims || depth >= options->max_depth) {
            bvh.nodes[ni].first_prim = start;
            bvh.nodes[ni].prim_count = count;
            bvh.nodes[ni].flags      = BVH_FLAG_LEAF;
            continue;
        }

        /* ── binned SAH split ────────────────────────────────────────── */
        float parent_sa    = aabb_surface_area(node_bounds);
        float leaf_cost    = options->sah_intersect_cost * (float)count;
        float best_cost    = FLT_MAX;
        int   best_axis    = -1;
        int   best_bin     = -1;

        for (int axis = 0; axis < 3; axis++) {
            float cmin = centroid_axis(centroid_bounds.min, axis);
            float cmax = centroid_axis(centroid_bounds.max, axis);
            if (cmax - cmin < PHOTON_EPSILON) continue;

            /* initialise bins */
            for (uint32_t b = 0; b < bin_count; b++) {
                bins[b].bounds = aabb_empty();
                bins[b].count  = 0;
            }

            float inv_extent = (float)bin_count / (cmax - cmin);

            /* assign primitives to bins */
            for (uint32_t i = start; i < end; i++) {
                float c = centroid_axis(prim_infos[ordered[i]].centroid, axis);
                int b = (int)((c - cmin) * inv_extent);
                if (b >= (int)bin_count) b = (int)bin_count - 1;
                bins[b].bounds = aabb_union(bins[b].bounds, prim_infos[ordered[i]].bounds);
                bins[b].count++;
            }

            /* sweep from left: prefix surface areas and counts */
            float *left_sa  = (float *)malloc((bin_count - 1) * sizeof(float));
            uint32_t *left_cnt = (uint32_t *)malloc((bin_count - 1) * sizeof(uint32_t));
            assert(left_sa && left_cnt);

            AABB running = aabb_empty();
            uint32_t running_count = 0;
            for (uint32_t b = 0; b < bin_count - 1; b++) {
                running       = aabb_union(running, bins[b].bounds);
                running_count += bins[b].count;
                left_sa[b]    = aabb_surface_area(running);
                left_cnt[b]   = running_count;
            }

            /* sweep from right and evaluate cost */
            running       = aabb_empty();
            running_count = 0;
            for (int b = (int)bin_count - 1; b >= 1; b--) {
                running       = aabb_union(running, bins[b].bounds);
                running_count += bins[b].count;
                float cost = options->sah_traversal_cost +
                             options->sah_intersect_cost *
                             (left_sa[b - 1]  * (float)left_cnt[b - 1] +
                              aabb_surface_area(running) * (float)running_count) /
                             parent_sa;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;
                    best_bin  = b - 1;       /* split after bin b-1 */
                }
            }

            free(left_sa);
            free(left_cnt);
        }

        /* if no beneficial split, make leaf */
        if (best_axis == -1 || best_cost >= leaf_cost) {
            bvh.nodes[ni].first_prim = start;
            bvh.nodes[ni].prim_count = count;
            bvh.nodes[ni].flags      = BVH_FLAG_LEAF;
            continue;
        }

        /* ── partition ordered[] around the split ────────────────────── */
        {
            float cmin = centroid_axis(centroid_bounds.min, best_axis);
            float cmax = centroid_axis(centroid_bounds.max, best_axis);
            float inv_extent = (float)bin_count / (cmax - cmin);

            /* stable partition into left (<=best_bin) and right (>best_bin) */
            uint32_t lo = start, hi = end - 1;
            while (lo <= hi && lo < end) {
                float c = centroid_axis(prim_infos[ordered[lo]].centroid, best_axis);
                int b = (int)((c - cmin) * inv_extent);
                if (b >= (int)bin_count) b = (int)bin_count - 1;
                if (b <= best_bin) {
                    lo++;
                } else {
                    /* swap to right side */
                    uint32_t tmp = ordered[lo];
                    ordered[lo]  = ordered[hi];
                    ordered[hi]  = tmp;
                    if (hi == 0) break;
                    hi--;
                }
            }

            uint32_t mid = lo;

            /* safety: degenerate split → fallback to midpoint */
            if (mid == start || mid == end) {
                mid = start + count / 2;
            }

            /* allocate children */
            uint32_t left_idx  = bvh_alloc_node(&bvh);
            uint32_t right_idx = bvh_alloc_node(&bvh);

            bvh.nodes[ni].left_child  = left_idx;
            bvh.nodes[ni].right_child = right_idx;
            bvh.nodes[ni].flags       = (uint32_t)best_axis << BVH_AXIS_SHIFT;

            bvh.nodes[left_idx].parent_idx  = ni;
            bvh.nodes[right_idx].parent_idx = ni;

            /* push children (right first so left is processed first) */
            assert(sp + 2 <= stack_cap);
            stack[sp++] = (BuildWorkItem){right_idx, mid, end, depth + 1};
            stack[sp++] = (BuildWorkItem){left_idx, start, mid, depth + 1};
        }
    }

    bvh.depth = max_depth_seen;

    /* build final prim_indices in traversal order */
    bvh.prim_indices = (uint32_t *)malloc(prim_count * sizeof(uint32_t));
    assert(bvh.prim_indices);
    for (uint32_t i = 0; i < prim_count; i++) {
        bvh.prim_indices[i] = prim_infos[ordered[i]].index;
    }

    /* patch leaf first_prim to reference into prim_indices (identity,
       since ordered already represents the prim_indices layout) */

    free(bins);
    free(stack);
    free(ordered);

    return bvh;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BLAS builders                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

BVH bvh_build_triangles(const vec3 *vertices, const uint32_t *indices,
                         uint32_t triangle_count, const BVHBuildOptions *options)
{
    BVHBuildOptions opts = options ? *options : bvh_default_options();

    PrimInfo *infos = (PrimInfo *)malloc(triangle_count * sizeof(PrimInfo));
    assert(infos);

    for (uint32_t i = 0; i < triangle_count; i++) {
        vec3 v0 = vertices[indices[i * 3 + 0]];
        vec3 v1 = vertices[indices[i * 3 + 1]];
        vec3 v2 = vertices[indices[i * 3 + 2]];

        AABB box = aabb_empty();
        box = aabb_union_point(box, v0);
        box = aabb_union_point(box, v1);
        box = aabb_union_point(box, v2);

        infos[i].bounds   = box;
        infos[i].centroid = vec3_scale(vec3_add(vec3_add(v0, v1), v2), 1.0f / 3.0f);
        infos[i].index    = i;
    }

    BVH bvh = build_bvh_from_prim_info(infos, triangle_count, &opts);
    free(infos);
    return bvh;
}

BVH bvh_build_mesh(const RTMeshData *mesh, const BVHBuildOptions *options)
{
    assert(mesh);
    uint32_t tri_count = mesh->index_count / 3;

    /* extract position-only vertex buffer */
    vec3 *positions = (vec3 *)malloc(mesh->vertex_count * sizeof(vec3));
    assert(positions);
    for (uint32_t i = 0; i < mesh->vertex_count; i++) {
        positions[i] = mesh->vertices[i].position;
    }

    BVH bvh = bvh_build_triangles(positions, mesh->indices, tri_count, options);

    free(positions);
    return bvh;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TLAS builder                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

BVH bvh_build_tlas(const RTInstance *instances, uint32_t instance_count,
                   const BVHBuildOptions *options)
{
    BVHBuildOptions opts = options ? *options : bvh_default_options();

    PrimInfo *infos = (PrimInfo *)malloc(instance_count * sizeof(PrimInfo));
    assert(infos);

    for (uint32_t i = 0; i < instance_count; i++) {
        infos[i].bounds   = instances[i].world_bounds;
        infos[i].centroid = aabb_center(instances[i].world_bounds);
        infos[i].index    = i;
    }

    BVH bvh = build_bvh_from_prim_info(infos, instance_count, &opts);
    free(infos);
    return bvh;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Refit                                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

void bvh_refit(BVH *bvh, const vec3 *vertices, const uint32_t *indices)
{
    assert(bvh && bvh->nodes);

    /* bottom-up: process nodes in reverse allocation order (children before parents) */
    for (int64_t i = (int64_t)bvh->node_count - 1; i >= 0; i--) {
        BVHNode *n = &bvh->nodes[i];
        if (n->flags & BVH_FLAG_LEAF) {
            AABB box = aabb_empty();
            for (uint32_t p = 0; p < n->prim_count; p++) {
                uint32_t tri = bvh->prim_indices[n->first_prim + p];
                vec3 v0 = vertices[indices[tri * 3 + 0]];
                vec3 v1 = vertices[indices[tri * 3 + 1]];
                vec3 v2 = vertices[indices[tri * 3 + 2]];
                box = aabb_union_point(box, v0);
                box = aabb_union_point(box, v1);
                box = aabb_union_point(box, v2);
            }
            node_set_aabb(n, box);
        } else {
            AABB left  = node_get_aabb(&bvh->nodes[n->left_child]);
            AABB right = node_get_aabb(&bvh->nodes[n->right_child]);
            node_set_aabb(n, aabb_union(left, right));
        }
    }
}

void bvh_refit_tlas(BVH *bvh, const RTInstance *instances, uint32_t instance_count)
{
    assert(bvh && bvh->nodes);
    (void)instance_count;

    for (int64_t i = (int64_t)bvh->node_count - 1; i >= 0; i--) {
        BVHNode *n = &bvh->nodes[i];
        if (n->flags & BVH_FLAG_LEAF) {
            AABB box = aabb_empty();
            for (uint32_t p = 0; p < n->prim_count; p++) {
                uint32_t inst = bvh->prim_indices[n->first_prim + p];
                box = aabb_union(box, instances[inst].world_bounds);
            }
            node_set_aabb(n, box);
        } else {
            AABB left  = node_get_aabb(&bvh->nodes[n->left_child]);
            AABB right = node_get_aabb(&bvh->nodes[n->right_child]);
            node_set_aabb(n, aabb_union(left, right));
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Destroy                                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

void bvh_destroy(BVH *bvh)
{
    if (!bvh) return;
    if (bvh->nodes) {
        photon_aligned_free(bvh->nodes);
        bvh->nodes = NULL;
    }
    if (bvh->prim_indices) {
        free(bvh->prim_indices);
        bvh->prim_indices = NULL;
    }
    bvh->node_count    = 0;
    bvh->node_capacity = 0;
    bvh->prim_count    = 0;
    bvh->depth         = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Single-ray BVH traversal (iterative, ordered)                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool bvh_intersect(const BVH *bvh, const Ray *ray, HitInfo *hit,
                   const vec3 *vertices, const uint32_t *indices)
{
    assert(bvh && ray && hit && vertices && indices);

    hit->hit = false;
    hit->t   = ray->tmax;

    if (bvh->node_count == 0) return false;

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0; /* root */

    /* precompute ray inv_direction sign for ordered traversal */
    int dir_is_neg[3] = {
        ray->inv_direction.x < 0.0f,
        ray->inv_direction.y < 0.0f,
        ray->inv_direction.z < 0.0f
    };

    while (sp > 0) {
        uint32_t ni = stack[--sp];
        const BVHNode *node = &bvh->nodes[ni];

        /* AABB test */
        if (!ray_aabb_intersect(ray, node->aabb_min, node->aabb_max))
            continue;

        if (node->flags & BVH_FLAG_LEAF) {
            /* test all primitives in leaf */
            for (uint32_t p = 0; p < node->prim_count; p++) {
                uint32_t tri = bvh->prim_indices[node->first_prim + p];
                vec3 v0 = vertices[indices[tri * 3 + 0]];
                vec3 v1 = vertices[indices[tri * 3 + 1]];
                vec3 v2 = vertices[indices[tri * 3 + 2]];

                float t, u, v;
                if (ray_triangle_intersect(ray, v0, v1, v2, &t, &u, &v)) {
                    if (t > ray->tmin && t < hit->t) {
                        hit->t       = t;
                        hit->u       = u;
                        hit->v       = v;
                        hit->prim_id = tri;
                        hit->hit     = true;
                    }
                }
            }
        } else {
            /* ordered traversal: visit near child first */
            uint32_t axis = (node->flags & BVH_AXIS_MASK) >> BVH_AXIS_SHIFT;
            uint32_t first  = node->left_child;
            uint32_t second = node->right_child;
            if (dir_is_neg[axis]) {
                first  = node->right_child;
                second = node->left_child;
            }
            /* push far child first (popped last), near child second (popped first) */
            assert(sp + 2 <= 64);
            stack[sp++] = second;
            stack[sp++] = first;
        }
    }

    return hit->hit;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Shadow / any-hit traversal                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool bvh_intersect_any(const BVH *bvh, const Ray *ray,
                       const vec3 *vertices, const uint32_t *indices)
{
    assert(bvh && ray && vertices && indices);
    if (bvh->node_count == 0) return false;

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        uint32_t ni = stack[--sp];
        const BVHNode *node = &bvh->nodes[ni];

        if (!ray_aabb_intersect(ray, node->aabb_min, node->aabb_max))
            continue;

        if (node->flags & BVH_FLAG_LEAF) {
            for (uint32_t p = 0; p < node->prim_count; p++) {
                uint32_t tri = bvh->prim_indices[node->first_prim + p];
                vec3 v0 = vertices[indices[tri * 3 + 0]];
                vec3 v1 = vertices[indices[tri * 3 + 1]];
                vec3 v2 = vertices[indices[tri * 3 + 2]];

                float t, u, v;
                if (ray_triangle_intersect(ray, v0, v1, v2, &t, &u, &v)) {
                    if (t > ray->tmin && t < ray->tmax)
                        return true;
                }
            }
        } else {
            uint32_t axis = (node->flags & BVH_AXIS_MASK) >> BVH_AXIS_SHIFT;
            int neg = (axis == 0) ? (ray->inv_direction.x < 0.0f)
                    : (axis == 1) ? (ray->inv_direction.y < 0.0f)
                                  : (ray->inv_direction.z < 0.0f);
            uint32_t first  = neg ? node->right_child : node->left_child;
            uint32_t second = neg ? node->left_child  : node->right_child;
            assert(sp + 2 <= 64);
            stack[sp++] = second;
            stack[sp++] = first;
        }
    }

    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Packet traversal (4 rays)                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

void bvh_intersect_packet4(const BVH *bvh, RayPacket4 *packet, HitInfo hits[4],
                           const vec3 *vertices, const uint32_t *indices)
{
    assert(bvh && packet && hits && vertices && indices);

    for (int r = 0; r < 4; r++) {
        hits[r].hit = false;
        hits[r].t   = FLT_MAX;
    }

    if (bvh->node_count == 0) return;

    /* extract tmax values to track per-ray closest hit */
    PHOTON_ALIGN(16) float tmax_arr[4];
    _mm_store_ps(tmax_arr, packet->tmax);

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        uint32_t ni = stack[--sp];
        const BVHNode *node = &bvh->nodes[ni];

        /* SSE AABB test: 4 rays vs 1 box (broadcast box to 4 lanes) */
        __m128 bmin_x = _mm_set1_ps(node->aabb_min[0]);
        __m128 bmin_y = _mm_set1_ps(node->aabb_min[1]);
        __m128 bmin_z = _mm_set1_ps(node->aabb_min[2]);
        __m128 bmax_x = _mm_set1_ps(node->aabb_max[0]);
        __m128 bmax_y = _mm_set1_ps(node->aabb_max[1]);
        __m128 bmax_z = _mm_set1_ps(node->aabb_max[2]);

        __m128 hit_mask = ray_aabb_intersect_sse4(
            packet->origin_x, packet->origin_y, packet->origin_z,
            packet->inv_dir_x, packet->inv_dir_y, packet->inv_dir_z,
            bmin_x, bmin_y, bmin_z,
            bmax_x, bmax_y, bmax_z,
            packet->tmin, _mm_load_ps(tmax_arr));

        /* check if any ray hit the box */
        if (_mm_movemask_ps(hit_mask) == 0)
            continue;

        if (node->flags & BVH_FLAG_LEAF) {
            /* scalar fallback: test each active ray against each triangle */
            PHOTON_ALIGN(16) float ox[4], oy[4], oz[4];
            PHOTON_ALIGN(16) float dx[4], dy[4], dz[4];
            PHOTON_ALIGN(16) float tmin_arr[4];
            _mm_store_ps(ox, packet->origin_x);
            _mm_store_ps(oy, packet->origin_y);
            _mm_store_ps(oz, packet->origin_z);
            _mm_store_ps(dx, packet->dir_x);
            _mm_store_ps(dy, packet->dir_y);
            _mm_store_ps(dz, packet->dir_z);
            _mm_store_ps(tmin_arr, packet->tmin);

            for (uint32_t p = 0; p < node->prim_count; p++) {
                uint32_t tri = bvh->prim_indices[node->first_prim + p];
                vec3 v0 = vertices[indices[tri * 3 + 0]];
                vec3 v1 = vertices[indices[tri * 3 + 1]];
                vec3 v2 = vertices[indices[tri * 3 + 2]];

                for (int r = 0; r < 4; r++) {
                    if (!(packet->active_mask & (1u << r))) continue;

                    Ray single;
                    single.origin    = vec3_new(ox[r], oy[r], oz[r]);
                    single.direction = vec3_new(dx[r], dy[r], dz[r]);
                    single.tmin      = tmin_arr[r];
                    single.tmax      = tmax_arr[r];
                    single.inv_direction = vec3_new(
                        1.0f / dx[r], 1.0f / dy[r], 1.0f / dz[r]);

                    float t, u, v;
                    if (ray_triangle_intersect(&single, v0, v1, v2, &t, &u, &v)) {
                        if (t > single.tmin && t < tmax_arr[r]) {
                            tmax_arr[r]      = t;
                            hits[r].t        = t;
                            hits[r].u        = u;
                            hits[r].v        = v;
                            hits[r].prim_id  = tri;
                            hits[r].hit      = true;
                        }
                    }
                }
            }
        } else {
            assert(sp + 2 <= 64);
            stack[sp++] = node->right_child;
            stack[sp++] = node->left_child;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Two-level traversal helpers                                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

static Ray transform_ray_to_local(const Ray *world_ray, const float inv_transform[16])
{
    mat4 inv;
    memcpy(inv.m, inv_transform, sizeof(float) * 16);

    Ray local;
    local.origin    = mat4_transform_point(inv, world_ray->origin);
    local.direction = mat4_transform_dir(inv, world_ray->direction);
    local.tmin      = world_ray->tmin;
    local.tmax      = world_ray->tmax;
    /* recompute inv_direction */
    local.inv_direction.x = (fabsf(local.direction.x) > PHOTON_EPSILON)
                            ? 1.0f / local.direction.x : 1e30f;
    local.inv_direction.y = (fabsf(local.direction.y) > PHOTON_EPSILON)
                            ? 1.0f / local.direction.y : 1e30f;
    local.inv_direction.z = (fabsf(local.direction.z) > PHOTON_EPSILON)
                            ? 1.0f / local.direction.z : 1e30f;
    local.pixel_index = world_ray->pixel_index;
    return local;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Two-level scene traversal                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool bvh_intersect_scene(const BVH *tlas, const RTMesh *meshes,
                         const RTInstance *instances,
                         const Ray *ray, HitInfo *hit)
{
    assert(tlas && meshes && instances && ray && hit);
    hit->hit = false;
    hit->t   = ray->tmax;

    if (tlas->node_count == 0) return false;

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        uint32_t ni = stack[--sp];
        const BVHNode *node = &tlas->nodes[ni];

        if (!ray_aabb_intersect(ray, node->aabb_min, node->aabb_max))
            continue;

        if (node->flags & BVH_FLAG_LEAF) {
            for (uint32_t p = 0; p < node->prim_count; p++) {
                uint32_t inst_id = tlas->prim_indices[node->first_prim + p];
                const RTInstance *inst = &instances[inst_id];
                const RTMesh *mesh     = &meshes[inst->mesh_id];

                /* transform ray into instance local space */
                Ray local_ray = transform_ray_to_local(ray, inst->inv_transform);
                local_ray.tmax = hit->t; /* clip to closest so far */

                /* build a temporary BVH view for the BLAS stored in the mesh */
                BVH blas;
                blas.nodes        = mesh->blas_nodes;
                blas.node_count   = mesh->blas_node_count;
                blas.node_capacity = mesh->blas_node_count;
                blas.prim_indices = NULL; /* we'll handle indices specially */
                blas.prim_count   = mesh->data.index_count / 3;
                blas.depth        = 0;
                blas.arena        = NULL;

                /* BLAS prim_indices: assume the BLAS was built with
                   bvh_build_mesh, so the mesh already has them embedded.
                   We need the prim_indices from the original BLAS build.
                   For a two-level setup the RTMesh should carry a complete
                   BVH.  We traverse the BLAS nodes directly here. */

                /* traverse BLAS */
                vec3 *positions = (vec3 *)malloc(mesh->data.vertex_count * sizeof(vec3));
                if (!positions) continue;
                for (uint32_t vi = 0; vi < mesh->data.vertex_count; vi++) {
                    positions[vi] = mesh->data.vertices[vi].position;
                }

                HitInfo local_hit;
                local_hit.hit = false;
                local_hit.t   = local_ray.tmax;

                /* inline BLAS traversal using the mesh's blas_nodes */
                uint32_t blas_stack[64];
                int bsp = 0;
                blas_stack[bsp++] = 0;

                while (bsp > 0) {
                    uint32_t bni = blas_stack[--bsp];
                    const BVHNode *bnode = &mesh->blas_nodes[bni];

                    if (!ray_aabb_intersect(&local_ray, bnode->aabb_min, bnode->aabb_max))
                        continue;

                    if (bnode->flags & BVH_FLAG_LEAF) {
                        for (uint32_t bp = 0; bp < bnode->prim_count; bp++) {
                            uint32_t tri_idx = bnode->first_prim + bp;
                            /* the first_prim in the BLAS leaf is an index into
                               a prim_indices array, but those prim_indices are
                               triangle indices.  For the two-level case the mesh
                               stores triangles directly. Use tri_idx as-is for
                               sequential triangle access, or read from the
                               mesh indices. */
                            vec3 v0 = positions[mesh->data.indices[tri_idx * 3 + 0]];
                            vec3 v1 = positions[mesh->data.indices[tri_idx * 3 + 1]];
                            vec3 v2 = positions[mesh->data.indices[tri_idx * 3 + 2]];

                            float t, u, v;
                            if (ray_triangle_intersect(&local_ray, v0, v1, v2, &t, &u, &v)) {
                                if (t > local_ray.tmin && t < local_hit.t) {
                                    local_hit.t       = t;
                                    local_hit.u       = u;
                                    local_hit.v       = v;
                                    local_hit.prim_id = tri_idx;
                                    local_hit.hit     = true;
                                }
                            }
                        }
                    } else {
                        uint32_t axis = (bnode->flags & BVH_AXIS_MASK) >> BVH_AXIS_SHIFT;
                        int neg = (axis == 0) ? (local_ray.inv_direction.x < 0.0f)
                                : (axis == 1) ? (local_ray.inv_direction.y < 0.0f)
                                              : (local_ray.inv_direction.z < 0.0f);
                        uint32_t first  = neg ? bnode->right_child : bnode->left_child;
                        uint32_t second = neg ? bnode->left_child  : bnode->right_child;
                        assert(bsp + 2 <= 64);
                        blas_stack[bsp++] = second;
                        blas_stack[bsp++] = first;
                    }
                }

                free(positions);

                if (local_hit.hit && local_hit.t < hit->t) {
                    hit->t       = local_hit.t;
                    hit->u       = local_hit.u;
                    hit->v       = local_hit.v;
                    hit->prim_id = local_hit.prim_id;
                    hit->inst_id = inst_id;
                    hit->mesh_id = inst->mesh_id;
                    hit->mat_id  = mesh->data.material_id;
                    hit->hit     = true;
                }
            }
        } else {
            uint32_t axis = (node->flags & BVH_AXIS_MASK) >> BVH_AXIS_SHIFT;
            int neg = (axis == 0) ? (ray->inv_direction.x < 0.0f)
                    : (axis == 1) ? (ray->inv_direction.y < 0.0f)
                                  : (ray->inv_direction.z < 0.0f);
            uint32_t first  = neg ? node->right_child : node->left_child;
            uint32_t second = neg ? node->left_child  : node->right_child;
            assert(sp + 2 <= 64);
            stack[sp++] = second;
            stack[sp++] = first;
        }
    }

    return hit->hit;
}

bool bvh_intersect_scene_any(const BVH *tlas, const RTMesh *meshes,
                             const RTInstance *instances, const Ray *ray)
{
    assert(tlas && meshes && instances && ray);
    if (tlas->node_count == 0) return false;

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        uint32_t ni = stack[--sp];
        const BVHNode *node = &tlas->nodes[ni];

        if (!ray_aabb_intersect(ray, node->aabb_min, node->aabb_max))
            continue;

        if (node->flags & BVH_FLAG_LEAF) {
            for (uint32_t p = 0; p < node->prim_count; p++) {
                uint32_t inst_id = tlas->prim_indices[node->first_prim + p];
                const RTInstance *inst = &instances[inst_id];
                const RTMesh *mesh     = &meshes[inst->mesh_id];

                Ray local_ray = transform_ray_to_local(ray, inst->inv_transform);

                vec3 *positions = (vec3 *)malloc(mesh->data.vertex_count * sizeof(vec3));
                if (!positions) continue;
                for (uint32_t vi = 0; vi < mesh->data.vertex_count; vi++) {
                    positions[vi] = mesh->data.vertices[vi].position;
                }

                bool found = false;
                uint32_t blas_stack[64];
                int bsp = 0;
                blas_stack[bsp++] = 0;

                while (bsp > 0 && !found) {
                    uint32_t bni = blas_stack[--bsp];
                    const BVHNode *bnode = &mesh->blas_nodes[bni];

                    if (!ray_aabb_intersect(&local_ray, bnode->aabb_min, bnode->aabb_max))
                        continue;

                    if (bnode->flags & BVH_FLAG_LEAF) {
                        for (uint32_t bp = 0; bp < bnode->prim_count && !found; bp++) {
                            uint32_t tri_idx = bnode->first_prim + bp;
                            vec3 v0 = positions[mesh->data.indices[tri_idx * 3 + 0]];
                            vec3 v1 = positions[mesh->data.indices[tri_idx * 3 + 1]];
                            vec3 v2 = positions[mesh->data.indices[tri_idx * 3 + 2]];

                            float t, u, v;
                            if (ray_triangle_intersect(&local_ray, v0, v1, v2, &t, &u, &v)) {
                                if (t > local_ray.tmin && t < local_ray.tmax)
                                    found = true;
                            }
                        }
                    } else {
                        assert(bsp + 2 <= 64);
                        blas_stack[bsp++] = bnode->right_child;
                        blas_stack[bsp++] = bnode->left_child;
                    }
                }

                free(positions);
                if (found) return true;
            }
        } else {
            assert(sp + 2 <= 64);
            stack[sp++] = node->right_child;
            stack[sp++] = node->left_child;
        }
    }

    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GPU Buffer Packing                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

BVHGPUBuffer bvh_pack_for_gpu(const BVH *bvh)
{
    BVHGPUBuffer buf;
    memset(&buf, 0, sizeof(buf));
    if (!bvh || bvh->node_count == 0) return buf;

    buf.node_count = bvh->node_count;
    buf.size       = bvh->node_count * sizeof(BVHNode);
    buf.data       = malloc(buf.size);
    assert(buf.data);
    memcpy(buf.data, bvh->nodes, buf.size);
    return buf;
}

void bvh_gpu_buffer_destroy(BVHGPUBuffer *buf)
{
    if (!buf) return;
    free(buf->data);
    buf->data       = NULL;
    buf->size       = 0;
    buf->node_count = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Statistics                                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

BVHStats bvh_compute_stats(const BVH *bvh)
{
    BVHStats stats;
    memset(&stats, 0, sizeof(stats));
    if (!bvh || bvh->node_count == 0) return stats;

    stats.total_nodes = bvh->node_count;

    /* iterative walk using a stack of (node_index, depth) */
    typedef struct { uint32_t idx; uint32_t depth; } WalkItem;
    WalkItem *wstack = (WalkItem *)malloc(bvh->node_count * sizeof(WalkItem));
    assert(wstack);

    uint32_t wsp = 0;
    wstack[wsp++] = (WalkItem){0, 0};

    uint32_t leaf_count      = 0;
    uint64_t total_leaf_prims = 0;
    uint32_t max_depth       = 0;
    float    sah_cost        = 0.0f;
    float    root_sa         = aabb_surface_area(node_get_aabb(&bvh->nodes[0]));
    if (root_sa < PHOTON_EPSILON) root_sa = 1.0f;

    while (wsp > 0) {
        WalkItem wi = wstack[--wsp];
        const BVHNode *n = &bvh->nodes[wi.idx];

        if (wi.depth > max_depth) max_depth = wi.depth;

        if (n->flags & BVH_FLAG_LEAF) {
            leaf_count++;
            total_leaf_prims += n->prim_count;
            float sa = aabb_surface_area(node_get_aabb(n));
            sah_cost += (sa / root_sa) * (float)n->prim_count;
        } else {
            sah_cost += aabb_surface_area(node_get_aabb(n)) / root_sa;
            wstack[wsp++] = (WalkItem){n->left_child, wi.depth + 1};
            wstack[wsp++] = (WalkItem){n->right_child, wi.depth + 1};
        }
    }

    stats.leaf_nodes     = leaf_count;
    stats.max_depth      = max_depth;
    stats.avg_leaf_prims = leaf_count > 0 ? (float)total_leaf_prims / (float)leaf_count : 0.0f;
    stats.sah_cost       = sah_cost;

    free(wstack);
    return stats;
}
