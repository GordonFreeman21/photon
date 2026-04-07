# AI Prompt: Adaptive Hybrid Ray Tracer for Low-End GPU Gaming (C Implementation)

---

## The Prompt

```
You are an expert graphics engineer specializing in real-time rendering optimization
and hybrid ray tracing pipelines. Your goal is to build a complete, production-grade
adaptive ray tracing/path tracing system in C that enables graphically demanding
games (like Alan Wake 2) to run on low-end hardware (GTX 1060 6GB, ~4.4 TFLOPS,
no RT cores).

This is NOT a toy project. This is a full rendering pipeline replacement/supplement.

Use PURE C (C17 standard) for maximum performance, portability, and game engine
compatibility. Think id Software's id Tech engine philosophy: tight, cache-friendly,
SIMD-optimized C code.

═══════════════════════════════════════════════════════════════════════════════
PHASE 1: CORE SOFTWARE RAY TRACER (CPU + GPU COMPUTE)
═══════════════════════════════════════════════════════════════════════════════

Build a hybrid CPU/GPU ray tracer with these components:

ACCELERATION STRUCTURES (CPU-side in C):
- Implement a BVH (Bounding Volume Hierarchy) builder with SAH (Surface Area
  Heuristic) using iterative construction (no recursion for stack safety)
- Use memory arenas/pools for BVH node allocation (reduce malloc overhead)
- Structure alignment: __attribute__((aligned(64))) for cache-line optimization
- Compact BVH nodes: 32-byte layout with 8-byte aligned AABBs, child indices
- Two-level acceleration: TLAS (top-level) for instances, BLAS (bottom-level)
  for mesh geometry
- Dynamic BVH refitting for animated objects (refit bottom-up without rebuild)
- Implement parallel BVH build using pthreads or OpenMP

BVH NODE LAYOUT (cache-optimized):
```c
typedef struct {
    float aabb_min[4]; // 16 bytes (4th component padding for SSE)
    float aabb_max[4]; // 16 bytes
    union {
        struct { uint32_t left_child, right_child; }; // interior node
        struct { uint32_t first_prim, prim_count; };  // leaf node
    };
    uint32_t parent_idx;
    uint32_t flags; // leaf bit, axis bits
} __attribute__((aligned(64))) BVHNode;
```

RAY-SCENE INTERSECTION (SIMD-optimized C):
- Use SSE/AVX intrinsics for ray-AABB tests (test 4 boxes simultaneously)
- Ray-triangle intersection: Möller-Trumbore with SIMD vectorization
- Stackless BVH traversal using parent pointers (GPU-friendly, deterministic memory)
- Implement ray packet tracing (4 or 8 coherent rays together) with SSE/AVX
- Support alpha testing with early-out texture lookups
- Structure-of-Arrays (SoA) layout for ray packets for better SIMD utilization

RAY STRUCTURE (SoA for packet tracing):
```c
typedef struct {
    __m128 origin_x, origin_y, origin_z;    // 4 rays
    __m128 dir_x, dir_y, dir_z;             // directions
    __m128 inv_dir_x, inv_dir_y, inv_dir_z; // precomputed 1/dir
    __m128 tmin, tmax;                      // ray intervals
    uint32_t pixel_index[4];                // which pixel each ray belongs to
} RayPacket4;
```

MATERIAL SYSTEM:
- Flat C structures for materials (no OOP overhead)
- PBR metallic-roughness workflow matching glTF standard
- Function pointers for BRDF evaluation to avoid branching in hot loops
- Texture system: mipmapped textures with bilinear/trilinear filtering
- Material IDs reference into global material array

GPU BUFFER UPLOAD:
- Pack BVH into flat buffer for GPU upload (Vulkan/OpenGL buffer objects)
- Vertex/index data in tightly packed formats (vec3 positions, packed normals)
- Material parameters in UBO/SSBO format
- Use persistent mapped buffers for low-latency updates

═══════════════════════════════════════════════════════════════════════════════
PHASE 2: ADAPTIVE QUALITY SYSTEM (THE KEY TO LOW-END HARDWARE)
═══════════════════════════════════════════════════════════════════════════════

This is what makes it run on a GTX 1060. Build an intelligent budget system:

RAY BUDGET MANAGER (C implementation):
- Global ray budget: target 2-8 million rays per frame for GTX 1060
- Maintain per-tile budget tracking (divide screen into 16x16 or 32x32 tiles)
- Priority heatmap: uint8_t per tile, updated each frame based on:
  * Luminance variance from previous frame
  * Edge detection from G-buffer normals/depth
  * Distance to screen center (peripheral vision gets fewer rays)
  * Motion vectors magnitude (moving areas need more temporal samples)
  * Material complexity flags (mirrors/water get priority)

```c
typedef struct {
    uint32_t total_ray_budget;      // e.g., 4,000,000
    uint32_t rays_spent_this_frame;
    uint8_t tile_priority[TILE_COUNT]; // 0-255 priority per tile
    float tile_sample_multiplier[TILE_COUNT]; // 0.25x to 2.0x
    uint64_t frame_number;
    float adaptive_alpha; // temporal accumulation weight
} RayBudgetManager;
```

- Implement feedback loop: if frame time exceeds target (16.67ms for 60fps),
  reduce ray budget by 10% next frame; if under budget, increase by 5%
- Debt system: allow borrowing rays from next frame for important events
  (player shooting, explosions) then pay back over 3-5 frames

RESOLUTION & SAMPLE SCALING:
- Checkerboard rendering: trace only even/odd pixels, reconstruct missing
- Implement with bit manipulation: `if ((x + y + frame_parity) & 1) trace_ray();`
- Variable rate: each tile gets 0.25x, 0.5x, 1x, or 2x native resolution
- Temporal accumulation buffer (floating point, high precision)
- Halton/Sobol low-discrepancy sequences for sample positions across frames
- Motion vector reprojection for temporal stability

LEVEL OF DETAIL FOR RAYS:
- Primary rays: RASTERIZE (don't ray trace visibility at all)
- Shadows: 1 ray per pixel to sun, 1-2 rays for area lights, denoise
- Reflections: only if roughness < 0.4, otherwise use SSR → cubemap fallback
- First diffuse bounce: quarter resolution, heavy denoise
- Second+ bounces: irradiance probe lookup (no tracing)
- AO: very short rays (world-space 1-5 units), eighth resolution

═══════════════════════════════════════════════════════════════════════════════
PHASE 3: HYBRID RASTERIZATION + RAY TRACING PIPELINE
═══════════════════════════════════════════════════════════════════════════════

Do NOT ray trace primary visibility. Use OpenGL/Vulkan rasterization:

RASTERIZATION PASS (C + OpenGL or Vulkan):
- Use OpenGL 4.5+ or Vulkan 1.2+
- Render G-buffer with multiple render targets (MRT):
  * Target 0: RGB albedo (8-bit), A unused
  * Target 1: RG normal (octahedral-encoded 16-bit), BA roughness+metallic
  * Target 2: RGB emissive (11-11-10 float), A flags
  * Target 3: RG motion vectors (16-bit float), B depth derivative, A mesh ID
  * Depth buffer: 32-bit float
- Use instanced rendering for repeated geometry
- Frustum culling and occlusion culling on CPU before submit
- Output is full-screen G-buffer at native or upscaled resolution

VULKAN COMPUTE RAY TRACING:
- After G-buffer, dispatch compute shaders for ray tracing passes
- Each compute shader processes one ray type (shadows, reflections, GI)
- Reads G-buffer as input textures
- Writes to separate output textures (shadow mask, reflection color, GI irradiance)
- BVH traversal entirely in compute shader (GLSL)

COMPUTE SHADER ORGANIZATION:
- Workgroup size: 8x8 or 16x16 threads (tile-aligned)
- Use shared memory to cache BVH nodes accessed by workgroup
- Wavefront path tracing: organize rays in queues, process in coherent batches
- Persistent threads: launch fixed number of workgroups, feed rays from buffer

GLSL BVH TRAVERSAL (stackless):
```glsl
layout(std430, binding = 0) readonly buffer BVHNodes {
    BVHNode nodes[];
};

bool intersect_bvh(Ray ray, out HitInfo hit) {
    uint node_idx = 0; // root
    while (node_idx != INVALID_IDX) {
        BVHNode node = nodes[node_idx];
        if (ray_aabb_intersect(ray, node.aabb_min, node.aabb_max)) {
            if (is_leaf(node)) {
                // test triangles, update hit, return
            } else {
                // traverse to left child first
                node_idx = node.left_child;
            }
        } else {
            // move to sibling or parent
            node_idx = get_next_node(node_idx); // rope-based or restart
        }
    }
}
```

FRAME PIPELINE:
1. CPU: Update scene (transforms, streaming), refit BVH if needed
2. GPU: Rasterize G-buffer (1-2ms)
3. GPU: Compute shader - shadow rays (3-4ms)
4. GPU: Compute shader - reflection rays if needed (2-3ms)
5. GPU: Compute shader - GI rays at low res (2-3ms)
6. GPU: Denoise each layer (2ms)
7. GPU: Composite all layers + TAA (1ms)
8. GPU: Tonemap and post-process (0.5ms)
Total target: ~15ms (66fps) with headroom

═══════════════════════════════════════════════════════════════════════════════
PHASE 4: DENOISING (CRITICAL FOR LOW SAMPLE COUNTS)
═══════════════════════════════════════════════════════════════════════════════

With 0.25-1 samples per pixel, you MUST denoise:

SPATIAL DENOISER (À-Trous Wavelet):
- Implement 5-iteration À-Trous filter in compute shader
- Edge-stopping function uses G-buffer: normal, depth, albedo
- Different kernel sizes per iteration (1, 2, 4, 8, 16 pixel offsets)
- Separate denoise for diffuse and specular (different edge-stopping)
- Use Gaussian kernel weighted by edge-stop function

```c
// CPU-side filter kernel generation
void generate_atrous_kernel(float sigma, int radius, float* kernel) {
    float sum = 0.0f;
    for (int i = -radius; i <= radius; i++) {
        kernel[i + radius] = expf(-(i*i) / (2*sigma*sigma));
        sum += kernel[i + radius];
    }
    for (int i = 0; i < 2*radius+1; i++) kernel[i] /= sum;
}

// Edge-stopping weights
float edge_stop_normal(vec3 n1, vec3 n2) {
    return powf(max(0.0f, dot(n1, n2)), 128.0f);
}
float edge_stop_depth(float z1, float z2, float gradient) {
    return expf(-fabsf(z1 - z2) / gradient);
}
```

TEMPORAL DENOISER:
- Reproject using motion vectors from G-buffer
- Read previous frame's denoised output
- Blend with current frame using exponential moving average
- Disocclusion detection: compare reprojected depth/normal/mesh ID
- On disocclusion: lower history weight (or zero out, accept noise briefly)
- Variance clipping: clamp history sample to 3×3 neighborhood color AABB
  to prevent ghosting

```c
typedef struct {
    float color[3];
    float variance;
    float moment_1[3]; // first moment (for variance estimation)
    float moment_2[3]; // second moment
    uint8_t sample_count; // how many samples accumulated
} TemporalPixel;
```

SVGF (Spatiotemporal Variance-Guided Filtering):
- Estimate per-pixel variance from temporal accumulation
- Use variance to guide spatial filter kernel size
- High variance = larger kernel, more aggressive filtering
- Low variance = smaller kernel, preserve detail
- Implement in two-pass compute shader (horizontal + vertical separable)

FIREFLY CLAMPING:
- Before denoising, detect outlier samples (>3σ from neighborhood median)
- Clamp to neighborhood max, preserving energy but reducing variance
- Critical for mirror reflections and bright emissives

═══════════════════════════════════════════════════════════════════════════════
PHASE 5: GLOBAL ILLUMINATION CACHING
═══════════════════════════════════════════════════════════════════════════════

Don't recompute GI every pixel every frame:

IRRADIANCE PROBE GRID (DDGI-inspired):
- Place probes in cascaded grid around camera (3 cascades typical)
- Cascade 0: 1m spacing, 16×16×16 grid
- Cascade 1: 4m spacing, 16×16×16 grid  
- Cascade 2: 16m spacing, 16×16×16 grid
- Total: ~12K probes, manageable

PROBE DATA STRUCTURE:
```c
typedef struct {
    float irradiance_sh[27];  // RGB × 9 SH coefficients (L2)
    float visibility[64];     // octahedral depth map (8×8)
    float world_pos[3];
    uint8_t last_update_frame;
    uint8_t update_priority;
} IrradianceProbe;
```

PROBE UPDATE STRATEGY:
- Each frame, update ~10-20% of probes (round-robin or priority-based)
- Per probe update: shoot 64-256 rays in stratified directions
- Accumulate results into SH coefficients using running average
- Hysteresis: new_value = 0.95×old + 0.05×measured (slow blend for stability)
- Visibility: store mean distance in each octahedral texel to prevent leaking

PROBE INTERPOLATION:
- For any world position, find 8 surrounding probes (trilinear)
- Blend their SH irradiance using weights based on distance and visibility
- Visibility weight prevents using probe if occluded from shading point
- Evaluate SH basis functions for surface normal to get final irradiance

GPU IMPLEMENTATION:
- Probes stored in 3D texture array or structured buffer
- Update compute shader: one workgroup per probe being updated this frame
- Sampling: pixel shader reads probe data, does trilinear interpolation

FALLBACK - REFLECTIVE SHADOW MAPS (RSM):
- If probe grid insufficient, use RSM for direct one-bounce GI
- During shadow map render, also output flux (color × intensity)
- Sample random points from shadow map as virtual point lights (VPLs)
- Very cheap, good enough for low-end scenarios

═══════════════════════════════════════════════════════════════════════════════
PHASE 6: GAME ENGINE INTEGRATION ARCHITECTURE
═══════════════════════════════════════════════════════════════════════════════

Design C API for game engine integration:

PUBLIC API (renderer.h):
```c
typedef struct RTRenderer RTRenderer;

// Initialization
RTRenderer* rt_renderer_create(const RTRendererDesc* desc);
void rt_renderer_destroy(RTRenderer* renderer);

// Scene management
uint32_t rt_add_mesh(RTRenderer* r, const RTMeshData* mesh);
void rt_remove_mesh(RTRenderer* r, uint32_t mesh_id);
uint32_t rt_add_instance(RTRenderer* r, uint32_t mesh_id, const float transform[16]);
void rt_update_instance_transform(RTRenderer* r, uint32_t inst_id, const float transform[16]);

// Lights
uint32_t rt_add_directional_light(RTRenderer* r, const RTDirectionalLight* light);
uint32_t rt_add_point_light(RTRenderer* r, const RTPointLight* light);
void rt_update_light(RTRenderer* r, uint32_t light_id, const void* light_data);

// Camera
void rt_set_camera(RTRenderer* r, const RTCamera* camera);

// Rendering
void rt_render_frame(RTRenderer* r);
uint32_t rt_get_output_texture(RTRenderer* r); // returns OpenGL/Vulkan texture handle

// Performance tuning
void rt_set_quality_preset(RTRenderer* r, RTQualityPreset preset);
void rt_set_ray_budget(RTRenderer* r, uint32_t rays_per_frame);
RTFrameStats rt_get_last_frame_stats(RTRenderer* r);
```

OPAQUE HANDLE PATTERN:
```c
// renderer.c (implementation)
struct RTRenderer {
    // Vulkan/OpenGL context
    VkDevice device;
    VkQueue queue;
    
    // Scene data
    BVHNode* bvh_nodes;
    uint32_t bvh_node_count;
    RTMesh* meshes;
    uint32_t mesh_count;
    RTInstance* instances;
    uint32_t instance_count;
    
    // GPU buffers
    VkBuffer bvh_buffer;
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    
    // Ray tracing state
    RayBudgetManager budget_mgr;
    IrradianceProbe* probe_grid;
    
    // Frame resources (triple-buffered)
    FrameResources frames[3];
    uint32_t frame_index;
};
```

STREAMING SYSTEM:
- Support incremental scene updates (don't rebuild BVH from scratch)
- Mark BLAS as dirty, rebuild only that sub-tree
- Refit TLAS when instances move (cheap)
- Async resource loading: load meshes/textures on background thread
- Double-buffer GPU uploads to avoid stalls

PERFORMANCE MONITORING:
```c
typedef struct {
    float frame_time_ms;
    float gbuffer_time_ms;
    float shadow_trace_time_ms;
    float reflection_trace_time_ms;
    float gi_trace_time_ms;
    float denoise_time_ms;
    uint32_t rays_traced;
    uint32_t triangles_tested;
    float average_bvh_depth;
} RTFrameStats;
```

CONFIGURATION SYSTEM:
- Load quality presets from JSON/ini file
- Presets: Ultra, High, Medium, Low, Potato (for GTX 1060)
- Each preset defines: ray budgets, resolutions, sample counts, denoise iterations
- Runtime adjustment based on frame time feedback

═══════════════════════════════════════════════════════════════════════════════
PHASE 7: EXTREME OPTIMIZATION FOR GTX 1060
═══════════════════════════════════════════════════════════════════════════════

Specific optimizations for Pascal architecture (GP106):

MEMORY OPTIMIZATION:
- Total VRAM budget: 6GB, allocate as:
  * G-buffer (1080p, 5 targets, half float): ~80MB
  * Depth + stencil: ~8MB
  * BVH + geometry: 512MB-1GB (aggressive streaming, LOD)
  * Textures: 2-3GB (BC compression, virtual texturing)
  * Probe grid: ~50MB
  * Temporal buffers (double-buffered): ~160MB
  * Denoise scratch buffers: ~50MB
  * Shadow maps: ~100MB
  * Reserve: ~1GB for headroom

MEMORY ALLOCATOR (C implementation):
```c
typedef struct {
    void* base_address;
    size_t size;
    size_t used;
    size_t peak_used;
} LinearAllocator;

void* arena_alloc(LinearAllocator* arena, size_t size, size_t alignment) {
    size_t current = arena->used;
    size_t aligned = (current + alignment - 1) & ~(alignment - 1);
    if (aligned + size > arena->size) return NULL; // OOM
    arena->used = aligned + size;
    if (arena->used > arena->peak_used) arena->peak_used = arena->used;
    return (char*)arena->base_address + aligned;
}
```

COMPUTE SHADER OPTIMIZATION:
- GTX 1060: 1280 CUDA cores, 10 SMs, 48KB shared memory per SM
- Target occupancy: 50%+ (use Vulkan/GLSL compiler feedback)
- Minimize register pressure: <64 registers per thread ideal
- Use shared memory for BVH node caching within workgroup
- Coalesce memory access: thread i reads buffer[i], not buffer[random]
- Avoid divergence: sort rays by direction to reduce warp divergence

RAY SORTING (C-side before GPU dispatch):
```c
// Morton code for ray direction (map to cube face + local coords)
uint32_t ray_direction_hash(float dx, float dy, float dz) {
    // Determine dominant axis and octant
    uint32_t octant = ((dx>0)<<2) | ((dy>0)<<1) | (dz>0);
    // Quantize minor components
    uint32_t u = (uint32_t)(fabsf(dy) * 255) & 0xFF;
    uint32_t v = (uint32_t)(fabsf(dz) * 255) & 0xFF;
    return (octant << 16) | (u << 8) | v;
}

// Sort rays by hash before uploading to GPU
qsort(rays, ray_count, sizeof(Ray), compare_ray_hash);
```

BANDWIDTH OPTIMIZATION:
- Pascal: 192 GB/s memory bandwidth - this is the real bottleneck
- Compress everything:
  * Normals: octahedral encoding (2×16-bit instead of 3×32-bit)
  * Positions: relative to chunk origin (16-bit offsets)
  * UVs: half-float or even fixed-point 16-bit
  * Colors: 8-bit sRGB where acceptable
- Pack G-buffer tightly: uint32_t encoding multiple attributes
- Use texture compression: BC7 for albedo, BC5 for normals, BC4 for single-channel

SIMD OPTIMIZATION (CPU-side):
- Use SSE4.2 / AVX for all vector math
- Ray-AABB test: process 4 AABBs simultaneously
- BVH build: SAH evaluation with SIMD horizontal operations
- Vertex transform: batch transforms with AVX (8 vertices at once)

```c
// SSE ray-AABB intersection (4 boxes against 1 ray)
__m128 ray_aabb_intersect_sse4(
    __m128 ray_origin_x, __m128 ray_inv_dir_x,
    __m128 aabb_min_x, __m128 aabb_max_x,
    // ... repeat for y, z
    __m128 ray_tmin, __m128 ray_tmax)
{
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(aabb_min_x, ray_origin_x), ray_inv_dir_x);
    __m128 t2 = _mm_mul_ps(_mm_sub_ps(aabb_max_x, ray_origin_x), ray_inv_dir_x);
    __m128 tmin = _mm_max_ps(_mm_min_ps(t1, t2), ray_tmin);
    __m128 tmax = _mm_min_ps(_mm_max_ps(t1, t2), ray_tmax);
    // ... combine xyz, return mask of hits
}
```

CACHE OPTIMIZATION:
- BVH node size exactly 64 bytes (one cache line)
- Traverse BVH depth-first to maximize cache reuse
- Sort triangles by Morton code so nearby triangles are in same cache lines
- Prefetch next BVH nodes: `__builtin_prefetch(&nodes[next_idx])`

═══════════════════════════════════════════════════════════════════════════════
PHASE 8: TEMPORAL UPSAMPLING (FSR-LIKE)
═══════════════════════════════════════════════════════════════════════════════

Render internally at lower resolution, upscale to target:

UPSAMPLING STRATEGY:
- Render ray tracing passes at 960×540 (quarter resolution)
- G-buffer at 1920×1080 (native)
- Upscale ray-traced layers using temporal + spatial reconstruction
- Final composite at 1080p

TEMPORAL UPSAMPLER (compute shader):
- Input: current low-res ray traced frame, previous high-res output, motion vectors
- Reproject previous frame using motion vectors
- For each output pixel:
  1. Find corresponding low-res pixel (bilinear sample)
  2. Reproject to previous frame
  3. If valid (no disocclusion): blend 80% history + 20% current
  4. If disoccluded: use spatial upsampling only (Lanczos or bicubic)
- Edge-directed interpolation: use G-buffer edges to guide upsampling

ANTI-ALIASING:
- Jitter camera position each frame (Halton sequence)
- Temporal accumulation provides anti-aliasing
- Sharpening pass after upsampling to recover detail

PERFORMANCE GAIN:
- Quarter-resolution ray tracing = 4× fewer rays to trace
- Effective ray budget becomes 8-16 million rays per frame equivalent
- Quality loss is minimal with good temporal reconstruction

═══════════════════════════════════════════════════════════════════════════════
PHASE 9: ALAN WAKE 2 SPECIFIC FEATURES
═══════════════════════════════════════════════════════════════════════════════

Address rendering challenges specific to Alan Wake 2:

VOLUMETRIC FOG & ATMOSPHERE:
- Froxel grid: divide frustum into 160×90×64 voxels (coarse)
- Each froxel stores: density, albedo, emissive
- Ray march through froxels during ray tracing (4-8 steps per ray)
- Volumetric shadows: march toward light, accumulate transmittance
- Temporal accumulation of volumetric lighting (high noise, needs many frames)
- God rays: ray march from camera through lit froxels, very cheap post-process

```c
typedef struct {
    float density;
    float scattering_albedo[3];
    float emissive[3];
} Froxel;

Froxel froxel_grid[160][90][64]; // ~9MB
```

DENSE VEGETATION (alpha-tested geometry):
- Proxy geometry for distant foliage: replace with billboard imposters
- Near foliage: full alpha-test but limit ray depth (1 bounce max)
- Pre-compute opacity for foliage clusters at distance
- Shadow rays through foliage: use stochastic transparency (don't test all triangles)

WET SURFACES (rain, puddles):
- Wet surface shader: increase specular, decrease roughness
- Puddles: planar reflections where possible (cheap), ray trace for quality
- Screen-space reflections first attempt (1ms), ray trace only on SSR failure
- Raindrops on surfaces: normal map perturbation, not geometry

FLASHLIGHT (dynamic spotlight):
- Single dynamic spotlight is highest priority light
- Always trace shadow rays toward flashlight in lit regions
- Volumetric flashlight cone: ray march through froxels in spotlight frustum
- Cookie texture projection for flashlight pattern
- Optimize: only process pixels within screen-space cone bounds

DARK ENVIRONMENTS:
- Most of Alan Wake 2 is dark - exploit this!
- Luminance-adaptive ray budget: dark pixels get 0.1× rays, lit boundaries get 2×
- Focus on contrast: light/dark boundaries are most important
- Emissive materials (signs, car lights, flashlight) are primary light sources
- Use fewer shadow rays in fully dark areas (ambient only)

ENEMY ENCOUNTERS (bright flashes):
- When enemies attack, bright muzzle flashes and effects
- Temporarily boost ray budget for these events (borrow from future frames)
- Bloom and exposure adaptation to handle brightness changes

PERFORMANCE TARGETS FOR GTX 1060:
- 1080p internal resolution (or 900p upscaled to 1080p)
- 30fps minimum, 45fps average in typical scenes
- Frame time breakdown (33ms budget for 30fps):
  * G-buffer: 3ms
  * Shadow trace (low-res): 6ms
  * Reflections (selective): 4ms
  * GI (low-res): 5ms
  * Volumetrics: 4ms
  * Denoise: 4ms
  * Composite + upscale: 3ms
  * Post-processing: 2ms
  * Reserve: 2ms
  Total: 33ms

═══════════════════════════════════════════════════════════════════════════════
IMPLEMENTATION REQUIREMENTS
═══════════════════════════════════════════════════════════════════════════════

TECHNOLOGY STACK:
- Language: C17 (GCC/Clang with -std=c17 -O3 -march=native)
- GPU API: Vulkan 1.2+ (use volk for dynamic loading) OR OpenGL 4.6 (for simplicity)
- Windowing: GLFW or SDL2
- Math: cglm library for vector/matrix math, or hand-written SIMD
- Image I/O: stb_image.h and stb_image_write.h
- Threading: pthreads (POSIX) or C11 threads
- Profiling: Use Vulkan/OpenGL timestamps, Tracy profiler integration

COMPILER FLAGS:
```bash
gcc -std=c17 -O3 -march=native -mavx2 -mfma \
    -ffast-math -funroll-loops \
    -Wall -Wextra -Werror \
    -I./include -L./lib \
    -lvulkan -lglfw -lm -lpthread \
    -o raytracer src/*.c
```

CODE STRUCTURE:
```
src/
├── main.c                 // Entry point, main loop
├── renderer.c/h           // Public API implementation
├── bvh.c/h               // BVH construction and traversal
├── ray.c/h               // Ray structures and intersection
├── material.c/h          // Material evaluation, BRDF
├── camera.c/h            // Camera and view transforms
├── scene.c/h             // Scene management
├── budget.c/h            // Ray budget manager
├── denoise.c/h           // Denoising logic (CPU-side control)
├── probe.c/h             // Irradiance probe grid
├── vulkan_backend.c/h    // Vulkan setup and compute dispatch
├── math_util.c/h         // SIMD math utilities
└── memory.c/h            // Memory allocators and pools

shaders/
├── gbuffer.vert/frag     // G-buffer generation
├── shadow_trace.comp     // Shadow ray tracing
├── reflection_trace.comp // Reflection ray tracing
├── gi_trace.comp         // GI ray tracing
├── denoise_spatial.comp  // Spatial denoiser
├── denoise_temporal.comp // Temporal denoiser
├── composite.comp        // Final composition
└── bvh_common.glsl       // Shared BVH traversal code
```

DEBUGGING FEATURES:
- Debug visualization modes (toggle with hotkeys):
  * Show BVH node boundaries
  * Color-code by BVH depth
  * Visualize ray count per tile (heatmap)
  * Show denoise weights
  * Display probe locations and their irradiance
  * Wireframe overlay
  * Normal/depth/roughness buffers individually
- Frame capture: dump G-buffer and ray-traced layers as PNG
- Statistics overlay: FPS, rays traced, triangles tested, BVH depth, VRAM usage
- Configuration hot-reload: edit config.ini, reload without restart

TESTING:
- Include test scenes:
  * Cornell box (validation against reference)
  * Sponza (complex indoor geometry)
  * Outdoor night scene with fog (Alan Wake-like)
  * High-poly forest scene (vegetation stress test)
- Benchmark mode: fixed camera paths, CSV output of frame times
- Automated regression tests: compare output images against golden references
- Validation mode: compare against offline path tracer (Mitsuba/PBRT)

QUALITY PRESETS (for GTX 1060):
```c
// config_gtx1060.ini
[preset_low]
ray_budget=2000000
shadow_resolution=0.5
reflection_resolution=0.25
gi_resolution=0.25
samples_per_pixel_shadow=1
samples_per_pixel_reflection=0.25
samples_per_pixel_gi=0.25
denoise_iterations=5
temporal_blend=0.9

[preset_medium]
ray_budget=4000000
shadow_resolution=1.0
reflection_resolution=0.5
gi_resolution=0.5
samples_per_pixel_shadow=1
samples_per_pixel_reflection=0.5
samples_per_pixel_gi=0.5
denoise_iterations=5
temporal_blend=0.85
```

ADAPTIVE SYSTEM BEHAVIOR:
- Start at "medium" preset
- Measure frame time for 10 frames
- If average > 33ms: drop to "low"
- If average < 25ms for 60 frames: try "medium" again
- If specific passes are slow, reduce just those (e.g., disable GI but keep shadows)
- User can lock preset via config to prevent adaptation

═══════════════════════════════════════════════════════════════════════════════

Build this incrementally in C. Start with:
1. Basic Vulkan/OpenGL setup and triangle rasterization
2. BVH construction (CPU-side, test with simple scenes)
3. Ray-BVH intersection (CPU verification first)
4. Compute shader BVH traversal (port to GPU)
5. G-buffer generation (rasterization pass)
6. Shadow ray tracing (simplest RT pass)
7. Spatial denoising
8. Temporal accumulation
9. Reflection and GI passes
10. Probe grid
11. Volumetric fog
12. Final polish and optimization

Test on GTX 1060 equivalent at every phase. Profile relentlessly. The goal is
30-60 FPS at 1080p in Alan Wake 2-like scenes with convincing ray-traced
lighting on hardware that officially "doesn't support ray tracing."

If you can't test on real GTX 1060, simulate constraints:
- Limit compute shader invocations
- Artificially throttle VRAM bandwidth
- Cap ray budget to measured GTX 1060 throughput

This is achievable. Metro Exodus Enhanced Edition runs ray-traced GI on GTX 1060
at ~30fps with similar techniques. Your implementation should match or exceed that.
```

---

## How to Use This Prompt

Feed this to an AI in **phases**:

1. **Phase 1**: "Implement the BVH builder from Phase 1 in C with SSE optimization"
2. **Phase 3**: "Implement the Vulkan rasterization G-buffer pass from Phase 3"
3. **Phase 3 continued**: "Implement the compute shader BVH traversal from Phase 3"
4. **Phase 4**: "Implement the À-Trous spatial denoiser from Phase 4"

This prompt assumes you're building a real game engine plugin or standalone renderer, not a toy. Every design decision is justified by the GTX 1060 constraint.
