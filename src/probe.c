/*
 * Photon - Irradiance Probe Grid (DDGI-inspired)
 * CPU-side probe management and sampling
 */

#include "../include/probe.h"
#include "../include/math_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Utility Helpers                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Get probe index from cascade and grid coordinates */
static uint32_t probe_get_index(uint32_t cascade, uint32_t gx, uint32_t gy, uint32_t gz) {
    uint32_t probes_per_cascade = PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM;
    return cascade * probes_per_cascade + gz * PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM +
           gy * PHOTON_PROBE_GRID_DIM + gx;
}

/* Get grid coordinates and cascade from probe index */
static void probe_get_coords(uint32_t idx, uint32_t* cascade, uint32_t* gx, uint32_t* gy, uint32_t* gz) {
    uint32_t probes_per_cascade = PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM;
    *cascade = idx / probes_per_cascade;
    uint32_t local_idx = idx % probes_per_cascade;
    *gz = local_idx / (PHOTON_PROBE_GRID_DIM * PHOTON_PROBE_GRID_DIM);
    *gy = (local_idx / PHOTON_PROBE_GRID_DIM) % PHOTON_PROBE_GRID_DIM;
    *gx = local_idx % PHOTON_PROBE_GRID_DIM;
}

/* Compute world position for a probe */
static vec3 probe_compute_world_position(const ProbeGrid* grid, uint32_t probe_idx) {
    uint32_t cascade, gx, gy, gz;
    probe_get_coords(probe_idx, &cascade, &gx, &gy, &gz);
    
    float spacing = grid->cascade_spacing[cascade];
    vec3 offset = vec3_new(
        (float)gx * spacing,
        (float)gy * spacing,
        (float)gz * spacing
    );
    
    return vec3_add(grid->grid_origin, offset);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Grid Management                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

ProbeGrid probe_grid_create(vec3 origin, float cascade_spacings[PHOTON_PROBE_CASCADE_COUNT]) {
    ProbeGrid grid = {0};
    
    /* Allocate probe array: PHOTON_PROBE_TOTAL probes */
    grid.probes = (IrradianceProbe*)calloc(PHOTON_PROBE_TOTAL, sizeof(IrradianceProbe));
    if (!grid.probes) {
        return grid;  /* Return empty grid on allocation failure */
    }
    
    grid.probe_count = PHOTON_PROBE_TOTAL;
    grid.grid_origin = origin;
    grid.grid_dim = PHOTON_PROBE_GRID_DIM;
    grid.hysteresis = 0.95f;
    grid.rays_per_probe = 64;
    grid.probes_per_frame = 256;
    
    /* Copy cascade spacings */
    for (uint32_t i = 0; i < PHOTON_PROBE_CASCADE_COUNT; i++) {
        grid.cascade_spacing[i] = cascade_spacings[i];
    }
    
    /* Initialize all probes */
    for (uint32_t i = 0; i < PHOTON_PROBE_TOTAL; i++) {
        IrradianceProbe* probe = &grid.probes[i];
        
        /* Zero SH coefficients and visibility */
        memset(probe->irradiance_sh, 0, sizeof(probe->irradiance_sh));
        memset(probe->visibility, 0, sizeof(probe->visibility));
        
        /* Compute world position */
        vec3 world_pos = probe_compute_world_position(&grid, i);
        probe->world_pos[0] = world_pos.x;
        probe->world_pos[1] = world_pos.y;
        probe->world_pos[2] = world_pos.z;
        
        /* Initialize metadata */
        probe->last_update_frame = 0;
        probe->update_priority = 1;
        probe->padding = 0;
    }
    
    return grid;
}

void probe_grid_destroy(ProbeGrid* grid) {
    if (grid && grid->probes) {
        free(grid->probes);
        grid->probes = NULL;
        grid->probe_count = 0;
    }
}

void probe_grid_update_position(ProbeGrid* grid, vec3 camera_pos) {
    /* Reposition grid origin so camera is near the center of the grid */
    
    /* Get average spacing to determine step size */
    float avg_spacing = 0.0f;
    for (uint32_t i = 0; i < PHOTON_PROBE_CASCADE_COUNT; i++) {
        avg_spacing += grid->cascade_spacing[i];
    }
    avg_spacing /= PHOTON_PROBE_CASCADE_COUNT;
    
    /* Desired grid extent (half-width on each side) */
    float grid_extent = (PHOTON_PROBE_GRID_DIM - 1) * 0.5f * avg_spacing;
    
    /* Snap camera position to grid cell and offset to center */
    float cell_size = avg_spacing;
    vec3 snap_pos = vec3_new(
        floorf(camera_pos.x / cell_size) * cell_size,
        floorf(camera_pos.y / cell_size) * cell_size,
        floorf(camera_pos.z / cell_size) * cell_size
    );
    
    /* Set grid origin such that camera is roughly centered */
    grid->grid_origin = vec3_sub(snap_pos, vec3_new(grid_extent, grid_extent, grid_extent));
    
    /* Update all probe world positions */
    for (uint32_t i = 0; i < PHOTON_PROBE_TOTAL; i++) {
        vec3 world_pos = probe_compute_world_position(grid, i);
        grid->probes[i].world_pos[0] = world_pos.x;
        grid->probes[i].world_pos[1] = world_pos.y;
        grid->probes[i].world_pos[2] = world_pos.z;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Probe Updates                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

void probe_select_updates(ProbeGrid* grid, uint32_t* probe_indices,
                          uint32_t* count, uint32_t max_updates) {
    if (!probe_indices || !count) {
        return;
    }
    
    uint32_t selected = 0;
    uint32_t probes_per_frame = photon_minf(grid->probes_per_frame, max_updates);
    uint32_t total_probes = grid->probe_count;
    
    /* Round-robin base offset */
    static uint32_t round_robin_offset = 0;
    uint32_t base_offset = (round_robin_offset / probes_per_frame) * probes_per_frame;
    
    /* Collect candidate probes: combine round-robin + priority */
    typedef struct {
        uint32_t idx;
        uint8_t priority;
        uint8_t age;
    } ProbeCandidate;
    
    ProbeCandidate* candidates = (ProbeCandidate*)malloc(total_probes * sizeof(ProbeCandidate));
    if (!candidates) {
        *count = 0;
        return;
    }
    
    /* Collect all probes with priority */
    for (uint32_t i = 0; i < total_probes; i++) {
        candidates[i].idx = i;
        candidates[i].priority = grid->probes[i].update_priority;
        candidates[i].age = grid->probes[i].last_update_frame;
    }
    
    /* Simple priority sort: prefer high priority and old probes */
    for (uint32_t i = 0; i < probes_per_frame && i < total_probes; i++) {
        uint32_t best_idx = i;
        uint8_t best_score = 0;
        
        for (uint32_t j = i; j < total_probes; j++) {
            uint8_t score = (candidates[j].priority << 1) + (candidates[j].age > 30 ? 1 : 0);
            if (score > best_score || j == i) {
                best_score = score;
                best_idx = j;
            }
        }
        
        /* Swap to front */
        ProbeCandidate tmp = candidates[i];
        candidates[i] = candidates[best_idx];
        candidates[best_idx] = tmp;
        
        probe_indices[selected++] = candidates[i].idx;
    }
    
    round_robin_offset = (round_robin_offset + probes_per_frame) % total_probes;
    free(candidates);
    
    *count = selected;
}

/* Fibonacci sphere for stratified sampling */
static void fibonacci_sphere_point(uint32_t i, uint32_t total, uint32_t frame_index, vec3* out) {
    float phi = acosf(1.0f - 2.0f * i / total);
    float theta = PHOTON_PI * (1.0f + sqrtf(5.0f)) * i;
    
    /* Jitter based on frame for temporal variation */
    float jitter_scale = 0.1f;
    float jitter_u = (float)((frame_index * 17) % 256) / 256.0f * jitter_scale - jitter_scale * 0.5f;
    float jitter_v = (float)((frame_index * 37) % 256) / 256.0f * jitter_scale - jitter_scale * 0.5f;
    
    phi += jitter_u;
    theta += jitter_v;
    
    float sin_phi = sinf(phi);
    out->x = cosf(theta) * sin_phi;
    out->y = sinf(theta) * sin_phi;
    out->z = cosf(phi);
}

void probe_generate_ray_directions(vec3* directions, uint32_t count, uint32_t frame_index) {
    if (!directions) {
        return;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        fibonacci_sphere_point(i, count, frame_index, &directions[i]);
    }
}

void probe_update_irradiance(IrradianceProbe* probe, const vec3* directions,
                             const vec3* radiance, uint32_t sample_count, float hysteresis) {
    if (!probe || !directions || !radiance) {
        return;
    }
    
    /* Accumulate measured SH coefficients */
    float measured_sh[PHOTON_SH_COEFF_COUNT * 3] = {0};
    
    for (uint32_t i = 0; i < sample_count; i++) {
        float basis[PHOTON_SH_COEFF_COUNT];
        sh_evaluate_basis(directions[i], basis);
        
        /* Project radiance sample onto SH basis for each color channel */
        for (uint32_t c = 0; c < 3; c++) {
            float color = (c == 0) ? radiance[i].x : (c == 1) ? radiance[i].y : radiance[i].z;
            
            for (uint32_t k = 0; k < PHOTON_SH_COEFF_COUNT; k++) {
                measured_sh[c * PHOTON_SH_COEFF_COUNT + k] += basis[k] * color;
            }
        }
    }
    
    /* Normalize by sample count */
    float weight = 1.0f / (sample_count + PHOTON_EPSILON);
    for (uint32_t i = 0; i < PHOTON_SH_COEFF_COUNT * 3; i++) {
        measured_sh[i] *= weight;
    }
    
    /* Apply hysteresis: blend old and new values */
    for (uint32_t i = 0; i < PHOTON_SH_COEFF_COUNT * 3; i++) {
        probe->irradiance_sh[i] = hysteresis * probe->irradiance_sh[i] +
                                  (1.0f - hysteresis) * measured_sh[i];
    }
}

void probe_update_visibility(IrradianceProbe* probe, const vec3* directions,
                             const float* distances, uint32_t sample_count, float hysteresis) {
    if (!probe || !directions || !distances) {
        return;
    }
    
    /* Octahedral map: 8×8 depth values */
    float measured_visibility[64] = {0};
    
    for (uint32_t i = 0; i < sample_count && i < 64; i++) {
        /* Encode direction to octahedral coordinates */
        vec2 oct = octahedral_encode(directions[i]);
        
        /* Map to 8×8 grid */
        int32_t ox = (int32_t)(photon_saturate((oct.x + 1.0f) * 0.5f) * 7.999f);
        int32_t oy = (int32_t)(photon_saturate((oct.y + 1.0f) * 0.5f) * 7.999f);
        uint32_t oct_idx = oy * 8 + ox;
        
        if (oct_idx < 64) {
            measured_visibility[oct_idx] = distances[i];
        }
    }
    
    /* Apply hysteresis */
    for (uint32_t i = 0; i < 64; i++) {
        probe->visibility[i] = hysteresis * probe->visibility[i] +
                               (1.0f - hysteresis) * measured_visibility[i];
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Probe Sampling                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

void probe_get_interpolation(const ProbeGrid* grid, vec3 world_pos,
                             uint32_t probe_indices[8], float weights[8]) {
    if (!grid || !probe_indices || !weights) {
        return;
    }
    
    /* Find cascade based on distance to grid origin */
    vec3 rel_pos = vec3_sub(world_pos, grid->grid_origin);
    float dist_to_origin = vec3_length(rel_pos);
    
    uint32_t cascade = 0;
    for (uint32_t i = 1; i < PHOTON_PROBE_CASCADE_COUNT; i++) {
        if (dist_to_origin > grid->cascade_spacing[i-1] * PHOTON_PROBE_GRID_DIM * 0.5f) {
            cascade = i;
        }
    }
    cascade = photon_minf(cascade, PHOTON_PROBE_CASCADE_COUNT - 1);
    
    float spacing = grid->cascade_spacing[cascade];
    float inv_spacing = 1.0f / (spacing + PHOTON_EPSILON);
    
    /* Compute grid cell coordinates */
    float gx_f = rel_pos.x * inv_spacing;
    float gy_f = rel_pos.y * inv_spacing;
    float gz_f = rel_pos.z * inv_spacing;
    
    /* Clamp to valid range */
    gx_f = photon_clampf(gx_f, 0.0f, (float)(PHOTON_PROBE_GRID_DIM - 1));
    gy_f = photon_clampf(gy_f, 0.0f, (float)(PHOTON_PROBE_GRID_DIM - 1));
    gz_f = photon_clampf(gz_f, 0.0f, (float)(PHOTON_PROBE_GRID_DIM - 1));
    
    uint32_t gx0 = (uint32_t)gx_f;
    uint32_t gy0 = (uint32_t)gy_f;
    uint32_t gz0 = (uint32_t)gz_f;
    
    uint32_t gx1 = photon_minf(gx0 + 1, PHOTON_PROBE_GRID_DIM - 1);
    uint32_t gy1 = photon_minf(gy0 + 1, PHOTON_PROBE_GRID_DIM - 1);
    uint32_t gz1 = photon_minf(gz0 + 1, PHOTON_PROBE_GRID_DIM - 1);
    
    /* Compute interpolation weights */
    float wx1 = gx_f - (float)gx0;
    float wy1 = gy_f - (float)gy0;
    float wz1 = gz_f - (float)gz0;
    
    float wx0 = 1.0f - wx1;
    float wy0 = 1.0f - wy1;
    float wz0 = 1.0f - wz1;
    
    /* Fill 8 corners of trilinear cell */
    probe_indices[0] = probe_get_index(cascade, gx0, gy0, gz0);
    probe_indices[1] = probe_get_index(cascade, gx1, gy0, gz0);
    probe_indices[2] = probe_get_index(cascade, gx0, gy1, gz0);
    probe_indices[3] = probe_get_index(cascade, gx1, gy1, gz0);
    probe_indices[4] = probe_get_index(cascade, gx0, gy0, gz1);
    probe_indices[5] = probe_get_index(cascade, gx1, gy0, gz1);
    probe_indices[6] = probe_get_index(cascade, gx0, gy1, gz1);
    probe_indices[7] = probe_get_index(cascade, gx1, gy1, gz1);
    
    weights[0] = wx0 * wy0 * wz0;
    weights[1] = wx1 * wy0 * wz0;
    weights[2] = wx0 * wy1 * wz0;
    weights[3] = wx1 * wy1 * wz0;
    weights[4] = wx0 * wy0 * wz1;
    weights[5] = wx1 * wy0 * wz1;
    weights[6] = wx0 * wy1 * wz1;
    weights[7] = wx1 * wy1 * wz1;
}

vec3 probe_sample_irradiance(const ProbeGrid* grid, vec3 world_pos, vec3 normal) {
    if (!grid || grid->probe_count == 0) {
        return vec3_new(0.0f, 0.0f, 0.0f);
    }
    
    /* Get surrounding probes and weights */
    uint32_t probe_indices[8];
    float weights[8];
    probe_get_interpolation(grid, world_pos, probe_indices, weights);
    
    vec3 irradiance = vec3_new(0.0f, 0.0f, 0.0f);
    
    /* Evaluate SH for normal at each probe and accumulate weighted irradiance */
    float basis[PHOTON_SH_COEFF_COUNT];
    sh_evaluate_basis(normal, basis);
    
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t probe_idx = probe_indices[i];
        float weight = weights[i];
        
        if (probe_idx >= grid->probe_count) {
            continue;
        }
        
        const IrradianceProbe* probe = &grid->probes[probe_idx];
        
        /* Reconstruct irradiance for each color channel */
        vec3 probe_irradiance = vec3_new(0.0f, 0.0f, 0.0f);
        
        for (uint32_t c = 0; c < 3; c++) {
            float* sh_coeffs = (float*)&probe->irradiance_sh[c * PHOTON_SH_COEFF_COUNT];
            float channel_value = 0.0f;
            
            for (uint32_t k = 0; k < PHOTON_SH_COEFF_COUNT; k++) {
                channel_value += sh_coeffs[k] * basis[k];
            }
            
            if (c == 0) probe_irradiance.x = channel_value;
            else if (c == 1) probe_irradiance.y = channel_value;
            else probe_irradiance.z = channel_value;
        }
        
        /* Accumulate weighted contribution */
        irradiance = vec3_add(irradiance, vec3_scale(probe_irradiance, weight));
    }
    
    /* Ensure non-negative (SH can produce negative values) */
    irradiance.x = photon_maxf(0.0f, irradiance.x);
    irradiance.y = photon_maxf(0.0f, irradiance.y);
    irradiance.z = photon_maxf(0.0f, irradiance.z);
    
    return irradiance;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GPU Buffer Packing                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

ProbeGPUBuffer probe_pack_for_gpu(const ProbeGrid* grid) {
    ProbeGPUBuffer buf = {0};
    
    if (!grid || grid->probe_count == 0) {
        return buf;
    }
    
    /* Calculate buffer sizes */
    size_t sh_data_size = grid->probe_count * PHOTON_SH_COEFF_COUNT * 3 * sizeof(float);
    size_t visibility_data_size = grid->probe_count * 64 * sizeof(float);
    size_t position_data_size = grid->probe_count * 3 * sizeof(float);
    
    buf.total_size = sh_data_size + visibility_data_size + position_data_size;
    
    /* Allocate contiguous buffer */
    void* gpu_buffer = malloc(buf.total_size);
    if (!gpu_buffer) {
        return buf;
    }
    
    /* Set pointers into the contiguous buffer */
    buf.sh_data = gpu_buffer;
    buf.visibility_data = (uint8_t*)gpu_buffer + sh_data_size;
    buf.position_data = (uint8_t*)gpu_buffer + sh_data_size + visibility_data_size;
    
    /* Pack SH data */
    float* sh_ptr = (float*)buf.sh_data;
    for (uint32_t i = 0; i < grid->probe_count; i++) {
        memcpy(sh_ptr, grid->probes[i].irradiance_sh, 
               PHOTON_SH_COEFF_COUNT * 3 * sizeof(float));
        sh_ptr += PHOTON_SH_COEFF_COUNT * 3;
    }
    
    /* Pack visibility data */
    float* vis_ptr = (float*)buf.visibility_data;
    for (uint32_t i = 0; i < grid->probe_count; i++) {
        memcpy(vis_ptr, grid->probes[i].visibility, 64 * sizeof(float));
        vis_ptr += 64;
    }
    
    /* Pack position data */
    float* pos_ptr = (float*)buf.position_data;
    for (uint32_t i = 0; i < grid->probe_count; i++) {
        memcpy(pos_ptr, grid->probes[i].world_pos, 3 * sizeof(float));
        pos_ptr += 3;
    }
    
    return buf;
}

void probe_gpu_buffer_destroy(ProbeGPUBuffer* buf) {
    if (buf && buf->sh_data) {
        free(buf->sh_data);
        buf->sh_data = NULL;
        buf->visibility_data = NULL;
        buf->position_data = NULL;
        buf->total_size = 0;
    }
}
