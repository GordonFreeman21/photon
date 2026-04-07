/*
 * Photon - Adaptive Hybrid Ray Tracer
 * Public Renderer API Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/renderer.h"
#include "../include/scene.h"
#include "../include/camera.h"
#include "../include/budget.h"
#include "../include/denoise.h"
#include "../include/probe.h"
#include "../include/vulkan_backend.h"
#include "../include/math_util.h"
#include "../include/memory.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Internal Renderer Structure                                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

struct RTRenderer {
    /* Subsystems */
    VulkanContext*      vk_ctx;
    Scene               scene;
    RTCamera            camera;
    RayBudgetManager    budget;
    DenoiseState        denoise_shadow;
    DenoiseState        denoise_reflection;
    DenoiseState        denoise_gi;
    ProbeGrid           probe_grid;

    /* G-Buffer */
    GBuffer             gbuffer;

    /* GPU Pipelines */
    ComputePipeline     shadow_pipeline;
    ComputePipeline     reflection_pipeline;
    ComputePipeline     gi_pipeline;
    ComputePipeline     denoise_spatial_pipeline;
    ComputePipeline     denoise_temporal_pipeline;
    ComputePipeline     composite_pipeline;
    GraphicsPipeline    gbuffer_pipeline;

    /* GPU Buffers */
    GPUBuffer           bvh_buffer;
    GPUBuffer           vertex_buffer;
    GPUBuffer           index_buffer;
    GPUBuffer           material_buffer;
    GPUBuffer           light_buffer;
    GPUBuffer           probe_buffer;

    /* Configuration */
    RTRendererDesc      config;
    QualityConfig       quality;
    RTQualityPreset     current_preset;
    DebugVisMode        debug_vis;

    /* Stats */
    RTFrameStats        last_stats;
    uint64_t            frame_count;

    /* Adaptive quality */
    float               frame_time_history[60];
    uint32_t            frame_time_index;
    uint32_t            frames_at_current_quality;
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Helpers                                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

static float get_time_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (float)(ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6);
}

static float compute_average_frame_time(const RTRenderer* r)
{
    uint32_t count = r->frame_count < 60 ? (uint32_t)r->frame_count : 60;
    if (count == 0) return 0.0f;
    float sum = 0.0f;
    for (uint32_t i = 0; i < count; i++)
        sum += r->frame_time_history[i];
    return sum / (float)count;
}

/* Try to step quality up or down based on recent frame times. */
static void adaptive_quality_adjust(RTRenderer* r)
{
    if (!r->config.adaptive_quality) return;

    float avg = compute_average_frame_time(r);
    if (avg <= 0.0f) return;

    float target_ms = r->budget.target_frame_time_ms;

    /* Require at least 60 frames at current quality before changing */
    r->frames_at_current_quality++;
    if (r->frames_at_current_quality < 60) return;

    if (avg > target_ms * 1.15f && r->current_preset > RT_QUALITY_POTATO) {
        /* Frame time too high — drop quality */
        rt_set_quality_preset(r, (RTQualityPreset)(r->current_preset - 1));
        r->frames_at_current_quality = 0;
        printf("[Photon] Adaptive quality: lowered to preset %d (avg %.2f ms)\n",
               r->current_preset, avg);
    } else if (avg < target_ms * 0.80f && r->current_preset < RT_QUALITY_ULTRA) {
        /* Plenty of headroom — raise quality */
        rt_set_quality_preset(r, (RTQualityPreset)(r->current_preset + 1));
        r->frames_at_current_quality = 0;
        printf("[Photon] Adaptive quality: raised to preset %d (avg %.2f ms)\n",
               r->current_preset, avg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Lifecycle                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

RTRenderer* rt_renderer_create(const RTRendererDesc* desc)
{
    if (!desc) {
        fprintf(stderr, "[Photon] rt_renderer_create: NULL descriptor\n");
        return NULL;
    }

    RTRenderer* r = (RTRenderer*)calloc(1, sizeof(RTRenderer));
    if (!r) {
        fprintf(stderr, "[Photon] rt_renderer_create: allocation failed\n");
        return NULL;
    }

    r->config = *desc;
    printf("[Photon] Creating renderer %ux%u\n", desc->width, desc->height);

    /* --- Vulkan context -------------------------------------------------- */
    VulkanInitDesc vk_desc = {
        .width              = desc->width,
        .height             = desc->height,
        .app_name           = desc->window_title ? desc->window_title : "Photon",
        .enable_validation  = true,
        .window_handle      = NULL
    };
    r->vk_ctx = vk_create(&vk_desc);
    if (!r->vk_ctx) {
        fprintf(stderr, "[Photon] Vulkan initialization failed\n");
        free(r);
        return NULL;
    }

    /* --- Scene ----------------------------------------------------------- */
    scene_init(&r->scene);

    /* --- Camera ---------------------------------------------------------- */
    vec3 cam_pos    = vec3_new(0.0f, 1.0f, 5.0f);
    vec3 cam_target = vec3_new(0.0f, 0.0f, 0.0f);
    float aspect    = (float)desc->width / (float)desc->height;
    r->camera = camera_create(cam_pos, cam_target, 60.0f * (PHOTON_PI / 180.0f), aspect);

    /* --- Budget manager -------------------------------------------------- */
    uint32_t initial_budget = desc->ray_budget > 0 ? desc->ray_budget : 500000;
    float target_fps = desc->vsync ? 60.0f : 120.0f;
    r->budget = budget_create(initial_budget, target_fps);

    /* --- Denoise states -------------------------------------------------- */
    r->denoise_shadow     = denoise_create(desc->width, desc->height);
    r->denoise_reflection = denoise_create(desc->width, desc->height);
    r->denoise_gi         = denoise_create(desc->width, desc->height);

    /* --- Probe grid with default cascade spacings [1, 4, 16] ------------- */
    float cascade_spacings[PHOTON_PROBE_CASCADE_COUNT] = { 1.0f, 4.0f, 16.0f };
    vec3 grid_origin = vec3_new(0.0f, 0.0f, 0.0f);
    r->probe_grid = probe_grid_create(grid_origin, cascade_spacings);

    /* --- G-Buffer -------------------------------------------------------- */
    r->gbuffer = vk_create_gbuffer(r->vk_ctx, desc->width, desc->height);

    /* --- Compute pipelines ----------------------------------------------- */
    r->shadow_pipeline           = vk_create_compute_pipeline(r->vk_ctx, "shaders/shadow_trace.comp.spv");
    r->reflection_pipeline       = vk_create_compute_pipeline(r->vk_ctx, "shaders/reflection_trace.comp.spv");
    r->gi_pipeline               = vk_create_compute_pipeline(r->vk_ctx, "shaders/gi_trace.comp.spv");
    r->denoise_spatial_pipeline  = vk_create_compute_pipeline(r->vk_ctx, "shaders/denoise_spatial.comp.spv");
    r->denoise_temporal_pipeline = vk_create_compute_pipeline(r->vk_ctx, "shaders/denoise_temporal.comp.spv");
    r->composite_pipeline        = vk_create_compute_pipeline(r->vk_ctx, "shaders/composite.comp.spv");

    /* --- Graphics pipeline (G-buffer pass) ------------------------------- */
    r->gbuffer_pipeline = vk_create_gbuffer_pipeline(r->vk_ctx, &r->gbuffer);

    /* --- GPU buffers ----------------------------------------------------- */
    size_t bvh_buf_size      = sizeof(BVHNode)    * PHOTON_MAX_INSTANCES * 256;
    size_t vertex_buf_size   = sizeof(Vertex)      * PHOTON_MAX_MESHES  * 1024;
    size_t index_buf_size    = sizeof(uint32_t)     * PHOTON_MAX_MESHES  * 4096;
    size_t material_buf_size = sizeof(RTMaterial)   * PHOTON_MAX_MATERIALS;
    size_t light_buf_size    = sizeof(RTLight)      * PHOTON_MAX_LIGHTS;
    size_t probe_buf_size    = sizeof(IrradianceProbe) * PHOTON_PROBE_TOTAL;

    r->bvh_buffer      = vk_create_buffer(r->vk_ctx, bvh_buf_size,      0x00000080); /* STORAGE */
    r->vertex_buffer    = vk_create_buffer(r->vk_ctx, vertex_buf_size,   0x00000080);
    r->index_buffer     = vk_create_buffer(r->vk_ctx, index_buf_size,    0x00000040); /* INDEX */
    r->material_buffer  = vk_create_buffer(r->vk_ctx, material_buf_size, 0x00000080);
    r->light_buffer     = vk_create_buffer(r->vk_ctx, light_buf_size,    0x00000080);
    r->probe_buffer     = vk_create_buffer(r->vk_ctx, probe_buf_size,    0x00000080);

    /* --- Apply initial quality preset ------------------------------------ */
    r->current_preset = desc->quality;
    r->quality = quality_get_preset(desc->quality);
    budget_apply_quality(&r->budget, &r->quality);

    r->debug_vis    = DEBUG_VIS_NONE;
    r->frame_count  = 0;
    r->frame_time_index = 0;
    r->frames_at_current_quality = 0;
    memset(r->frame_time_history, 0, sizeof(r->frame_time_history));
    memset(&r->last_stats, 0, sizeof(r->last_stats));

    printf("[Photon] Renderer created successfully (preset %d)\n", r->current_preset);
    return r;
}

void rt_renderer_destroy(RTRenderer* r)
{
    if (!r) return;
    printf("[Photon] Destroying renderer\n");

    /* GPU buffers */
    vk_destroy_buffer(r->vk_ctx, &r->probe_buffer);
    vk_destroy_buffer(r->vk_ctx, &r->light_buffer);
    vk_destroy_buffer(r->vk_ctx, &r->material_buffer);
    vk_destroy_buffer(r->vk_ctx, &r->index_buffer);
    vk_destroy_buffer(r->vk_ctx, &r->vertex_buffer);
    vk_destroy_buffer(r->vk_ctx, &r->bvh_buffer);

    /* Pipelines */
    vk_destroy_graphics_pipeline(r->vk_ctx, &r->gbuffer_pipeline);
    vk_destroy_compute_pipeline(r->vk_ctx, &r->composite_pipeline);
    vk_destroy_compute_pipeline(r->vk_ctx, &r->denoise_temporal_pipeline);
    vk_destroy_compute_pipeline(r->vk_ctx, &r->denoise_spatial_pipeline);
    vk_destroy_compute_pipeline(r->vk_ctx, &r->gi_pipeline);
    vk_destroy_compute_pipeline(r->vk_ctx, &r->reflection_pipeline);
    vk_destroy_compute_pipeline(r->vk_ctx, &r->shadow_pipeline);

    /* G-Buffer */
    vk_destroy_gbuffer(r->vk_ctx, &r->gbuffer);

    /* Subsystems */
    probe_grid_destroy(&r->probe_grid);
    denoise_destroy(&r->denoise_gi);
    denoise_destroy(&r->denoise_reflection);
    denoise_destroy(&r->denoise_shadow);
    budget_destroy(&r->budget);
    scene_destroy(&r->scene);

    /* Vulkan context last */
    vk_destroy(r->vk_ctx);

    free(r);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Scene Management                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t rt_add_mesh(RTRenderer* r, const RTMeshData* mesh)
{
    if (!r || !mesh) return PHOTON_INVALID_ID;
    return scene_add_mesh(&r->scene, mesh);
}

void rt_remove_mesh(RTRenderer* r, uint32_t mesh_id)
{
    if (!r) return;
    scene_remove_mesh(&r->scene, mesh_id);
}

uint32_t rt_add_instance(RTRenderer* r, uint32_t mesh_id, const float transform[16])
{
    if (!r) return PHOTON_INVALID_ID;
    return scene_add_instance(&r->scene, mesh_id, transform);
}

void rt_remove_instance(RTRenderer* r, uint32_t inst_id)
{
    if (!r) return;
    scene_remove_instance(&r->scene, inst_id);
}

void rt_update_instance_transform(RTRenderer* r, uint32_t inst_id, const float transform[16])
{
    if (!r) return;
    scene_update_instance_transform(&r->scene, inst_id, transform);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Lights                                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t rt_add_directional_light(RTRenderer* r, const RTDirectionalLight* light)
{
    if (!r || !light) return PHOTON_INVALID_ID;
    RTLight wrapper;
    memset(&wrapper, 0, sizeof(wrapper));
    wrapper.type        = LIGHT_DIRECTIONAL;
    wrapper.directional = *light;
    wrapper.active      = true;
    return scene_add_light(&r->scene, &wrapper);
}

uint32_t rt_add_point_light(RTRenderer* r, const RTPointLight* light)
{
    if (!r || !light) return PHOTON_INVALID_ID;
    RTLight wrapper;
    memset(&wrapper, 0, sizeof(wrapper));
    wrapper.type  = LIGHT_POINT;
    wrapper.point = *light;
    wrapper.active = true;
    return scene_add_light(&r->scene, &wrapper);
}

uint32_t rt_add_spot_light(RTRenderer* r, const RTSpotLight* light)
{
    if (!r || !light) return PHOTON_INVALID_ID;
    RTLight wrapper;
    memset(&wrapper, 0, sizeof(wrapper));
    wrapper.type = LIGHT_SPOT;
    wrapper.spot = *light;
    wrapper.active = true;
    return scene_add_light(&r->scene, &wrapper);
}

void rt_update_light(RTRenderer* r, uint32_t light_id, const void* light_data)
{
    if (!r || !light_data || light_id >= r->scene.light_count) return;
    RTLight updated = r->scene.lights[light_id];
    switch (updated.type) {
        case LIGHT_DIRECTIONAL:
            updated.directional = *(const RTDirectionalLight*)light_data;
            break;
        case LIGHT_POINT:
            updated.point = *(const RTPointLight*)light_data;
            break;
        case LIGHT_SPOT:
            updated.spot = *(const RTSpotLight*)light_data;
            break;
        default:
            break;
    }
    scene_update_light(&r->scene, light_id, &updated);
}

void rt_remove_light(RTRenderer* r, uint32_t light_id)
{
    if (!r) return;
    scene_remove_light(&r->scene, light_id);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Materials & Textures                                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t rt_add_material(RTRenderer* r, const RTMaterial* mat)
{
    if (!r || !mat) return PHOTON_INVALID_ID;
    return material_add(&r->scene.materials, mat);
}

uint32_t rt_load_texture(RTRenderer* r, const char* path)
{
    if (!r || !path) return PHOTON_INVALID_ID;
    return texture_load(&r->scene.textures, path);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Camera                                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

void rt_set_camera(RTRenderer* r, const RTCamera* camera)
{
    if (!r || !camera) return;
    r->camera = *camera;
    camera_update(&r->camera);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Rendering                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

void rt_render_frame(RTRenderer* r)
{
    if (!r) return;

    float frame_start = get_time_ms();

    uint32_t w = r->config.width;
    uint32_t h = r->config.height;
    uint32_t groups_x = (w + PHOTON_TILE_SIZE - 1) / PHOTON_TILE_SIZE;
    uint32_t groups_y = (h + PHOTON_TILE_SIZE - 1) / PHOTON_TILE_SIZE;

    /* ── 1. Camera update ─────────────────────────────────────────────── */
    camera_compute_taa_jitter(&r->camera, (uint32_t)r->frame_count);
    camera_save_previous(&r->camera);
    camera_update(&r->camera);

    /* ── 2. Scene commit (rebuild / refit acceleration structures) ───── */
    scene_commit(&r->scene);

    /* ── 3. Begin frame budget ────────────────────────────────────────── */
    budget_begin_frame(&r->budget, w, h);

    /* ── 4. Upload scene data to GPU buffers ──────────────────────────── */
    BVHGPUBuffer bvh_gpu = bvh_pack_for_gpu(&r->scene.tlas);
    vk_upload_buffer(r->vk_ctx, &r->bvh_buffer, bvh_gpu.data, bvh_gpu.size);
    bvh_gpu_buffer_destroy(&bvh_gpu);

    /* Upload vertex data from all meshes */
    {
        size_t offset = 0;
        for (uint32_t i = 0; i < r->scene.mesh_count; i++) {
            const RTMeshData* md = &r->scene.meshes[i].data;
            if (md->vertices && md->vertex_count > 0) {
                size_t vsize = md->vertex_count * sizeof(Vertex);
                if (offset + vsize <= r->vertex_buffer.size)
                    vk_upload_buffer(r->vk_ctx, &r->vertex_buffer,
                                     md->vertices, vsize);
                offset += vsize;
            }
        }
    }

    /* Upload material data */
    if (r->scene.materials.count > 0) {
        vk_upload_buffer(r->vk_ctx, &r->material_buffer,
                         r->scene.materials.materials,
                         r->scene.materials.count * sizeof(RTMaterial));
    }

    /* Upload light data */
    if (r->scene.light_count > 0) {
        vk_upload_buffer(r->vk_ctx, &r->light_buffer,
                         r->scene.lights,
                         r->scene.light_count * sizeof(RTLight));
    }

    /* Upload probe data */
    ProbeGPUBuffer probe_gpu = probe_pack_for_gpu(&r->probe_grid);
    vk_upload_buffer(r->vk_ctx, &r->probe_buffer, probe_gpu.sh_data, probe_gpu.total_size);
    probe_gpu_buffer_destroy(&probe_gpu);

    /* ── 5. Begin Vulkan frame ────────────────────────────────────────── */
    FrameResources frame;
    memset(&frame, 0, sizeof(frame));
    if (!vk_begin_frame(r->vk_ctx, &frame)) {
        fprintf(stderr, "[Photon] vk_begin_frame failed, skipping frame\n");
        return;
    }

    /* ── 6. G-Buffer pass ─────────────────────────────────────────────── */
    vk_timestamp_begin(r->vk_ctx, "gbuffer");
    vk_dispatch_compute(r->vk_ctx, (ComputePipeline*)&r->gbuffer_pipeline,
                        groups_x, groups_y, 1);
    vk_timestamp_end(r->vk_ctx, "gbuffer");

    /* ── 7. Shadow ray trace ──────────────────────────────────────────── */
    uint32_t shadow_rays = groups_x * groups_y * r->quality.shadow_spp;
    vk_timestamp_begin(r->vk_ctx, "shadow");
    budget_spend_rays(&r->budget, shadow_rays);
    vk_dispatch_compute(r->vk_ctx, &r->shadow_pipeline, groups_x, groups_y, 1);
    vk_timestamp_end(r->vk_ctx, "shadow");

    /* ── 8. Reflection ray trace (if enabled & budget allows) ─────────── */
    if (r->config.enable_reflections) {
        uint32_t refl_rays = groups_x * groups_y * r->quality.reflection_spp;
        if (budget_can_trace(&r->budget, refl_rays)) {
            vk_timestamp_begin(r->vk_ctx, "reflection");
            budget_spend_rays(&r->budget, refl_rays);
            vk_dispatch_compute(r->vk_ctx, &r->reflection_pipeline,
                                groups_x, groups_y, 1);
            vk_timestamp_end(r->vk_ctx, "reflection");
        }
    }

    /* ── 9. GI ray trace (if enabled & budget allows) ─────────────────── */
    if (r->config.enable_gi) {
        uint32_t gi_rays = groups_x * groups_y * r->quality.gi_spp;
        if (budget_can_trace(&r->budget, gi_rays)) {
            vk_timestamp_begin(r->vk_ctx, "gi");
            budget_spend_rays(&r->budget, gi_rays);
            vk_dispatch_compute(r->vk_ctx, &r->gi_pipeline,
                                groups_x, groups_y, 1);
            vk_timestamp_end(r->vk_ctx, "gi");
        }
    }

    /* ── 10. Denoise passes (spatial + temporal for each signal) ───────── */
    vk_timestamp_begin(r->vk_ctx, "denoise");

    /* Shadow denoise */
    denoise_temporal_update(&r->denoise_shadow, r->quality.temporal_blend);
    vk_dispatch_compute(r->vk_ctx, &r->denoise_spatial_pipeline,  groups_x, groups_y, 1);
    vk_dispatch_compute(r->vk_ctx, &r->denoise_temporal_pipeline, groups_x, groups_y, 1);
    denoise_swap_buffers(&r->denoise_shadow);

    /* Reflection denoise */
    if (r->config.enable_reflections) {
        denoise_temporal_update(&r->denoise_reflection, r->quality.temporal_blend);
        vk_dispatch_compute(r->vk_ctx, &r->denoise_spatial_pipeline,  groups_x, groups_y, 1);
        vk_dispatch_compute(r->vk_ctx, &r->denoise_temporal_pipeline, groups_x, groups_y, 1);
        denoise_swap_buffers(&r->denoise_reflection);
    }

    /* GI denoise */
    if (r->config.enable_gi) {
        denoise_temporal_update(&r->denoise_gi, r->quality.temporal_blend);
        vk_dispatch_compute(r->vk_ctx, &r->denoise_spatial_pipeline,  groups_x, groups_y, 1);
        vk_dispatch_compute(r->vk_ctx, &r->denoise_temporal_pipeline, groups_x, groups_y, 1);
        denoise_swap_buffers(&r->denoise_gi);
    }

    vk_timestamp_end(r->vk_ctx, "denoise");

    /* ── 11. Composite pass ───────────────────────────────────────────── */
    vk_timestamp_begin(r->vk_ctx, "composite");
    vk_dispatch_compute(r->vk_ctx, &r->composite_pipeline, groups_x, groups_y, 1);
    vk_timestamp_end(r->vk_ctx, "composite");

    /* ── 12. End Vulkan frame ─────────────────────────────────────────── */
    vk_end_frame(r->vk_ctx, &frame);

    /* ── 13. End budget frame with measured GPU times ─────────────────── */
    float frame_end  = get_time_ms();
    float frame_time = frame_end - frame_start;
    budget_end_frame(&r->budget, frame_time);

    /* ── 14. Update stats ─────────────────────────────────────────────── */
    r->last_stats.frame_time_ms            = frame_time;
    r->last_stats.gbuffer_time_ms          = vk_timestamp_get_ms(r->vk_ctx, "gbuffer");
    r->last_stats.shadow_trace_time_ms     = vk_timestamp_get_ms(r->vk_ctx, "shadow");
    r->last_stats.reflection_trace_time_ms = vk_timestamp_get_ms(r->vk_ctx, "reflection");
    r->last_stats.gi_trace_time_ms         = vk_timestamp_get_ms(r->vk_ctx, "gi");
    r->last_stats.denoise_time_ms          = vk_timestamp_get_ms(r->vk_ctx, "denoise");
    r->last_stats.composite_time_ms        = vk_timestamp_get_ms(r->vk_ctx, "composite");
    r->last_stats.rays_traced              = r->budget.rays_spent_this_frame;
    r->last_stats.active_probes            = r->probe_grid.probes_per_frame;

    /* ── 15. Adaptive quality adjustment ──────────────────────────────── */
    r->frame_time_history[r->frame_time_index % 60] = frame_time;
    r->frame_time_index++;
    adaptive_quality_adjust(r);

    /* ── 16. Increment frame counter ──────────────────────────────────── */
    r->frame_count++;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Performance & Quality                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

void rt_set_quality_preset(RTRenderer* r, RTQualityPreset preset)
{
    if (!r || preset >= RT_QUALITY_COUNT) return;

    r->current_preset = preset;
    r->quality = quality_get_preset(preset);
    budget_apply_quality(&r->budget, &r->quality);

    /* Propagate denoise settings from the quality config */
    r->denoise_shadow.atrous_iterations     = r->quality.denoise_iterations;
    r->denoise_shadow.temporal_blend_factor  = r->quality.temporal_blend;
    r->denoise_reflection.atrous_iterations  = r->quality.denoise_iterations;
    r->denoise_reflection.temporal_blend_factor = r->quality.temporal_blend;
    r->denoise_gi.atrous_iterations          = r->quality.denoise_iterations;
    r->denoise_gi.temporal_blend_factor      = r->quality.temporal_blend;

    printf("[Photon] Quality preset set to %d (budget %u, denoise iters %u)\n",
           preset, r->quality.ray_budget, r->quality.denoise_iterations);
}

void rt_set_ray_budget(RTRenderer* r, uint32_t rays_per_frame)
{
    if (!r) return;
    r->budget.total_ray_budget = rays_per_frame;
    printf("[Photon] Ray budget overridden to %u\n", rays_per_frame);
}

RTFrameStats rt_get_last_frame_stats(RTRenderer* r)
{
    if (!r) {
        RTFrameStats empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return r->last_stats;
}

uint32_t rt_get_output_texture(RTRenderer* r)
{
    if (!r) return PHOTON_INVALID_ID;
    /* Return the swapchain image handle managed by the Vulkan backend */
    return r->gbuffer.albedo;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Debug                                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

void rt_set_debug_vis(RTRenderer* r, DebugVisMode mode)
{
    if (!r || mode >= DEBUG_VIS_COUNT) return;
    r->debug_vis = mode;
    printf("[Photon] Debug vis mode set to %d\n", mode);
}

void rt_capture_frame(RTRenderer* r, const char* output_path)
{
    if (!r || !output_path) return;
    /* TODO: read back swapchain image and dump to PNG via stb_image_write */
    printf("[Photon] Frame capture requested -> %s (stub)\n", output_path);
}
