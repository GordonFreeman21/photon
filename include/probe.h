/*
 * Photon - Irradiance Probe Grid (DDGI-inspired)
 */
#ifndef PHOTON_PROBE_H
#define PHOTON_PROBE_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Probe Grid Management                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

ProbeGrid   probe_grid_create(vec3 origin, float cascade_spacings[PHOTON_PROBE_CASCADE_COUNT]);
void        probe_grid_destroy(ProbeGrid* grid);

/* Reposition grid around camera */
void probe_grid_update_position(ProbeGrid* grid, vec3 camera_pos);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Probe Updates                                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Select which probes to update this frame (round-robin + priority) */
void probe_select_updates(ProbeGrid* grid, uint32_t* probe_indices,
                          uint32_t* count, uint32_t max_updates);

/* CPU-side: generate ray directions for probe update */
void probe_generate_ray_directions(vec3* directions, uint32_t count, uint32_t frame_index);

/* Update probe SH coefficients from traced results */
void probe_update_irradiance(IrradianceProbe* probe, const vec3* directions,
                             const vec3* radiance, uint32_t sample_count, float hysteresis);

/* Update probe visibility (depth) from traced results */
void probe_update_visibility(IrradianceProbe* probe, const vec3* directions,
                             const float* distances, uint32_t sample_count, float hysteresis);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Probe Sampling (for shading)                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Sample irradiance at a world position with surface normal */
vec3 probe_sample_irradiance(const ProbeGrid* grid, vec3 world_pos, vec3 normal);

/* Get the 8 surrounding probes and interpolation weights */
void probe_get_interpolation(const ProbeGrid* grid, vec3 world_pos,
                             uint32_t probe_indices[8], float weights[8]);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GPU Buffer Packing                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    void*       sh_data;        /* packed SH coefficients for all probes */
    void*       visibility_data;/* packed visibility maps */
    void*       position_data;  /* packed probe positions */
    size_t      total_size;
} ProbeGPUBuffer;

ProbeGPUBuffer probe_pack_for_gpu(const ProbeGrid* grid);
void probe_gpu_buffer_destroy(ProbeGPUBuffer* buf);

#endif /* PHOTON_PROBE_H */
