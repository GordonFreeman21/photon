/*
 * Photon - Scene Management
 */
#ifndef PHOTON_SCENE_H
#define PHOTON_SCENE_H

#include "photon_types.h"
#include "bvh.h"
#include "material.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Scene                                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Meshes */
    RTMesh          meshes[PHOTON_MAX_MESHES];
    uint32_t        mesh_count;

    /* Instances */
    RTInstance      instances[PHOTON_MAX_INSTANCES];
    uint32_t        instance_count;

    /* Lights */
    RTLight         lights[PHOTON_MAX_LIGHTS];
    uint32_t        light_count;

    /* Acceleration structures */
    BVH             tlas;
    bool            tlas_dirty;

    /* Materials & Textures */
    MaterialSystem  materials;
    TextureSystem   textures;

    /* Scene bounds */
    AABB            world_bounds;
} Scene;

void        scene_init(Scene* scene);
void        scene_destroy(Scene* scene);

/* Mesh management */
uint32_t    scene_add_mesh(Scene* scene, const RTMeshData* mesh_data);
void        scene_remove_mesh(Scene* scene, uint32_t mesh_id);

/* Instance management */
uint32_t    scene_add_instance(Scene* scene, uint32_t mesh_id, const float transform[16]);
void        scene_remove_instance(Scene* scene, uint32_t inst_id);
void        scene_update_instance_transform(Scene* scene, uint32_t inst_id, const float transform[16]);

/* Light management */
uint32_t    scene_add_light(Scene* scene, const RTLight* light);
void        scene_remove_light(Scene* scene, uint32_t light_id);
void        scene_update_light(Scene* scene, uint32_t light_id, const RTLight* light);

/* Build / update acceleration structures */
void        scene_build_acceleration(Scene* scene);
void        scene_refit_acceleration(Scene* scene);

/* Commit changes: rebuild dirty BLAS, refit/rebuild TLAS */
void        scene_commit(Scene* scene);

#endif /* PHOTON_SCENE_H */
