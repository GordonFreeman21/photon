/*
 * Photon - PBR Material System
 * Cook-Torrance BRDF, texture loading/sampling, material management
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "../include/material.h"
#include "../include/math_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Material Management                                                       */
/* ═══════════════════════════════════════════════════════════════════════════ */

MaterialSystem material_system_create(uint32_t capacity)
{
    MaterialSystem sys;
    sys.materials = (RTMaterial*)malloc(capacity * sizeof(RTMaterial));
    sys.count     = 0;
    sys.capacity  = capacity;
    return sys;
}

void material_system_destroy(MaterialSystem* sys)
{
    if (sys) {
        free(sys->materials);
        sys->materials = NULL;
        sys->count     = 0;
        sys->capacity  = 0;
    }
}

uint32_t material_add(MaterialSystem* sys, const RTMaterial* mat)
{
    assert(sys->count < sys->capacity);
    uint32_t id = sys->count;
    sys->materials[id] = *mat;
    sys->count++;
    return id;
}

RTMaterial* material_get(MaterialSystem* sys, uint32_t id)
{
    assert(id < sys->count);
    return &sys->materials[id];
}

RTMaterial material_default(void)
{
    RTMaterial mat;
    memset(&mat, 0, sizeof(mat));
    mat.base_color         = (vec4){1.0f, 1.0f, 1.0f, 1.0f};
    mat.metallic           = 0.0f;
    mat.roughness          = 0.5f;
    mat.ior                = 1.5f;
    mat.emissive_strength  = 0.0f;
    mat.emissive_color     = (vec3){0.0f, 0.0f, 0.0f};
    mat.albedo_tex              = PHOTON_INVALID_ID;
    mat.normal_tex              = PHOTON_INVALID_ID;
    mat.metallic_roughness_tex  = PHOTON_INVALID_ID;
    mat.emissive_tex            = PHOTON_INVALID_ID;
    mat.flags              = 0;
    return mat;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  BRDF Helpers                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

vec3 fresnel_schlick(float cos_theta, vec3 F0)
{
    float t = 1.0f - cos_theta;
    float t2 = t * t;
    float t5 = t2 * t2 * t;
    return vec3_add(F0, vec3_scale(vec3_sub(vec3_new(1.0f, 1.0f, 1.0f), F0), t5));
}

float distribution_ggx(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = photon_maxf(vec3_dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
    denom = PHOTON_PI * denom * denom;

    return a2 / photon_maxf(denom, PHOTON_EPSILON);
}

static float geometry_schlick_ggx(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = photon_maxf(vec3_dot(N, V), 0.0f);
    float NdotL = photon_maxf(vec3_dot(N, L), 0.0f);
    return geometry_schlick_ggx(NdotV, roughness) *
           geometry_schlick_ggx(NdotL, roughness);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Cook-Torrance BRDF                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

vec3 brdf_cook_torrance(vec3 N, vec3 V, vec3 L, vec3 base_color,
                        float metallic, float roughness)
{
    vec3 H = vec3_normalize(vec3_add(V, L));

    /* F0: dielectric base of 0.04, lerped to base_color for metals */
    vec3 F0 = vec3_lerp(vec3_new(0.04f, 0.04f, 0.04f), base_color, metallic);

    vec3  F = fresnel_schlick(photon_maxf(vec3_dot(H, V), 0.0f), F0);
    float D = distribution_ggx(N, H, roughness);
    float G = geometry_smith(N, V, L, roughness);

    float NdotV = photon_maxf(vec3_dot(N, V), 0.0f);
    float NdotL = photon_maxf(vec3_dot(N, L), 0.0f);

    /* Specular: (D * G * F) / (4 * NdotV * NdotL + epsilon) */
    float spec_denom = 4.0f * NdotV * NdotL + PHOTON_EPSILON;
    vec3  specular   = vec3_scale(F, (D * G) / spec_denom);

    /* Diffuse: kD * base_color / PI */
    vec3 kD = vec3_scale(vec3_sub(vec3_new(1.0f, 1.0f, 1.0f), F), 1.0f - metallic);
    vec3 diffuse = vec3_scale(vec3_mul(kD, base_color), 1.0f / PHOTON_PI);

    return vec3_add(diffuse, specular);
}

BRDFSample brdf_evaluate(vec3 N, vec3 V, vec3 L, const RTMaterial* mat)
{
    vec3 base = vec3_new(mat->base_color.x, mat->base_color.y, mat->base_color.z);
    vec3 result = brdf_cook_torrance(N, V, L, base, mat->metallic, mat->roughness);

    float NdotL = photon_maxf(vec3_dot(N, L), 0.0f);

    /* Approximate split for the sample struct */
    vec3 F0 = vec3_lerp(vec3_new(0.04f, 0.04f, 0.04f), base, mat->metallic);
    vec3 H  = vec3_normalize(vec3_add(V, L));
    vec3 F  = fresnel_schlick(photon_maxf(vec3_dot(H, V), 0.0f), F0);
    vec3 kD = vec3_scale(vec3_sub(vec3_new(1.0f, 1.0f, 1.0f), F), 1.0f - mat->metallic);

    BRDFSample sample;
    sample.diffuse  = vec3_scale(vec3_mul(kD, base), 1.0f / PHOTON_PI);
    sample.specular = vec3_sub(result, sample.diffuse);
    sample.pdf      = NdotL / PHOTON_PI;
    return sample;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GGX Importance Sampling                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

vec3 brdf_sample_direction(vec3 N, vec3 V, const RTMaterial* mat,
                           vec2 random, float* out_pdf)
{
    float a = mat->roughness * mat->roughness;

    /* Sample GGX microfacet normal in tangent space */
    float phi      = 2.0f * PHOTON_PI * random.x;
    float cos_theta = sqrtf((1.0f - random.y) /
                            (1.0f + (a * a - 1.0f) * random.y));
    float sin_theta = sqrtf(1.0f - cos_theta * cos_theta);

    /* Tangent-space half vector */
    vec3 Ht = vec3_new(sin_theta * cosf(phi),
                       sin_theta * sinf(phi),
                       cos_theta);

    /* Build tangent-to-world basis from N */
    vec3 up = (fabsf(N.z) < 0.999f)
                  ? vec3_new(0.0f, 0.0f, 1.0f)
                  : vec3_new(1.0f, 0.0f, 0.0f);
    vec3 T = vec3_normalize(vec3_cross(up, N));
    vec3 B = vec3_cross(N, T);

    /* World-space half vector */
    vec3 H = vec3_normalize(
        vec3_add(vec3_add(vec3_scale(T, Ht.x), vec3_scale(B, Ht.y)),
                 vec3_scale(N, Ht.z)));

    /* Reflect V around H to get sample direction L */
    vec3 L = vec3_sub(vec3_scale(H, 2.0f * vec3_dot(V, H)), V);

    /* PDF = D(H) * NdotH / (4 * VdotH) */
    float NdotH = photon_maxf(vec3_dot(N, H), 0.0f);
    float VdotH = photon_maxf(vec3_dot(V, H), 0.0f);
    float D     = distribution_ggx(N, H, mat->roughness);

    *out_pdf = (D * NdotH) / (4.0f * VdotH + PHOTON_EPSILON);

    return L;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Texture System                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

TextureSystem texture_system_create(uint32_t capacity)
{
    TextureSystem sys;
    sys.textures = (Texture*)malloc(capacity * sizeof(Texture));
    sys.count    = 0;
    sys.capacity = capacity;
    memset(sys.textures, 0, capacity * sizeof(Texture));
    return sys;
}

void texture_system_destroy(TextureSystem* sys)
{
    if (!sys) return;
    for (uint32_t i = 0; i < sys->count; i++) {
        Texture* tex = &sys->textures[i];
        /* Free mip chain (mip 0 == pixels, skip it) */
        if (tex->mip_data) {
            for (uint32_t m = 1; m < tex->mip_levels; m++) {
                free(tex->mip_data[m]);
            }
            free(tex->mip_data);
        }
        /* Free base pixel data (loaded by stbi or allocated manually) */
        if (tex->pixels) {
            stbi_image_free(tex->pixels);
        }
    }
    free(sys->textures);
    sys->textures = NULL;
    sys->count    = 0;
    sys->capacity = 0;
}

uint32_t texture_load(TextureSystem* sys, const char* path)
{
    assert(sys->count < sys->capacity);

    int w, h, ch;
    uint8_t* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) return PHOTON_INVALID_ID;

    uint32_t id = sys->count;
    Texture* tex = &sys->textures[id];
    tex->pixels    = pixels;
    tex->width     = (uint32_t)w;
    tex->height    = (uint32_t)h;
    tex->channels  = 4;
    tex->mip_levels = 0;
    tex->mip_data   = NULL;

    texture_generate_mipmaps(tex);
    sys->count++;
    return id;
}

uint32_t texture_load_from_memory(TextureSystem* sys, const uint8_t* data,
                                  uint32_t width, uint32_t height, uint32_t channels)
{
    assert(sys->count < sys->capacity);

    uint32_t size = width * height * channels;
    uint8_t* copy = (uint8_t*)malloc(size);
    memcpy(copy, data, size);

    uint32_t id = sys->count;
    Texture* tex = &sys->textures[id];
    tex->pixels    = copy;
    tex->width     = width;
    tex->height    = height;
    tex->channels  = channels;
    tex->mip_levels = 0;
    tex->mip_data   = NULL;

    texture_generate_mipmaps(tex);
    sys->count++;
    return id;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Mipmap Generation (box filter)                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

void texture_generate_mipmaps(Texture* tex)
{
    if (!tex || !tex->pixels) return;

    /* Count mip levels */
    uint32_t w = tex->width;
    uint32_t h = tex->height;
    uint32_t levels = 1;
    while (w > 1 || h > 1) {
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
        levels++;
    }

    /* Free old mip chain if present */
    if (tex->mip_data) {
        for (uint32_t m = 1; m < tex->mip_levels; m++) {
            free(tex->mip_data[m]);
        }
        free(tex->mip_data);
    }

    tex->mip_levels = levels;
    tex->mip_data   = (uint8_t**)malloc(levels * sizeof(uint8_t*));
    tex->mip_data[0] = tex->pixels;  /* level 0 is the base image */

    uint32_t ch = tex->channels;

    uint32_t prev_w = tex->width;
    uint32_t prev_h = tex->height;

    for (uint32_t m = 1; m < levels; m++) {
        uint32_t mip_w = (prev_w > 1) ? prev_w / 2 : 1;
        uint32_t mip_h = (prev_h > 1) ? prev_h / 2 : 1;

        uint8_t* prev   = tex->mip_data[m - 1];
        uint8_t* mip    = (uint8_t*)malloc(mip_w * mip_h * ch);

        for (uint32_t y = 0; y < mip_h; y++) {
            for (uint32_t x = 0; x < mip_w; x++) {
                uint32_t sx = x * 2;
                uint32_t sy = y * 2;
                /* Clamp source coordinates to previous mip bounds */
                uint32_t sx1 = (sx + 1 < prev_w) ? sx + 1 : sx;
                uint32_t sy1 = (sy + 1 < prev_h) ? sy + 1 : sy;

                for (uint32_t c = 0; c < ch; c++) {
                    uint32_t sum = 0;
                    sum += prev[(sy  * prev_w + sx)  * ch + c];
                    sum += prev[(sy  * prev_w + sx1) * ch + c];
                    sum += prev[(sy1 * prev_w + sx)  * ch + c];
                    sum += prev[(sy1 * prev_w + sx1) * ch + c];
                    mip[(y * mip_w + x) * ch + c] = (uint8_t)(sum / 4);
                }
            }
        }

        tex->mip_data[m] = mip;
        prev_w = mip_w;
        prev_h = mip_h;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Texture Sampling                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

static vec4 sample_bilinear(const uint8_t* data, uint32_t w, uint32_t h,
                            uint32_t ch, float u, float v)
{
    /* Wrap UVs to [0, 1) */
    u = fmodf(u, 1.0f);
    v = fmodf(v, 1.0f);
    if (u < 0.0f) u += 1.0f;
    if (v < 0.0f) v += 1.0f;

    float fx = u * (float)w - 0.5f;
    float fy = v * (float)h - 0.5f;

    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    float dx = fx - (float)x0;
    float dy = fy - (float)y0;

    /* Wrap integer coords */
    int x1 = (x0 + 1) % (int)w;
    int y1 = (y0 + 1) % (int)h;
    x0 = ((x0 % (int)w) + (int)w) % (int)w;
    y0 = ((y0 % (int)h) + (int)h) % (int)h;

    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    /* Fetch four texels */
    const uint8_t* p00 = &data[(y0 * (int)w + x0) * (int)ch];
    const uint8_t* p10 = &data[(y0 * (int)w + x1) * (int)ch];
    const uint8_t* p01 = &data[(y1 * (int)w + x0) * (int)ch];
    const uint8_t* p11 = &data[(y1 * (int)w + x1) * (int)ch];

    float inv = 1.0f / 255.0f;

    r = photon_lerpf(photon_lerpf(p00[0] * inv, p10[0] * inv, dx),
                     photon_lerpf(p01[0] * inv, p11[0] * inv, dx), dy);
    if (ch >= 2)
        g = photon_lerpf(photon_lerpf(p00[1] * inv, p10[1] * inv, dx),
                         photon_lerpf(p01[1] * inv, p11[1] * inv, dx), dy);
    if (ch >= 3)
        b = photon_lerpf(photon_lerpf(p00[2] * inv, p10[2] * inv, dx),
                         photon_lerpf(p01[2] * inv, p11[2] * inv, dx), dy);
    if (ch >= 4)
        a = photon_lerpf(photon_lerpf(p00[3] * inv, p10[3] * inv, dx),
                         photon_lerpf(p01[3] * inv, p11[3] * inv, dx), dy);

    return (vec4){r, g, b, a};
}

vec4 texture_sample(const Texture* tex, float u, float v)
{
    if (!tex || !tex->pixels)
        return (vec4){1.0f, 0.0f, 1.0f, 1.0f};  /* magenta = missing */

    return sample_bilinear(tex->pixels, tex->width, tex->height,
                           tex->channels, u, v);
}

vec4 texture_sample_mip(const Texture* tex, float u, float v, float mip_level)
{
    if (!tex || !tex->pixels)
        return (vec4){1.0f, 0.0f, 1.0f, 1.0f};

    if (!tex->mip_data || tex->mip_levels <= 1)
        return texture_sample(tex, u, v);

    /* Clamp mip level */
    float max_mip = (float)(tex->mip_levels - 1);
    mip_level = photon_clampf(mip_level, 0.0f, max_mip);

    uint32_t mip0 = (uint32_t)floorf(mip_level);
    uint32_t mip1 = mip0 + 1;
    if (mip1 >= tex->mip_levels) mip1 = tex->mip_levels - 1;
    float frac = mip_level - (float)mip0;

    /* Compute dimensions for each mip level */
    uint32_t w0 = tex->width  >> mip0; if (w0 < 1) w0 = 1;
    uint32_t h0 = tex->height >> mip0; if (h0 < 1) h0 = 1;
    uint32_t w1 = tex->width  >> mip1; if (w1 < 1) w1 = 1;
    uint32_t h1 = tex->height >> mip1; if (h1 < 1) h1 = 1;

    vec4 s0 = sample_bilinear(tex->mip_data[mip0], w0, h0, tex->channels, u, v);
    vec4 s1 = sample_bilinear(tex->mip_data[mip1], w1, h1, tex->channels, u, v);

    /* Trilinear blend between two mip levels */
    return (vec4){
        photon_lerpf(s0.x, s1.x, frac),
        photon_lerpf(s0.y, s1.y, frac),
        photon_lerpf(s0.z, s1.z, frac),
        photon_lerpf(s0.w, s1.w, frac)
    };
}
