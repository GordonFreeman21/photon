#include "../include/budget.h"
#include "../include/math_util.h"
#include <math.h>
#include <string.h>

// ============================================================================
// Budget Manager - Create/Destroy
// ============================================================================

RayBudgetManager budget_create(uint32_t initial_budget, float target_fps) {
    RayBudgetManager manager = {0};
    
    manager.total_ray_budget = initial_budget;
    manager.target_frame_time_ms = 1000.0f / target_fps;
    manager.rays_spent_this_frame = 0;
    manager.frame_number = 0;
    manager.ray_debt = 0;
    manager.adaptive_alpha = 0.1f;
    manager.debt_payback_rate = 0.2f;
    manager.last_frame_time_ms = 0.0f;
    
    // Initialize all tiles to priority 128 and multiplier 1.0
    for (uint32_t i = 0; i < PHOTON_MAX_TILE_X * PHOTON_MAX_TILE_Y; i++) {
        manager.tile_priority[i] = 128;
        manager.tile_sample_multiplier[i] = 1.0f;
    }
    
    return manager;
}

void budget_destroy(RayBudgetManager* manager) {
    // Nothing to free - all data is embedded in the struct
    (void)manager;
}

// ============================================================================
// Frame Management
// ============================================================================

void budget_begin_frame(RayBudgetManager* manager, uint32_t screen_width, uint32_t screen_height) {
    (void)screen_width;
    (void)screen_height;
    manager->rays_spent_this_frame = 0;
    manager->frame_number++;
    
    // Pay back debt by reducing it and subtracting from budget
    uint32_t payback_amount = (uint32_t)(manager->ray_debt * manager->debt_payback_rate);
    if (payback_amount > manager->ray_debt) {
        payback_amount = manager->ray_debt;
    }
    
    manager->ray_debt -= payback_amount;
    
    // Subtract payback from budget (but don't let budget go negative)
    if (payback_amount <= manager->total_ray_budget) {
        manager->total_ray_budget -= payback_amount;
    } else {
        manager->total_ray_budget = 0;
    }
}

void budget_end_frame(RayBudgetManager* manager, float frame_time_ms) {
    manager->last_frame_time_ms = frame_time_ms;
    
    float target_time = manager->target_frame_time_ms;
    
    // Adaptive feedback loop
    if (frame_time_ms > target_time * 1.1f) {
        // Frame took too long - reduce budget by 10%
        manager->total_ray_budget = (uint32_t)((float)manager->total_ray_budget * 0.9f);
    } else if (frame_time_ms < target_time * 0.8f) {
        // Frame was fast - increase budget by 5%
        manager->total_ray_budget = (uint32_t)((float)manager->total_ray_budget * 1.05f);
    }
    
    // Clamp budget to valid range [500000, 16000000]
    if (manager->total_ray_budget < 500000) {
        manager->total_ray_budget = 500000;
    }
    if (manager->total_ray_budget > 16000000) {
        manager->total_ray_budget = 16000000;
    }
}

// ============================================================================
// Tile Operations
// ============================================================================

float budget_get_tile_multiplier(const RayBudgetManager* manager, uint32_t tile_x, uint32_t tile_y) {
    if (tile_x >= PHOTON_MAX_TILE_X || tile_y >= PHOTON_MAX_TILE_Y) {
        return 1.0f;
    }
    return manager->tile_sample_multiplier[tile_y * PHOTON_MAX_TILE_X + tile_x];
}

bool budget_can_trace(const RayBudgetManager* manager, uint32_t count) {
    // Allow debt allowance (50% additional on top of budget)
    uint32_t debt_allowance = (uint32_t)((float)manager->total_ray_budget * 0.5f);
    uint32_t effective_budget = manager->total_ray_budget + debt_allowance;
    return (manager->rays_spent_this_frame + count) <= effective_budget;
}

void budget_spend_rays(RayBudgetManager* manager, uint32_t count) {
    manager->rays_spent_this_frame += count;
}

void budget_borrow_rays(RayBudgetManager* manager, uint32_t count) {
    manager->ray_debt += count;
}

// ============================================================================
// Priority Update
// ============================================================================

void budget_update_priorities(RayBudgetManager* manager,
                              const float* luminance_variance,
                              const float* motion_magnitude,
                              const uint8_t* material_flags,
                              uint32_t screen_width, uint32_t screen_height) {
    float max_priority = 0.0f;
    float priorities[PHOTON_TILE_COUNT];
    
    uint32_t tiles_x = screen_width / PHOTON_TILE_SIZE;
    uint32_t tiles_y = screen_height / PHOTON_TILE_SIZE;
    if (tiles_x > PHOTON_MAX_TILE_X) tiles_x = PHOTON_MAX_TILE_X;
    if (tiles_y > PHOTON_MAX_TILE_Y) tiles_y = PHOTON_MAX_TILE_Y;
    
    // Compute priorities for each tile
    for (uint32_t tile_y = 0; tile_y < tiles_y; tile_y++) {
        for (uint32_t tile_x = 0; tile_x < tiles_x; tile_x++) {
            float priority = 0.0f;
            
            uint32_t start_x = tile_x * PHOTON_TILE_SIZE;
            uint32_t start_y = tile_y * PHOTON_TILE_SIZE;
            uint32_t end_x = start_x + PHOTON_TILE_SIZE;
            uint32_t end_y = start_y + PHOTON_TILE_SIZE;
            
            if (end_x > screen_width) end_x = screen_width;
            if (end_y > screen_height) end_y = screen_height;
            
            uint32_t pixel_count = 0;
            float variance_sum = 0.0f;
            float motion_sum = 0.0f;
            uint32_t complex_pixels = 0;
            
            for (uint32_t y = start_y; y < end_y; y++) {
                for (uint32_t x = start_x; x < end_x; x++) {
                    uint32_t idx = y * screen_width + x;
                    if (idx < screen_width * screen_height) {
                        if (luminance_variance) {
                            variance_sum += luminance_variance[idx];
                        }
                        if (motion_magnitude) {
                            motion_sum += motion_magnitude[idx];
                        }
                        if (material_flags) {
                            if (material_flags[idx] & (MAT_FLAG_MIRROR | MAT_FLAG_WATER)) {
                                complex_pixels++;
                            }
                        }
                        pixel_count++;
                    }
                }
            }
            
            if (pixel_count > 0) {
                priority += sqrtf(variance_sum / (float)pixel_count);
                priority += motion_sum / (float)pixel_count;
                priority += (float)complex_pixels / (float)pixel_count * 2.0f;
            }
            
            // Apply distance-from-center falloff (center=higher priority)
            float center_x = (float)screen_width / 2.0f;
            float center_y = (float)screen_height / 2.0f;
            float tile_center_x = (float)start_x + (float)PHOTON_TILE_SIZE / 2.0f;
            float tile_center_y = (float)start_y + (float)PHOTON_TILE_SIZE / 2.0f;
            
            float dx = tile_center_x - center_x;
            float dy = tile_center_y - center_y;
            float dist_to_center = sqrtf(dx * dx + dy * dy);
            
            float max_dist = sqrtf(center_x * center_x + center_y * center_y);
            if (max_dist > 0.0f) {
                float center_falloff = 1.0f - (dist_to_center / max_dist) * 0.5f;
                priority *= center_falloff;
            }
            
            priorities[tile_y * PHOTON_MAX_TILE_X + tile_x] = priority;
            if (priority > max_priority) {
                max_priority = priority;
            }
        }
    }
    
    // Map to 0-255 priority and compute sample multiplier (0.25-2.0 range)
    if (max_priority > 0.0f) {
        for (uint32_t tile_y = 0; tile_y < tiles_y; tile_y++) {
            for (uint32_t tile_x = 0; tile_x < tiles_x; tile_x++) {
                uint32_t tile_idx = tile_y * PHOTON_MAX_TILE_X + tile_x;
                float normalized = priorities[tile_idx] / max_priority;
                if (normalized < 0.0f) normalized = 0.0f;
                if (normalized > 1.0f) normalized = 1.0f;
                
                manager->tile_priority[tile_idx] = (uint8_t)(normalized * 255.0f);
                manager->tile_sample_multiplier[tile_idx] = 0.25f + normalized * 1.75f;
            }
        }
    } else {
        for (uint32_t i = 0; i < PHOTON_TILE_COUNT; i++) {
            manager->tile_priority[i] = 128;
            manager->tile_sample_multiplier[i] = 1.0f;
        }
    }
}

// ============================================================================
// Quality Presets
// ============================================================================

QualityConfig quality_get_preset(RTQualityPreset preset) {
    QualityConfig config = {0};
    
    switch (preset) {
        case RT_QUALITY_POTATO:
            config.ray_budget = 1000000;
            config.shadow_resolution = 0.25f;
            config.reflection_resolution = 0.125f;
            config.gi_resolution = 0.125f;
            config.shadow_spp = 1;
            config.reflection_spp = 1;
            config.gi_spp = 1;
            config.denoise_iterations = 5;
            config.temporal_blend = 0.95f;
            break;
            
        case RT_QUALITY_LOW:
            config.ray_budget = 2000000;
            config.shadow_resolution = 0.5f;
            config.reflection_resolution = 0.25f;
            config.gi_resolution = 0.25f;
            config.shadow_spp = 2;
            config.reflection_spp = 1;
            config.gi_spp = 1;
            config.denoise_iterations = 3;
            config.temporal_blend = 0.85f;
            break;
            
        case RT_QUALITY_MEDIUM:
            config.ray_budget = 4000000;
            config.shadow_resolution = 1.0f;
            config.reflection_resolution = 0.5f;
            config.gi_resolution = 0.5f;
            config.shadow_spp = 4;
            config.reflection_spp = 2;
            config.gi_spp = 2;
            config.denoise_iterations = 2;
            config.temporal_blend = 0.75f;
            break;
            
        case RT_QUALITY_HIGH:
            config.ray_budget = 8000000;
            config.shadow_resolution = 1.0f;
            config.reflection_resolution = 1.0f;
            config.gi_resolution = 0.5f;
            config.shadow_spp = 8;
            config.reflection_spp = 4;
            config.gi_spp = 2;
            config.denoise_iterations = 1;
            config.temporal_blend = 0.5f;
            break;
            
        case RT_QUALITY_ULTRA:
            config.ray_budget = 16000000;
            config.shadow_resolution = 1.0f;
            config.reflection_resolution = 1.0f;
            config.gi_resolution = 1.0f;
            config.shadow_spp = 16;
            config.reflection_spp = 8;
            config.gi_spp = 4;
            config.denoise_iterations = 0;
            config.temporal_blend = 0.25f;
            break;
            
        default:
            config.ray_budget = 4000000;
            config.shadow_resolution = 1.0f;
            config.reflection_resolution = 0.5f;
            config.gi_resolution = 0.5f;
            config.shadow_spp = 4;
            config.reflection_spp = 2;
            config.gi_spp = 2;
            config.denoise_iterations = 2;
            config.temporal_blend = 0.75f;
            break;
    }
    
    return config;
}

void budget_apply_quality(RayBudgetManager* manager, const QualityConfig* config) {
    manager->total_ray_budget = config->ray_budget;
}
