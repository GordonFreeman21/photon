/*
 * Photon - Denoising Logic
 */
#ifndef PHOTON_DENOISE_H
#define PHOTON_DENOISE_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Denoise State Management                                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

DenoiseState    denoise_create(uint32_t width, uint32_t height);
void            denoise_destroy(DenoiseState* state);
void            denoise_resize(DenoiseState* state, uint32_t width, uint32_t height);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  CPU-side kernel generation (uploaded to GPU as uniforms)                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Generate Gaussian kernel for À-Trous wavelet filter */
void denoise_generate_atrous_kernel(float sigma, int radius, float* kernel, int* kernel_size);

/* Compute edge-stopping function weights */
float denoise_edge_stop_normal(vec3 n1, vec3 n2);
float denoise_edge_stop_depth(float z1, float z2, float gradient);
float denoise_edge_stop_luminance(vec3 c1, vec3 c2, float sigma);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Temporal Accumulation (CPU-side control, GPU-side execution)               */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Update temporal accumulation parameters */
void denoise_temporal_update(DenoiseState* state, float blend_factor);

/* Swap temporal buffers for next frame */
void denoise_swap_buffers(DenoiseState* state);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Firefly Clamping                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Compute firefly threshold from neighborhood statistics */
float denoise_firefly_threshold(const float* neighborhood_luminance,
                                 uint32_t count, float sigma_multiplier);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Denoise Configuration per pass                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float   sigma_normal;
    float   sigma_depth;
    float   sigma_luminance;
    uint32_t iterations;
    float   temporal_blend;
    bool    enable_firefly_clamp;
    float   firefly_sigma;
} DenoisePassConfig;

DenoisePassConfig denoise_config_shadow(void);
DenoisePassConfig denoise_config_reflection(void);
DenoisePassConfig denoise_config_gi(void);

#endif /* PHOTON_DENOISE_H */
