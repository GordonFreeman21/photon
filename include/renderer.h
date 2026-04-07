/*
 * Photon - Adaptive Hybrid Ray Tracer
 * Public API
 */
#ifndef PHOTON_RENDERER_H
#define PHOTON_RENDERER_H

#include "photon_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Opaque Renderer Handle                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct RTRenderer RTRenderer;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Initialization & Shutdown                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

RTRenderer* rt_renderer_create(const RTRendererDesc* desc);
void        rt_renderer_destroy(RTRenderer* renderer);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Scene Management                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t    rt_add_mesh(RTRenderer* r, const RTMeshData* mesh);
void        rt_remove_mesh(RTRenderer* r, uint32_t mesh_id);

uint32_t    rt_add_instance(RTRenderer* r, uint32_t mesh_id, const float transform[16]);
void        rt_remove_instance(RTRenderer* r, uint32_t inst_id);
void        rt_update_instance_transform(RTRenderer* r, uint32_t inst_id, const float transform[16]);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Lights                                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t    rt_add_directional_light(RTRenderer* r, const RTDirectionalLight* light);
uint32_t    rt_add_point_light(RTRenderer* r, const RTPointLight* light);
uint32_t    rt_add_spot_light(RTRenderer* r, const RTSpotLight* light);
void        rt_update_light(RTRenderer* r, uint32_t light_id, const void* light_data);
void        rt_remove_light(RTRenderer* r, uint32_t light_id);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Materials                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

uint32_t    rt_add_material(RTRenderer* r, const RTMaterial* mat);
uint32_t    rt_load_texture(RTRenderer* r, const char* path);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Camera                                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

void        rt_set_camera(RTRenderer* r, const RTCamera* camera);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Rendering                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

void        rt_render_frame(RTRenderer* r);
uint32_t    rt_get_output_texture(RTRenderer* r);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Performance & Quality                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

void            rt_set_quality_preset(RTRenderer* r, RTQualityPreset preset);
void            rt_set_ray_budget(RTRenderer* r, uint32_t rays_per_frame);
RTFrameStats    rt_get_last_frame_stats(RTRenderer* r);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Debug                                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

void        rt_set_debug_vis(RTRenderer* r, DebugVisMode mode);
void        rt_capture_frame(RTRenderer* r, const char* output_path);

#ifdef __cplusplus
}
#endif

#endif /* PHOTON_RENDERER_H */
