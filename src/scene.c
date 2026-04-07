#include "../include/scene.h"
#include "../include/math_util.h"
#include "../include/memory.h"

#include <string.h>

// ============================================================================
// Scene Lifecycle
// ============================================================================

void scene_init(Scene* scene)
{
    memset(scene, 0, sizeof(Scene));

    scene->materials = material_system_create(PHOTON_MAX_MATERIALS);
    scene->textures = texture_system_create(PHOTON_MAX_TEXTURES);

    scene->tlas_dirty = true;
    scene->world_bounds = aabb_empty();
}

void scene_destroy(Scene* scene)
{
    if (!scene) {
        return;
    }

    // Destroy all mesh BLASes
    for (uint32_t i = 0; i < scene->mesh_count; i++) {
        if (scene->meshes[i].blas_nodes) {
            BVH temp = {0};
            temp.nodes = scene->meshes[i].blas_nodes;
            temp.node_count = scene->meshes[i].blas_node_count;
            bvh_destroy(&temp);
            scene->meshes[i].blas_nodes = NULL;
        }
    }

    // Destroy TLAS
    bvh_destroy(&scene->tlas);

    // Destroy subsystems
    material_system_destroy(&scene->materials);
    texture_system_destroy(&scene->textures);
}

// ============================================================================
// Mesh Management
// ============================================================================

uint32_t scene_add_mesh(Scene* scene, const RTMeshData* mesh_data)
{
    if (!scene || !mesh_data || scene->mesh_count >= PHOTON_MAX_MESHES) {
        return PHOTON_INVALID_ID;
    }

    uint32_t slot = scene->mesh_count;

    // Copy mesh data into the scene
    scene->meshes[slot].data = *mesh_data;

    // Build BLAS for this mesh with default options
    BVHBuildOptions blas_options = bvh_default_options();
    BVH blas = bvh_build_mesh(mesh_data, &blas_options);
    if (!blas.nodes) {
        return PHOTON_INVALID_ID;
    }
    scene->meshes[slot].blas_nodes = blas.nodes;
    scene->meshes[slot].blas_node_count = blas.node_count;
    scene->meshes[slot].blas_root = 0;
    scene->meshes[slot].dirty = false;

    scene->mesh_count++;
    scene->tlas_dirty = true;

    return slot;
}

void scene_remove_mesh(Scene* scene, uint32_t mesh_id)
{
    if (!scene || mesh_id >= scene->mesh_count) {
        return;
    }

    // Destroy BLAS
    if (scene->meshes[mesh_id].blas_nodes) {
        BVH temp = {0};
        temp.nodes = scene->meshes[mesh_id].blas_nodes;
        temp.node_count = scene->meshes[mesh_id].blas_node_count;
        bvh_destroy(&temp);
        scene->meshes[mesh_id].blas_nodes = NULL;
    }

    // Shift remaining meshes down to fill the gap
    if (mesh_id < scene->mesh_count - 1) {
        for (uint32_t i = mesh_id; i < scene->mesh_count - 1; i++) {
            scene->meshes[i] = scene->meshes[i + 1];
        }
    }

    scene->mesh_count--;
    scene->tlas_dirty = true;
}

// ============================================================================
// Instance Management
// ============================================================================

uint32_t scene_add_instance(Scene* scene, uint32_t mesh_id, const float transform[16])
{
    if (!scene || mesh_id >= scene->mesh_count || scene->instance_count >= PHOTON_MAX_INSTANCES) {
        return PHOTON_INVALID_ID;
    }

    uint32_t idx = scene->instance_count;

    // Store mesh ID and transform
    scene->instances[idx].mesh_id = mesh_id;
    memcpy(scene->instances[idx].transform, transform, sizeof(float) * 16);

    // Compute inverse transform
    mat4 t;
    memcpy(t.m, transform, sizeof(float) * 16);
    mat4 inv = mat4_inverse(t);
    memcpy(scene->instances[idx].inv_transform, inv.m, sizeof(float) * 16);

    // Compute world bounds by transforming mesh AABB
    AABB mesh_aabb = scene->meshes[mesh_id].data.bounds;
    scene->instances[idx].world_bounds = aabb_transform(mesh_aabb, t);

    // Update scene world bounds
    scene->world_bounds = aabb_union(scene->world_bounds, scene->instances[idx].world_bounds);

    scene->instance_count++;
    scene->tlas_dirty = true;

    return idx;
}

void scene_remove_instance(Scene* scene, uint32_t inst_id)
{
    if (!scene || inst_id >= scene->instance_count) {
        return;
    }

    // Shift remaining instances down to fill the gap
    if (inst_id < scene->instance_count - 1) {
        for (uint32_t i = inst_id; i < scene->instance_count - 1; i++) {
            scene->instances[i] = scene->instances[i + 1];
        }
    }

    scene->instance_count--;
    scene->tlas_dirty = true;
}

void scene_update_instance_transform(Scene* scene, uint32_t inst_id, const float transform[16])
{
    if (!scene || inst_id >= scene->instance_count) {
        return;
    }

    RTInstance* instance = &scene->instances[inst_id];

    // Update transform
    memcpy(instance->transform, transform, sizeof(float) * 16);

    // Update inverse transform
    mat4 t;
    memcpy(t.m, transform, sizeof(float) * 16);
    mat4 inv = mat4_inverse(t);
    memcpy(instance->inv_transform, inv.m, sizeof(float) * 16);

    // Update world bounds
    AABB mesh_aabb = scene->meshes[instance->mesh_id].data.bounds;
    instance->world_bounds = aabb_transform(mesh_aabb, t);

    scene->tlas_dirty = true;
}

// ============================================================================
// Light Management
// ============================================================================

uint32_t scene_add_light(Scene* scene, const RTLight* light)
{
    if (!scene || !light || scene->light_count >= PHOTON_MAX_LIGHTS) {
        return PHOTON_INVALID_ID;
    }

    uint32_t idx = scene->light_count;
    scene->lights[idx] = *light;
    scene->light_count++;

    return idx;
}

void scene_remove_light(Scene* scene, uint32_t light_id)
{
    if (!scene || light_id >= scene->light_count) {
        return;
    }

    // Shift remaining lights down to fill the gap
    if (light_id < scene->light_count - 1) {
        for (uint32_t i = light_id; i < scene->light_count - 1; i++) {
            scene->lights[i] = scene->lights[i + 1];
        }
    }

    scene->light_count--;
}

void scene_update_light(Scene* scene, uint32_t light_id, const RTLight* light)
{
    if (!scene || light_id >= scene->light_count || !light) {
        return;
    }

    scene->lights[light_id] = *light;
}

// ============================================================================
// Acceleration Structure Management
// ============================================================================

void scene_build_acceleration(Scene* scene)
{
    if (!scene) {
        return;
    }

    // Build BLAS for all meshes
    BVHBuildOptions blas_options = bvh_default_options();
    for (uint32_t i = 0; i < scene->mesh_count; i++) {
        if (!scene->meshes[i].blas_nodes) {
            BVH blas = bvh_build_mesh(&scene->meshes[i].data, &blas_options);
            scene->meshes[i].blas_nodes = blas.nodes;
            scene->meshes[i].blas_node_count = blas.node_count;
            scene->meshes[i].blas_root = 0;
            scene->meshes[i].dirty = false;
        }
    }

    // Destroy existing TLAS
    bvh_destroy(&scene->tlas);

    // Build TLAS from all instances
    if (scene->instance_count > 0) {
        BVHBuildOptions tlas_options = bvh_default_options();
        scene->tlas = bvh_build_tlas(
            scene->instances,
            scene->instance_count,
            &tlas_options
        );
    }

    scene->tlas_dirty = false;
}

void scene_refit_acceleration(Scene* scene)
{
    if (!scene) {
        return;
    }

    // Rebuild dirty BLASes
    BVHBuildOptions blas_options = bvh_default_options();
    for (uint32_t i = 0; i < scene->mesh_count; i++) {
        if (scene->meshes[i].dirty && scene->meshes[i].blas_nodes) {
            BVH temp = {0};
            temp.nodes = scene->meshes[i].blas_nodes;
            temp.node_count = scene->meshes[i].blas_node_count;
            bvh_destroy(&temp);

            BVH blas = bvh_build_mesh(&scene->meshes[i].data, &blas_options);
            scene->meshes[i].blas_nodes = blas.nodes;
            scene->meshes[i].blas_node_count = blas.node_count;
            scene->meshes[i].blas_root = 0;
            scene->meshes[i].dirty = false;
        }
    }

    // Refit TLAS
    if (scene->tlas.node_count > 0 && scene->instance_count > 0) {
        bvh_refit_tlas(&scene->tlas, scene->instances, scene->instance_count);
    }

    scene->tlas_dirty = false;
}

void scene_commit(Scene* scene)
{
    if (!scene) {
        return;
    }

    // Check and rebuild if necessary
    if (scene->tlas_dirty) {
        if (scene->tlas.node_count == 0 || scene->instance_count == 0) {
            // Need full rebuild
            scene_build_acceleration(scene);
        } else {
            // Can refit instead of rebuild
            scene_refit_acceleration(scene);
        }
    }

    // Recompute world bounds from all instances
    scene->world_bounds = aabb_empty();
    for (uint32_t i = 0; i < scene->instance_count; i++) {
        scene->world_bounds = aabb_union(scene->world_bounds, scene->instances[i].world_bounds);
    }

    scene->tlas_dirty = false;
}
