/*
 * Photon - Ray Budget Manager
 */
#ifndef PHOTON_BUDGET_H
#define PHOTON_BUDGET_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Budget Manager                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

RayBudgetManager    budget_create(uint32_t initial_budget, float target_fps);
void                budget_destroy(RayBudgetManager* mgr);

/* Called at start of frame: computes tile priorities and sample multipliers */
void budget_begin_frame(RayBudgetManager* mgr, uint32_t screen_width, uint32_t screen_height);

/* Report frame time to drive adaptive feedback */
void budget_end_frame(RayBudgetManager* mgr, float frame_time_ms);

/* Get sample multiplier for a given tile */
float budget_get_tile_multiplier(const RayBudgetManager* mgr, uint32_t tile_x, uint32_t tile_y);

/* Check if ray budget allows more rays */
bool budget_can_trace(const RayBudgetManager* mgr, uint32_t ray_count);

/* Spend rays from budget */
void budget_spend_rays(RayBudgetManager* mgr, uint32_t count);

/* Borrow rays from future frames (for important events) */
void budget_borrow_rays(RayBudgetManager* mgr, uint32_t count);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Priority Heatmap Update                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Update tile priorities based on screen analysis */
void budget_update_priorities(RayBudgetManager* mgr,
                              const float* luminance_variance,
                              const float* motion_magnitude,
                              const uint8_t* material_flags,
                              uint32_t screen_width, uint32_t screen_height);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Quality Preset Integration                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    ray_budget;
    float       shadow_resolution;
    float       reflection_resolution;
    float       gi_resolution;
    uint32_t    shadow_spp;
    uint32_t    reflection_spp;
    uint32_t    gi_spp;
    uint32_t    denoise_iterations;
    float       temporal_blend;
} QualityConfig;

QualityConfig quality_get_preset(RTQualityPreset preset);
void budget_apply_quality(RayBudgetManager* mgr, const QualityConfig* config);

#endif /* PHOTON_BUDGET_H */
