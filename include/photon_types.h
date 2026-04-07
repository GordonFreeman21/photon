/*
 * Photon - Adaptive Hybrid Ray Tracer for Low-End GPU Gaming
 * Core type definitions
 */
#ifndef PHOTON_TYPES_H
#define PHOTON_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <float.h>

#ifdef _MSC_VER
    #include <intrin.h>
    #define PHOTON_ALIGN(x) __declspec(align(x))
    #define PHOTON_INLINE __forceinline
    #define PHOTON_PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
    #include <immintrin.h>
    #define PHOTON_ALIGN(x) __attribute__((aligned(x)))
    #define PHOTON_INLINE static inline __attribute__((always_inline))
    #define PHOTON_PREFETCH(addr) __builtin_prefetch(addr)
#endif

#include <xmmintrin.h>
#include <smmintrin.h>
#include <immintrin.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Constants                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

#define PHOTON_MAX_MESHES       4096
#define PHOTON_MAX_INSTANCES    16384
#define PHOTON_MAX_MATERIALS    2048
#define PHOTON_MAX_LIGHTS       256
#define PHOTON_MAX_TEXTURES     4096
#define PHOTON_MAX_BVH_DEPTH    64
#define PHOTON_TILE_SIZE        16
#define PHOTON_MAX_TILE_X       120  /* 1920/16 */
#define PHOTON_MAX_TILE_Y       68   /* 1080/16 */
#define PHOTON_TILE_COUNT       (PHOTON_MAX_TILE_X * PHOTON_MAX_TILE_Y)
#define PHOTON_INVALID_ID       UINT32_MAX
#define PHOTON_PI               3.14159265358979323846f
#define PHOTON_EPSILON          1e-6f
#define PHOTON_RAY_MIN_T        0.001f
#define PHOTON_RAY_MAX_T        10000.0f

#define PHOTON_PROBE_CASCADE_COUNT  3
#define PHOTON_PROBE_GRID_DIM       16
#define PHOTON_PROBE_TOTAL          (PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_CASCADE_COUNT)
#define PHOTON_SH_COEFF_COUNT       9   /* L2 spherical harmonics */
#define PHOTON_FROXEL_X             160
#define PHOTON_FROXEL_Y             90
#define PHOTON_FROXEL_Z             64
#define PHOTON_FRAME_OVERLAP        3   /* triple-buffered */

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Basic Math Types                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3;
typedef struct { float x, y, z, w; } vec4;
typedef struct { float m[16]; } mat4;
typedef struct { vec3 min; vec3 max; } AABB;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Quality Presets                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    RT_QUALITY_POTATO = 0,
    RT_QUALITY_LOW,
    RT_QUALITY_MEDIUM,
    RT_QUALITY_HIGH,
    RT_QUALITY_ULTRA,
    RT_QUALITY_COUNT
} RTQualityPreset;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BVH Node (64-byte cache-line aligned)                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct PHOTON_ALIGN(64) {
    float aabb_min[4];  /* 16 bytes (4th component = padding for SSE) */
    float aabb_max[4];  /* 16 bytes */
    union {
        struct { uint32_t left_child, right_child; };   /* interior node */
        struct { uint32_t first_prim, prim_count; };    /* leaf node */
    };
    uint32_t parent_idx;
    uint32_t flags;     /* bit 0: leaf, bits 1-2: split axis */
} BVHNode;

#define BVH_FLAG_LEAF       0x01
#define BVH_AXIS_MASK       0x06
#define BVH_AXIS_SHIFT      1

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Structures                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec3 origin;
    float tmin;
    vec3 direction;
    float tmax;
    vec3 inv_direction;
    uint32_t pixel_index;
} Ray;

/* SoA ray packet for SIMD processing (4 rays) */
typedef struct {
    __m128 origin_x, origin_y, origin_z;
    __m128 dir_x, dir_y, dir_z;
    __m128 inv_dir_x, inv_dir_y, inv_dir_z;
    __m128 tmin, tmax;
    uint32_t pixel_index[4];
    uint32_t active_mask;
} RayPacket4;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Hit Information                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float t;            /* hit distance */
    float u, v;         /* barycentric coordinates */
    uint32_t prim_id;   /* triangle index */
    uint32_t inst_id;   /* instance index */
    uint32_t mesh_id;   /* mesh index */
    uint32_t mat_id;    /* material index */
    bool hit;
} HitInfo;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Triangle & Mesh Data                                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec3 v0, v1, v2;
} Triangle;

typedef struct {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
} Vertex;

typedef struct {
    Vertex*     vertices;
    uint32_t*   indices;
    uint32_t    vertex_count;
    uint32_t    index_count;
    uint32_t    material_id;
    AABB        bounds;
} RTMeshData;

typedef struct {
    RTMeshData  data;
    uint32_t    blas_root;      /* root node index in BVH */
    uint32_t    blas_node_count;
    BVHNode*    blas_nodes;
    bool        dirty;
} RTMesh;

typedef struct {
    uint32_t mesh_id;
    float    transform[16];     /* 4x4 row-major */
    float    inv_transform[16];
    AABB     world_bounds;
    uint32_t flags;
} RTInstance;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Materials (PBR metallic-roughness)                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec4    base_color;
    float   metallic;
    float   roughness;
    float   ior;
    float   emissive_strength;
    vec3    emissive_color;
    uint32_t albedo_tex;
    uint32_t normal_tex;
    uint32_t metallic_roughness_tex;
    uint32_t emissive_tex;
    uint32_t flags;         /* bit 0: alpha_test, bit 1: double_sided, etc. */
} RTMaterial;

#define MAT_FLAG_ALPHA_TEST     0x01
#define MAT_FLAG_DOUBLE_SIDED   0x02
#define MAT_FLAG_EMISSIVE       0x04
#define MAT_FLAG_MIRROR         0x08
#define MAT_FLAG_WATER          0x10

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Lights                                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT,
    LIGHT_SPOT,
    LIGHT_AREA
} LightType;

typedef struct {
    vec3    direction;
    vec3    color;
    float   intensity;
    float   angular_diameter;   /* for soft shadows */
} RTDirectionalLight;

typedef struct {
    vec3    position;
    vec3    color;
    float   intensity;
    float   radius;
    float   range;
} RTPointLight;

typedef struct {
    vec3    position;
    vec3    direction;
    vec3    color;
    float   intensity;
    float   inner_cone;
    float   outer_cone;
    float   range;
    uint32_t cookie_tex;        /* flashlight pattern */
} RTSpotLight;

typedef struct {
    LightType   type;
    uint32_t    id;
    union {
        RTDirectionalLight  directional;
        RTPointLight        point;
        RTSpotLight         spot;
    };
    bool active;
} RTLight;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Camera                                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec3    position;
    vec3    forward;
    vec3    up;
    vec3    right;
    float   fov_y;              /* radians */
    float   aspect_ratio;
    float   near_plane;
    float   far_plane;
    float   jitter_x, jitter_y; /* sub-pixel jitter for TAA */
    mat4    view;
    mat4    projection;
    mat4    view_projection;
    mat4    inv_view_projection;
    mat4    prev_view_projection;
} RTCamera;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Ray Budget Manager                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    total_ray_budget;
    uint32_t    rays_spent_this_frame;
    uint8_t     tile_priority[PHOTON_TILE_COUNT];
    float       tile_sample_multiplier[PHOTON_TILE_COUNT];
    uint64_t    frame_number;
    float       adaptive_alpha;
    float       target_frame_time_ms;
    float       last_frame_time_ms;
    int32_t     ray_debt;
    float       debt_payback_rate;
} RayBudgetManager;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Denoiser State                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float   color[3];
    float   variance;
    float   moment_1[3];
    float   moment_2[3];
    uint8_t sample_count;
} TemporalPixel;

typedef struct {
    uint32_t    width, height;
    uint32_t    atrous_iterations;
    float       sigma_normal;
    float       sigma_depth;
    float       sigma_luminance;
    float       temporal_blend_factor;
    float       firefly_threshold;
    TemporalPixel*  temporal_buffer;
    TemporalPixel*  temporal_buffer_prev;
} DenoiseState;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Irradiance Probes (DDGI-inspired)                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float       irradiance_sh[PHOTON_SH_COEFF_COUNT * 3]; /* RGB × 9 SH coefficients */
    float       visibility[64];     /* octahedral depth map 8×8 */
    float       world_pos[3];
    uint8_t     last_update_frame;
    uint8_t     update_priority;
    uint16_t    padding;
} IrradianceProbe;

typedef struct {
    IrradianceProbe*    probes;
    uint32_t            probe_count;
    float               cascade_spacing[PHOTON_PROBE_CASCADE_COUNT];
    vec3                grid_origin;
    uint32_t            grid_dim;
    float               hysteresis;
    uint32_t            rays_per_probe;
    uint32_t            probes_per_frame;
} ProbeGrid;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Volumetric Fog (Froxel)                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float density;
    float scattering_albedo[3];
    float emissive[3];
} Froxel;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Performance Stats                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float       frame_time_ms;
    float       gbuffer_time_ms;
    float       shadow_trace_time_ms;
    float       reflection_trace_time_ms;
    float       gi_trace_time_ms;
    float       denoise_time_ms;
    float       composite_time_ms;
    float       volumetric_time_ms;
    float       post_process_time_ms;
    uint32_t    rays_traced;
    uint32_t    triangles_tested;
    float       average_bvh_depth;
    uint32_t    active_probes;
    float       vram_usage_mb;
} RTFrameStats;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Renderer Configuration                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    width;
    uint32_t    height;
    bool        fullscreen;
    bool        vsync;
    RTQualityPreset quality;
    uint32_t    ray_budget;
    float       shadow_resolution_scale;
    float       reflection_resolution_scale;
    float       gi_resolution_scale;
    uint32_t    shadow_samples;
    uint32_t    reflection_samples;
    uint32_t    gi_samples;
    uint32_t    denoise_iterations;
    float       temporal_blend;
    bool        enable_reflections;
    bool        enable_gi;
    bool        enable_volumetrics;
    bool        enable_checkerboard;
    bool        adaptive_quality;
    const char* window_title;
} RTRendererDesc;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Debug Visualization Mode                                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    DEBUG_VIS_NONE = 0,
    DEBUG_VIS_BVH_DEPTH,
    DEBUG_VIS_RAY_HEATMAP,
    DEBUG_VIS_NORMALS,
    DEBUG_VIS_DEPTH,
    DEBUG_VIS_ROUGHNESS,
    DEBUG_VIS_METALLIC,
    DEBUG_VIS_MOTION_VECTORS,
    DEBUG_VIS_PROBE_LOCATIONS,
    DEBUG_VIS_DENOISE_WEIGHTS,
    DEBUG_VIS_WIREFRAME,
    DEBUG_VIS_COUNT
} DebugVisMode;

#endif /* PHOTON_TYPES_H */
