/*
 * Photon - Material System (PBR metallic-roughness)
 */
#ifndef PHOTON_MATERIAL_H
#define PHOTON_MATERIAL_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Material Management                                                       */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    RTMaterial* materials;
    uint32_t    count;
    uint32_t    capacity;
} MaterialSystem;

MaterialSystem  material_system_create(uint32_t capacity);
void            material_system_destroy(MaterialSystem* sys);
uint32_t        material_add(MaterialSystem* sys, const RTMaterial* mat);
RTMaterial*     material_get(MaterialSystem* sys, uint32_t id);

/* Default material */
RTMaterial material_default(void);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BRDF Evaluation                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec3 diffuse;
    vec3 specular;
    float pdf;
} BRDFSample;

/* Evaluate Cook-Torrance specular BRDF */
vec3 brdf_cook_torrance(vec3 N, vec3 V, vec3 L, vec3 base_color,
                        float metallic, float roughness);

/* Evaluate full PBR BRDF (diffuse + specular) */
BRDFSample brdf_evaluate(vec3 N, vec3 V, vec3 L, const RTMaterial* mat);

/* Sample BRDF direction (importance sampling) */
vec3 brdf_sample_direction(vec3 N, vec3 V, const RTMaterial* mat,
                           vec2 random, float* out_pdf);

/* Fresnel-Schlick */
vec3 fresnel_schlick(float cos_theta, vec3 F0);

/* GGX Normal Distribution */
float distribution_ggx(vec3 N, vec3 H, float roughness);

/* Smith's Geometry function */
float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Texture System                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t*    pixels;
    uint32_t    width;
    uint32_t    height;
    uint32_t    channels;
    uint32_t    mip_levels;
    uint8_t**   mip_data;       /* array of mip level pointers */
} Texture;

typedef struct {
    Texture*    textures;
    uint32_t    count;
    uint32_t    capacity;
} TextureSystem;

TextureSystem   texture_system_create(uint32_t capacity);
void            texture_system_destroy(TextureSystem* sys);
uint32_t        texture_load(TextureSystem* sys, const char* path);
uint32_t        texture_load_from_memory(TextureSystem* sys, const uint8_t* data,
                                         uint32_t width, uint32_t height, uint32_t channels);

/* Sampling */
vec4 texture_sample(const Texture* tex, float u, float v);
vec4 texture_sample_mip(const Texture* tex, float u, float v, float mip_level);

/* Generate mipmaps for a texture */
void texture_generate_mipmaps(Texture* tex);

#endif /* PHOTON_MATERIAL_H */
