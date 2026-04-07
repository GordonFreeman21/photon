/*
 * Photon - Denoising Logic Implementation
 * À-Trous wavelet filter with temporal accumulation and firefly clamping
 */

#include "../include/denoise.h"
#include "../include/math_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  State Management                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * denoise_create: Allocate temporal buffers and initialize state
 * Allocates two TemporalPixel buffers (current + previous frame)
 * Each buffer is width*height TemporalPixels
 */
DenoiseState denoise_create(uint32_t width, uint32_t height)
{
    DenoiseState state = {0};
    state.width = width;
    state.height = height;
    
    /* Set default parameters */
    state.atrous_iterations = 5;
    state.sigma_normal = 128.0f;
    state.sigma_depth = 1.0f;
    state.sigma_luminance = 4.0f;
    state.temporal_blend_factor = 0.9f;
    state.firefly_threshold = 10.0f;
    
    /* Allocate temporal buffers */
    size_t buffer_size = (size_t)width * height;
    state.temporal_buffer = (TemporalPixel*)malloc(buffer_size * sizeof(TemporalPixel));
    state.temporal_buffer_prev = (TemporalPixel*)malloc(buffer_size * sizeof(TemporalPixel));
    
    /* Initialize buffers to zero */
    if (state.temporal_buffer != NULL) {
        memset(state.temporal_buffer, 0, buffer_size * sizeof(TemporalPixel));
    }
    if (state.temporal_buffer_prev != NULL) {
        memset(state.temporal_buffer_prev, 0, buffer_size * sizeof(TemporalPixel));
    }
    
    return state;
}

/**
 * denoise_destroy: Free temporal buffers
 */
void denoise_destroy(DenoiseState* state)
{
    if (state == NULL) return;
    
    if (state->temporal_buffer != NULL) {
        free(state->temporal_buffer);
        state->temporal_buffer = NULL;
    }
    
    if (state->temporal_buffer_prev != NULL) {
        free(state->temporal_buffer_prev);
        state->temporal_buffer_prev = NULL;
    }
    
    state->width = 0;
    state->height = 0;
}

/**
 * denoise_resize: Reallocate buffers at new resolution
 * Frees old buffers and allocates new ones
 */
void denoise_resize(DenoiseState* state, uint32_t width, uint32_t height)
{
    if (state == NULL) return;
    
    /* Free old buffers */
    denoise_destroy(state);
    
    /* Allocate new buffers */
    *state = denoise_create(width, height);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Kernel Generation                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * denoise_generate_atrous_kernel: Generate Gaussian kernel for À-Trous wavelet
 * 
 * Computes kernel[i] = exp(-(i-radius)^2 / (2*sigma^2))
 * Normalizes kernel so sum equals 1.0
 * Sets *kernel_size = 2*radius+1
 * 
 * Parameters:
 *   sigma: standard deviation of Gaussian
 *   radius: kernel half-width (kernel spans from -radius to +radius)
 *   kernel: output array (must be allocated by caller)
 *   kernel_size: output parameter for actual kernel size
 */
void denoise_generate_atrous_kernel(float sigma, int radius, float* kernel, int* kernel_size)
{
    if (kernel == NULL || kernel_size == NULL || sigma <= 0.0f) {
        if (kernel_size != NULL) *kernel_size = 0;
        return;
    }
    
    int size = 2 * radius + 1;
    float inv_sigma_sq = 1.0f / (2.0f * sigma * sigma);
    float sum = 0.0f;
    
    /* Compute unnormalized Gaussian kernel */
    for (int i = 0; i < size; i++) {
        int offset = i - radius;
        float val = expf(-(float)(offset * offset) * inv_sigma_sq);
        kernel[i] = val;
        sum += val;
    }
    
    /* Normalize so sum equals 1.0 */
    if (sum > FLT_EPSILON) {
        float inv_sum = 1.0f / sum;
        for (int i = 0; i < size; i++) {
            kernel[i] *= inv_sum;
        }
    }
    
    *kernel_size = size;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Edge-Stopping Functions                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * denoise_edge_stop_normal: Compute edge-stopping weight based on normals
 * 
 * Returns pow(max(0, dot(n1, n2)), 128)
 * High when normals are aligned, low at edges
 */
float denoise_edge_stop_normal(vec3 n1, vec3 n2)
{
    float dot_product = vec3_dot(n1, n2);
    float clamped = photon_maxf(0.0f, dot_product);
    
    /* pow(x, 128) = x^128 */
    return powf(clamped, 128.0f);
}

/**
 * denoise_edge_stop_depth: Compute edge-stopping weight based on depth discontinuities
 * 
 * Returns exp(-|z1 - z2| / (gradient + epsilon))
 * High when depths are similar, low at depth edges
 */
float denoise_edge_stop_depth(float z1, float z2, float gradient)
{
    float depth_diff = fabsf(z1 - z2);
    float denom = gradient + PHOTON_EPSILON;
    return expf(-depth_diff / denom);
}

/**
 * denoise_edge_stop_luminance: Compute edge-stopping weight based on color similarity
 * 
 * Computes luminance using BT.709: L = 0.2126*R + 0.7152*G + 0.0722*B
 * Returns exp(-|l1 - l2| / (sigma + epsilon))
 */
float denoise_edge_stop_luminance(vec3 c1, vec3 c2, float sigma)
{
    /* BT.709 luminance coefficients */
    float l1 = 0.2126f * c1.x + 0.7152f * c1.y + 0.0722f * c1.z;
    float l2 = 0.2126f * c2.x + 0.7152f * c2.y + 0.0722f * c2.z;
    
    float luminance_diff = fabsf(l1 - l2);
    float denom = sigma + PHOTON_EPSILON;
    return expf(-luminance_diff / denom);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Temporal Accumulation                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * denoise_temporal_update: Update temporal blending factor
 * 
 * Sets the blend factor for mixing current and previous frame data
 * blend_factor ranges from 0.0 (all current) to 1.0 (all previous)
 */
void denoise_temporal_update(DenoiseState* state, float blend_factor)
{
    if (state == NULL) return;
    
    /* Clamp to valid range */
    state->temporal_blend_factor = photon_clampf(blend_factor, 0.0f, 1.0f);
}

/**
 * denoise_swap_buffers: Swap temporal buffers for next frame
 * 
 * After denoising completes, swap current buffer to previous
 * so next frame can blend with this frame's results
 */
void denoise_swap_buffers(DenoiseState* state)
{
    if (state == NULL) return;
    
    /* Swap pointers */
    TemporalPixel* temp = state->temporal_buffer;
    state->temporal_buffer = state->temporal_buffer_prev;
    state->temporal_buffer_prev = temp;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Firefly Detection & Clamping                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * denoise_firefly_threshold: Compute threshold for firefly clamping
 * 
 * Fireflies are single-sample noise spikes (high energy pixels).
 * Threshold = median + sigma_multiplier * standard_deviation
 * 
 * Computes statistics from neighborhood luminance values to determine
 * a per-pixel threshold for clamping outliers.
 */
float denoise_firefly_threshold(const float* neighborhood_luminance,
                                 uint32_t count, float sigma_multiplier)
{
    if (neighborhood_luminance == NULL || count == 0) {
        return FLT_MAX;
    }
    
    if (count == 1) {
        return neighborhood_luminance[0] * sigma_multiplier;
    }
    
    /* Compute median */
    float* sorted = (float*)malloc(count * sizeof(float));
    if (sorted == NULL) return FLT_MAX;
    
    memcpy(sorted, neighborhood_luminance, count * sizeof(float));
    
    /* Simple selection sort for median */
    for (uint32_t i = 0; i < count / 2 + 1; i++) {
        uint32_t min_idx = i;
        for (uint32_t j = i + 1; j < count; j++) {
            if (sorted[j] < sorted[min_idx]) {
                min_idx = j;
            }
        }
        float temp = sorted[i];
        sorted[i] = sorted[min_idx];
        sorted[min_idx] = temp;
    }
    
    float median = sorted[count / 2];
    
    /* Compute standard deviation */
    float mean = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        mean += neighborhood_luminance[i];
    }
    mean /= (float)count;
    
    float variance = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        float diff = neighborhood_luminance[i] - mean;
        variance += diff * diff;
    }
    variance /= (float)count;
    
    float stddev = sqrtf(variance);
    
    free(sorted);
    
    /* Return threshold: median + sigma_multiplier * stddev */
    return median + sigma_multiplier * stddev;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Denoise Configuration Presets                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * denoise_config_shadow: Configuration for shadow pass denoising
 * 
 * Shadow rays are relatively clean but benefit from normal/depth-aware filtering
 * Settings:
 *   - Normal edge-stopping: aggressive (sigma=128)
 *   - Depth edge-stopping: moderate (sigma=1.0)
 *   - Luminance edge-stopping: moderate (sigma=4.0)
 *   - Iterations: 5 À-Trous wavelet levels
 *   - Temporal: strong blending (0.9)
 *   - Firefly: disabled (shadow rarely has spikes)
 */
DenoisePassConfig denoise_config_shadow(void)
{
    return (DenoisePassConfig){
        .sigma_normal = 128.0f,
        .sigma_depth = 1.0f,
        .sigma_luminance = 4.0f,
        .iterations = 5,
        .temporal_blend = 0.9f,
        .enable_firefly_clamp = false,
        .firefly_sigma = 0.0f
    };
}

/**
 * denoise_config_reflection: Configuration for specular reflection pass
 * 
 * Reflections are noisier, especially at grazing angles
 * Settings:
 *   - Normal edge-stopping: aggressive (same as shadow)
 *   - Depth edge-stopping: moderate (same as shadow)
 *   - Luminance edge-stopping: aggressive (sigma=2.0 for more spatial coherence)
 *   - Iterations: 5 À-Trous wavelet levels
 *   - Temporal: moderate blending (0.85 to preserve frame-to-frame detail)
 *   - Firefly: enabled with 3.0 sigma threshold
 */
DenoisePassConfig denoise_config_reflection(void)
{
    return (DenoisePassConfig){
        .sigma_normal = 128.0f,
        .sigma_depth = 1.0f,
        .sigma_luminance = 2.0f,
        .iterations = 5,
        .temporal_blend = 0.85f,
        .enable_firefly_clamp = true,
        .firefly_sigma = 3.0f
    };
}

/**
 * denoise_config_gi: Configuration for global illumination pass
 * 
 * GI is the noisiest pass, especially in dark areas
 * Settings:
 *   - Normal edge-stopping: not specified, uses default
 *   - Depth edge-stopping: not specified, uses default
 *   - Luminance edge-stopping: relaxed (sigma=8.0 for more blending)
 *   - Iterations: 5 À-Trous wavelet levels
 *   - Temporal: very strong blending (0.95 for stability)
 *   - Firefly: enabled with default sigma threshold
 */
DenoisePassConfig denoise_config_gi(void)
{
    return (DenoisePassConfig){
        .sigma_normal = 128.0f,     /* default */
        .sigma_depth = 1.0f,         /* default */
        .sigma_luminance = 8.0f,
        .iterations = 5,
        .temporal_blend = 0.95f,
        .enable_firefly_clamp = true,
        .firefly_sigma = 3.0f        /* default from reflection */
    };
}
