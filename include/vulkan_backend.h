/*
 * Photon - Vulkan Backend
 */
#ifndef PHOTON_VULKAN_BACKEND_H
#define PHOTON_VULKAN_BACKEND_H

#include "photon_types.h"

/* Forward declarations - actual Vulkan types are in implementation */
typedef struct VulkanContext VulkanContext;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Initialization                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    width;
    uint32_t    height;
    const char* app_name;
    bool        enable_validation;
    void*       window_handle;      /* GLFW window pointer */
} VulkanInitDesc;

VulkanContext*  vk_create(const VulkanInitDesc* desc);
void            vk_destroy(VulkanContext* ctx);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Swapchain                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool    vk_recreate_swapchain(VulkanContext* ctx, uint32_t width, uint32_t height);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  G-Buffer                                                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    albedo;         /* RGB8 albedo */
    uint32_t    normal_rm;      /* RG16F normal (octahedral), BA8 roughness+metallic */
    uint32_t    emissive;       /* R11G11B10F emissive + flags */
    uint32_t    motion_depth;   /* RG16F motion vectors, B16F depth derivative, A mesh ID */
    uint32_t    depth;          /* D32F depth */
    uint32_t    width, height;
} GBuffer;

GBuffer vk_create_gbuffer(VulkanContext* ctx, uint32_t width, uint32_t height);
void    vk_destroy_gbuffer(VulkanContext* ctx, GBuffer* gbuf);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Buffer Management                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    handle;
    void*       mapped;         /* persistent mapped pointer (or NULL) */
    size_t      size;
} GPUBuffer;

GPUBuffer   vk_create_buffer(VulkanContext* ctx, size_t size, uint32_t usage_flags);
void        vk_destroy_buffer(VulkanContext* ctx, GPUBuffer* buf);
void        vk_upload_buffer(VulkanContext* ctx, GPUBuffer* buf, const void* data, size_t size);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Compute Pipeline                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    pipeline;
    uint32_t    layout;
    uint32_t    descriptor_set;
} ComputePipeline;

ComputePipeline vk_create_compute_pipeline(VulkanContext* ctx, const char* shader_path);
void            vk_destroy_compute_pipeline(VulkanContext* ctx, ComputePipeline* pipe);
void            vk_dispatch_compute(VulkanContext* ctx, ComputePipeline* pipe,
                                    uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Graphics Pipeline (G-buffer pass)                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    pipeline;
    uint32_t    layout;
    uint32_t    render_pass;
    uint32_t    framebuffer;
} GraphicsPipeline;

GraphicsPipeline vk_create_gbuffer_pipeline(VulkanContext* ctx, const GBuffer* gbuf);
void             vk_destroy_graphics_pipeline(VulkanContext* ctx, GraphicsPipeline* pipe);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Frame Rendering                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t    command_buffer;
    uint32_t    image_available_semaphore;
    uint32_t    render_finished_semaphore;
    uint32_t    in_flight_fence;
    uint32_t    frame_index;
} FrameResources;

bool    vk_begin_frame(VulkanContext* ctx, FrameResources* frame);
void    vk_end_frame(VulkanContext* ctx, FrameResources* frame);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Timestamp Queries (profiling)                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

void    vk_timestamp_begin(VulkanContext* ctx, const char* label);
void    vk_timestamp_end(VulkanContext* ctx, const char* label);
float   vk_timestamp_get_ms(VulkanContext* ctx, const char* label);

#endif /* PHOTON_VULKAN_BACKEND_H */
