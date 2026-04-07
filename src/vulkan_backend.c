/*
 * Photon - Vulkan Backend Implementation (Stub)
 *
 * This file implements the Vulkan backend with placeholder/stub logic so it
 * compiles without the Vulkan SDK.  Real Vulkan API calls are noted in
 * comments; handles are represented as uint64_t counters.
 *
 * C17 standard.
 */

#include "../include/vulkan_backend.h"
#include "../include/math_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Internal context                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Monotonic counter used to hand out unique dummy handles. */
static uint64_t g_next_handle = 1;

static uint64_t alloc_handle(void)
{
    return g_next_handle++;
}

struct VulkanContext {
    /* In production these would be real Vulkan handles */
    uint64_t instance;
    uint64_t physical_device;
    uint64_t device;
    uint64_t graphics_queue;
    uint64_t compute_queue;
    uint64_t surface;
    uint64_t swapchain;
    uint64_t command_pool;
    uint64_t descriptor_pool;

    uint32_t width, height;
    uint32_t swapchain_image_count;
    uint32_t current_frame;

    /* Timestamp query pool */
    uint64_t timestamp_query_pool;
    float    timestamp_period_ns;

    bool     validation_enabled;
    void*    window;

    /* Frame resources */
    FrameResources frames[PHOTON_FRAME_OVERLAP];
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Initialization                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

VulkanContext* vk_create(const VulkanInitDesc* desc)
{
    if (!desc) {
        printf("[Photon/Vulkan] ERROR: VulkanInitDesc is NULL\n");
        return NULL;
    }

    VulkanContext* ctx = (VulkanContext*)malloc(sizeof(VulkanContext));
    if (!ctx) {
        printf("[Photon/Vulkan] ERROR: Failed to allocate VulkanContext\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(VulkanContext));

    ctx->width  = desc->width;
    ctx->height = desc->height;
    ctx->validation_enabled = desc->enable_validation;
    ctx->window = desc->window_handle;

    printf("[Photon/Vulkan] Creating Vulkan backend (%ux%u, validation=%s)\n",
           ctx->width, ctx->height,
           ctx->validation_enabled ? "ON" : "OFF");

    /* -- VkInstance -------------------------------------------------------- */
    /* vkCreateInstance(&createInfo, NULL, &ctx->instance); */
    ctx->instance = alloc_handle();
    printf("[Photon/Vulkan]   Instance          = 0x%llx\n",
           (unsigned long long)ctx->instance);

    /* -- Physical device -------------------------------------------------- */
    /* vkEnumeratePhysicalDevices / pick best discrete GPU */
    ctx->physical_device = alloc_handle();
    printf("[Photon/Vulkan]   Physical device   = 0x%llx\n",
           (unsigned long long)ctx->physical_device);

    /* -- Logical device + queues ------------------------------------------ */
    /* vkCreateDevice with graphics + compute queue families */
    ctx->device         = alloc_handle();
    ctx->graphics_queue = alloc_handle();
    ctx->compute_queue  = alloc_handle();
    printf("[Photon/Vulkan]   Logical device    = 0x%llx\n",
           (unsigned long long)ctx->device);
    printf("[Photon/Vulkan]   Graphics queue    = 0x%llx\n",
           (unsigned long long)ctx->graphics_queue);
    printf("[Photon/Vulkan]   Compute queue     = 0x%llx\n",
           (unsigned long long)ctx->compute_queue);

    /* -- Surface + Swapchain ---------------------------------------------- */
    /* glfwCreateWindowSurface / vkCreateSwapchainKHR */
    ctx->surface   = alloc_handle();
    ctx->swapchain = alloc_handle();
    ctx->swapchain_image_count = 3; /* triple-buffered */
    printf("[Photon/Vulkan]   Surface           = 0x%llx\n",
           (unsigned long long)ctx->surface);
    printf("[Photon/Vulkan]   Swapchain         = 0x%llx  (%u images)\n",
           (unsigned long long)ctx->swapchain, ctx->swapchain_image_count);

    /* -- Command pool ----------------------------------------------------- */
    /* vkCreateCommandPool */
    ctx->command_pool = alloc_handle();
    printf("[Photon/Vulkan]   Command pool      = 0x%llx\n",
           (unsigned long long)ctx->command_pool);

    /* -- Descriptor pool -------------------------------------------------- */
    /* vkCreateDescriptorPool */
    ctx->descriptor_pool = alloc_handle();
    printf("[Photon/Vulkan]   Descriptor pool   = 0x%llx\n",
           (unsigned long long)ctx->descriptor_pool);

    /* -- Timestamp query pool --------------------------------------------- */
    /* vkCreateQueryPool(VK_QUERY_TYPE_TIMESTAMP, ...) */
    ctx->timestamp_query_pool = alloc_handle();
    ctx->timestamp_period_ns  = 1.0f; /* would come from device limits */
    printf("[Photon/Vulkan]   Timestamp pool    = 0x%llx\n",
           (unsigned long long)ctx->timestamp_query_pool);

    /* -- Per-frame resources ---------------------------------------------- */
    ctx->current_frame = 0;
    for (uint32_t i = 0; i < PHOTON_FRAME_OVERLAP; i++) {
        /*
         * vkAllocateCommandBuffers
         * vkCreateSemaphore  (image_available)
         * vkCreateSemaphore  (render_finished)
         * vkCreateFence      (in_flight)
         */
        ctx->frames[i].command_buffer            = (uint32_t)alloc_handle();
        ctx->frames[i].image_available_semaphore = (uint32_t)alloc_handle();
        ctx->frames[i].render_finished_semaphore = (uint32_t)alloc_handle();
        ctx->frames[i].in_flight_fence           = (uint32_t)alloc_handle();
        ctx->frames[i].frame_index               = i;
    }
    printf("[Photon/Vulkan]   Frame resources   = %u frames allocated\n",
           PHOTON_FRAME_OVERLAP);

    printf("[Photon/Vulkan] Vulkan backend ready.\n");
    return ctx;
}

void vk_destroy(VulkanContext* ctx)
{
    if (!ctx) return;

    printf("[Photon/Vulkan] Destroying Vulkan backend ...\n");

    /*
     * In production:
     *   vkDeviceWaitIdle(ctx->device);
     *   destroy per-frame semaphores / fences / command buffers
     *   vkDestroyQueryPool, vkDestroyDescriptorPool, vkDestroyCommandPool
     *   vkDestroySwapchainKHR, vkDestroySurfaceKHR
     *   vkDestroyDevice, vkDestroyInstance
     */

    free(ctx);
    printf("[Photon/Vulkan] Backend destroyed.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Swapchain                                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool vk_recreate_swapchain(VulkanContext* ctx, uint32_t width, uint32_t height)
{
    if (!ctx) return false;

    printf("[Photon/Vulkan] Recreating swapchain (%u x %u)\n", width, height);

    /*
     * vkDeviceWaitIdle(ctx->device);
     * vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
     * create new swapchain with updated extent
     */

    ctx->width  = width;
    ctx->height = height;
    ctx->swapchain = alloc_handle();

    printf("[Photon/Vulkan]   New swapchain     = 0x%llx\n",
           (unsigned long long)ctx->swapchain);
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  G-Buffer                                                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

GBuffer vk_create_gbuffer(VulkanContext* ctx, uint32_t width, uint32_t height)
{
    GBuffer gbuf;
    memset(&gbuf, 0, sizeof(gbuf));

    if (!ctx) return gbuf;

    printf("[Photon/Vulkan] Creating G-Buffer (%u x %u)\n", width, height);

    gbuf.width  = width;
    gbuf.height = height;

    /*
     * Each target would be created with vkCreateImage / vkAllocateMemory /
     * vkCreateImageView using the formats noted in the header.
     */
    gbuf.albedo       = (uint32_t)alloc_handle(); /* VK_FORMAT_R8G8B8A8_UNORM   */
    gbuf.normal_rm    = (uint32_t)alloc_handle(); /* VK_FORMAT_R16G16B16A16_SFLOAT */
    gbuf.emissive     = (uint32_t)alloc_handle(); /* VK_FORMAT_B10G11R11_UFLOAT_PACK32 */
    gbuf.motion_depth = (uint32_t)alloc_handle(); /* VK_FORMAT_R16G16B16A16_SFLOAT */
    gbuf.depth        = (uint32_t)alloc_handle(); /* VK_FORMAT_D32_SFLOAT        */

    printf("[Photon/Vulkan]   albedo       = %u\n", gbuf.albedo);
    printf("[Photon/Vulkan]   normal_rm    = %u\n", gbuf.normal_rm);
    printf("[Photon/Vulkan]   emissive     = %u\n", gbuf.emissive);
    printf("[Photon/Vulkan]   motion_depth = %u\n", gbuf.motion_depth);
    printf("[Photon/Vulkan]   depth        = %u\n", gbuf.depth);

    return gbuf;
}

void vk_destroy_gbuffer(VulkanContext* ctx, GBuffer* gbuf)
{
    if (!ctx || !gbuf) return;

    printf("[Photon/Vulkan] Destroying G-Buffer\n");

    /*
     * vkDestroyImageView / vkDestroyImage / vkFreeMemory for each target
     */
    memset(gbuf, 0, sizeof(*gbuf));
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Buffer Management                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

GPUBuffer vk_create_buffer(VulkanContext* ctx, size_t size, uint32_t usage_flags)
{
    GPUBuffer buf;
    memset(&buf, 0, sizeof(buf));

    if (!ctx) return buf;

    /*
     * VkBufferCreateInfo info = { .size = size, .usage = usage_flags };
     * vkCreateBuffer(ctx->device, &info, NULL, &buf.handle);
     * vkAllocateMemory / vkBindBufferMemory
     * if HOST_VISIBLE: vkMapMemory -> buf.mapped
     */

    buf.handle = (uint32_t)alloc_handle();
    buf.size   = size;
    buf.mapped = NULL;

    printf("[Photon/Vulkan] Created buffer handle=%u  size=%zu  usage=0x%x\n",
           buf.handle, size, usage_flags);
    return buf;
}

void vk_destroy_buffer(VulkanContext* ctx, GPUBuffer* buf)
{
    if (!ctx || !buf) return;

    printf("[Photon/Vulkan] Destroying buffer handle=%u\n", buf->handle);

    /*
     * if (buf->mapped) vkUnmapMemory(ctx->device, memory);
     * vkDestroyBuffer(ctx->device, buf->handle, NULL);
     * vkFreeMemory(ctx->device, memory, NULL);
     */

    buf->handle = 0;
    buf->mapped = NULL;
    buf->size   = 0;
}

void vk_upload_buffer(VulkanContext* ctx, GPUBuffer* buf,
                      const void* data, size_t size)
{
    if (!ctx || !buf || !data) return;

    /* Stub: no actual upload in headless mode */

    /*
     * If buf->mapped:
     *     memcpy(buf->mapped, data, size);
     *     vkFlushMappedMemoryRanges (if not HOST_COHERENT)
     * Else:
     *     create staging buffer, memcpy, submit copy command, wait, free staging
     */
    (void)data;
    (void)size;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Compute Pipeline                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

ComputePipeline vk_create_compute_pipeline(VulkanContext* ctx,
                                           const char* shader_path)
{
    ComputePipeline pipe;
    memset(&pipe, 0, sizeof(pipe));

    if (!ctx || !shader_path) return pipe;

    printf("[Photon/Vulkan] Creating compute pipeline: %s\n", shader_path);

    /*
     * 1. Load SPIR-V from shader_path -> vkCreateShaderModule
     * 2. vkCreateDescriptorSetLayout
     * 3. vkCreatePipelineLayout
     * 4. VkComputePipelineCreateInfo -> vkCreateComputePipelines
     * 5. vkAllocateDescriptorSets
     */

    pipe.pipeline       = (uint32_t)alloc_handle();
    pipe.layout         = (uint32_t)alloc_handle();
    pipe.descriptor_set = (uint32_t)alloc_handle();

    printf("[Photon/Vulkan]   pipeline       = %u\n", pipe.pipeline);
    printf("[Photon/Vulkan]   layout         = %u\n", pipe.layout);
    printf("[Photon/Vulkan]   descriptor_set = %u\n", pipe.descriptor_set);
    return pipe;
}

void vk_destroy_compute_pipeline(VulkanContext* ctx, ComputePipeline* pipe)
{
    if (!ctx || !pipe) return;

    printf("[Photon/Vulkan] Destroying compute pipeline=%u\n", pipe->pipeline);

    /*
     * vkDestroyPipeline(ctx->device, pipe->pipeline, NULL);
     * vkDestroyPipelineLayout(ctx->device, pipe->layout, NULL);
     * descriptor sets freed when pool is destroyed
     */

    pipe->pipeline       = 0;
    pipe->layout         = 0;
    pipe->descriptor_set = 0;
}

void vk_dispatch_compute(VulkanContext* ctx, ComputePipeline* pipe,
                         uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
    if (!ctx || !pipe) return;

    /* Stub: no actual dispatch in headless mode */

    /*
     * vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipeline);
     * vkCmdBindDescriptorSets(cmd, ..., pipe->descriptor_set);
     * vkCmdDispatch(cmd, groups_x, groups_y, groups_z);
     */
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Graphics Pipeline (G-buffer pass)                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

GraphicsPipeline vk_create_gbuffer_pipeline(VulkanContext* ctx,
                                            const GBuffer* gbuf)
{
    GraphicsPipeline pipe;
    memset(&pipe, 0, sizeof(pipe));

    if (!ctx || !gbuf) return pipe;

    printf("[Photon/Vulkan] Creating G-Buffer graphics pipeline (%u x %u)\n",
           gbuf->width, gbuf->height);

    /*
     * 1. Load vertex + fragment SPIR-V -> vkCreateShaderModule
     * 2. Set up vertex input state, rasterization, depth/stencil, color blend
     * 3. vkCreateRenderPass with attachments matching GBuffer formats
     * 4. vkCreateFramebuffer with GBuffer image views
     * 5. vkCreatePipelineLayout
     * 6. vkCreateGraphicsPipelines
     */

    pipe.render_pass = (uint32_t)alloc_handle();
    pipe.framebuffer = (uint32_t)alloc_handle();
    pipe.layout      = (uint32_t)alloc_handle();
    pipe.pipeline    = (uint32_t)alloc_handle();

    printf("[Photon/Vulkan]   render_pass  = %u\n", pipe.render_pass);
    printf("[Photon/Vulkan]   framebuffer  = %u\n", pipe.framebuffer);
    printf("[Photon/Vulkan]   layout       = %u\n", pipe.layout);
    printf("[Photon/Vulkan]   pipeline     = %u\n", pipe.pipeline);
    return pipe;
}

void vk_destroy_graphics_pipeline(VulkanContext* ctx, GraphicsPipeline* pipe)
{
    if (!ctx || !pipe) return;

    printf("[Photon/Vulkan] Destroying graphics pipeline=%u\n", pipe->pipeline);

    /*
     * vkDestroyPipeline(ctx->device, pipe->pipeline, NULL);
     * vkDestroyPipelineLayout(ctx->device, pipe->layout, NULL);
     * vkDestroyFramebuffer(ctx->device, pipe->framebuffer, NULL);
     * vkDestroyRenderPass(ctx->device, pipe->render_pass, NULL);
     */

    pipe->pipeline    = 0;
    pipe->layout      = 0;
    pipe->render_pass = 0;
    pipe->framebuffer = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Frame Rendering                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

bool vk_begin_frame(VulkanContext* ctx, FrameResources* frame)
{
    if (!ctx || !frame) return false;

    ctx->current_frame = (ctx->current_frame + 1) % PHOTON_FRAME_OVERLAP;
    *frame = ctx->frames[ctx->current_frame];

    /*
     * vkWaitForFences(ctx->device, 1, &frame->in_flight_fence, VK_TRUE, UINT64_MAX);
     * vkResetFences(ctx->device, 1, &frame->in_flight_fence);
     * vkAcquireNextImageKHR(ctx->device, ctx->swapchain, UINT64_MAX,
     *                       frame->image_available_semaphore, VK_NULL_HANDLE,
     *                       &image_index);
     * vkResetCommandBuffer(frame->command_buffer, 0);
     * vkBeginCommandBuffer(frame->command_buffer, &begin_info);
     */

    return true;
}

void vk_end_frame(VulkanContext* ctx, FrameResources* frame)
{
    if (!ctx || !frame) return;

    /*
     * vkEndCommandBuffer(frame->command_buffer);
     *
     * VkSubmitInfo submit = {
     *     .waitSemaphoreCount   = 1,
     *     .pWaitSemaphores      = &frame->image_available_semaphore,
     *     .commandBufferCount   = 1,
     *     .pCommandBuffers      = &frame->command_buffer,
     *     .signalSemaphoreCount = 1,
     *     .pSignalSemaphores    = &frame->render_finished_semaphore,
     * };
     * vkQueueSubmit(ctx->graphics_queue, 1, &submit, frame->in_flight_fence);
     *
     * VkPresentInfoKHR present = {
     *     .waitSemaphoreCount = 1,
     *     .pWaitSemaphores    = &frame->render_finished_semaphore,
     *     .swapchainCount     = 1,
     *     .pSwapchains        = &ctx->swapchain,
     *     .pImageIndices      = &image_index,
     * };
     * vkQueuePresentKHR(ctx->graphics_queue, &present);
     */

    (void)frame;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Timestamp Queries (profiling)                                             */
/* ═══════════════════════════════════════════════════════════════════════════ */

void vk_timestamp_begin(VulkanContext* ctx, const char* label)
{
    if (!ctx || !label) return;

    /*
     * vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
     *                     ctx->timestamp_query_pool, query_index);
     */
    (void)label;
}

void vk_timestamp_end(VulkanContext* ctx, const char* label)
{
    if (!ctx || !label) return;

    /*
     * vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
     *                     ctx->timestamp_query_pool, query_index + 1);
     */
    (void)label;
}

float vk_timestamp_get_ms(VulkanContext* ctx, const char* label)
{
    if (!ctx || !label) return 0.0f;

    /*
     * uint64_t timestamps[2];
     * vkGetQueryPoolResults(ctx->device, ctx->timestamp_query_pool,
     *                       query_index, 2, sizeof(timestamps), timestamps,
     *                       sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
     * return (timestamps[1] - timestamps[0]) * ctx->timestamp_period_ns / 1e6f;
     */
    return 0.0f;
}
