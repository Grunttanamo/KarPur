// KarPur temporal anti-aliasing: NVIDIA DLAA on Violet's tone-mapped picture (TOTK_DLAA=1, default off).
//
// The approach is ZA Recomp's (runtime/nvn/vulkan/exec.c fsr2_copy, MIT) on what tests BU-BX found in Violet 4.0.0
// (docs/progress.md 2026-11-08):
// - The camera: the G-buffer draws (4 colour targets with depth) read the view-projection from vertex uniform block c5
//   +0, stored as columns, and the projection alone at +0x80. vk_exec_draw.c hands them over with the depth target.
// - The jitter: every viewport of a pass drawing with the scene depth is shifted by a sub-pixel offset (Halton 2, 3 over
//   8 frames); shadow maps (square) and full-screen passes (no depth) are not. Only once a resolve has run, so a frame
//   never shows jittered without one.
// - The resolve: right after the tone mapping draw (fragment shader 333e928a, TOTK_DLAA_AT=<id> names another) and
//   before the HUD is drawn over the same sRGB image: the image is copied into an RGBA8 one of ours, the motion vectors
//   are made from the depth (shaders/taa_mv.comp: this frame's camera inverted, the last frame's applied), DLSS
//   resolves into a second RGBA8 image, and that is copied back. A camera cut (the view axis turning more than ~18
//   degrees) or a frame without a camera starts the history again.
// - DLSS upscaling (TOTK_DLSS_OUT=window or <w>x<h>, default game): DLSS resolves to the window's size (fitted to 16:9)
//   instead. That picture is scaled down into the game's image, so the game's frame goes on at its own size, and kept
//   with the scaled-down copy (base); at the present, shaders/taa_compose.comp takes the DLSS picture wherever the
//   presented image still equals base and the presented image (scaled up) near the HUD the game drew over it.
// Everything is recorded at the replay's position (vk_exec_inline_begin): DLSS records into the command buffer itself,
// so the recorder thread is drained first and what the replay had bound is forgotten afterwards.
#include "vk_taa.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "nvn_api.h"
#include "taa_compose_spv.h"
#include "taa_mv_spv.h"
#include "vk.h"
#include "vk_dlaa.h"
#include "vk_fsr2.h"
#include "vk_slfg.h"
#include "platform/platform.h"

static struct {
    int mode;                           // 1 NVIDIA DLAA/DLSS, 2 AMD FSR 2
    int init, ok;                        // pipeline made (ok) or tried
    PFN_vkCmdPushDescriptorSetKHR push;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline mv_pipe;
    VkSampler sampler, linear;
    VkDescriptorSetLayout cdsl;          // the composite at the present
    VkPipelineLayout clayout;
    VkPipeline compose_pipe;
    // at w x h: 0 the picture (RGBA8), 1 motion vectors (RG16F), 3 base: the DLSS picture scaled to the game's size;
    // at ow x oh: 2 DLSS's output, 4 the composite, 5 DLSS's picture converted, without the HUD (RGBA8: frame generation)
    VkImage img[6];
    VmaAllocation alloc[6];
    VkImageView view[6];
    SlfgFrame fg;                        // this frame's inputs for DLSS Frame Generation (vk_slfg.cpp), at its resolve
    float proj[16];                      // this frame's projection (c5 +0x80)
    uint32_t w, h, ow, oh;               // their sizes
    int fresh;                           // the images were just made (UNDEFINED layout)
    VkImageView shown_view[8];           // the presented image's view per present slot (destroyed when the slot comes again)
    VkBuffer stats[8];                   // the composite's measurements per present slot (host-visible)
    VmaAllocation stats_alloc[8];
    uint32_t *stats_map[8];
    uint64_t sum[5], composes;           // summed over the composites since the last log line
    VkImageView hud_view;                // the game's image (the tone mapping draw's target) as raw RGBA8 bytes
    int present_up;                      // the present thread's next present composes (vk_taa_present_arm)

    uint32_t at;                         // fragment shader of the tone mapping draw
    int depth_inv;                       // reversed depth (near at 1): from the projection, TOTK_DLAA_DEPTH_INV overrides
    // this frame
    float cur[16];
    int have_cur, resolved;
    const VkTexImage *depth;
    VkImage depth_image;
    VkFormat depth_format;
    uint32_t dw, dh;
    // the last frame
    float prev[16];
    int have_prev;
    // the jitter of this frame and whether it applies (a resolve ran last frame)
    float jx, jy;
    int jitter_on;
    uint32_t sw, sh;                     // the scene size of the last resolve
    uint32_t phases;                     // jitter phases: 8 x (output / render)^2
    int upscaled;                        // this frame's resolve went to a larger output
    volatile int shown_kind;             // the last frame's: 0 no resolve, 1 DLAA, 2 DLSS to a larger picture (the overlay)
    volatile uint32_t shown_w, shown_h, shown_ow, shown_oh;
    uint64_t index, resolves;
    uint64_t last_resolve_ns;
    float sign_x, sign_y, bias;
    int logged;
} T;

int vk_taa_on(void) {
    static int checked, on;
    if (!checked) {
        checked = 1;
        T.mode = rt_knob("FSR2", "0") ? 2 : vk_dlaa_available() ? 1 : 0;
        on = T.mode != 0;
        if (on) {
            const char *e = cfg_get("TOTK_DLAA_AT");
            T.at = e && *e ? (uint32_t)strtoul(e, NULL, 16) : 0x333e928au;
            const char *x = cfg_get("TOTK_DLAA_JX"), *y = cfg_get("TOTK_DLAA_JY"), *b = cfg_get("TOTK_DLAA_JITTER_BIAS");
            T.sign_x = x && *x ? (float)atof(x) : 1.0f;
            T.sign_y = y && *y ? (float)atof(y) : 1.0f;
            T.bias = b && *b ? (float)atof(b) : 0.0f;
            T.depth_inv = -1;
            rt_log("%s: on; resolves after fragment shader %08x", T.mode == 2 ? "fsr2" : "dlaa", T.at);
        } else if (vk_dlaa_wanted()) rt_log("dlaa: TOTK_DLAA=1 but DLSS is not available (see the dlaa: lines above): off");
    }
    return on;
}

int vk_taa_need_scene(void) { return !T.have_cur; }

// Reversed depth from the projection (columns; P[col * 4 + row]): the depth a point 1 unit and 1000 units in front of
// the camera gets. Logged once (numbers only).
static void depth_order(const float p[16]) {
    if (T.depth_inv >= 0) return;
    const char *e = cfg_get("TOTK_DLAA_DEPTH_INV");
    float p22 = p[10], p23 = p[14], p32 = p[11], p33 = p[15];
    if (fabsf(p32) < 1e-6f || fabsf(p33) > 1e-3f) return;   // not a perspective projection
    float n1 = (p22 * (1.0f / p32) + p23), n2 = (p22 * (1000.0f / p32) + p23) / 1000.0f;   // ndc z at w = 1 and w = 1000
    T.depth_inv = e && *e ? atoi(e) != 0 : n1 > n2;
    rt_log("dlaa: clip depth at 1 / 1000 units: %.4f / %.4f, depth mode %s: %s depth%s", n1, n2,
           nvn_depth_minus_w() ? "-w..w" : "0..w", T.depth_inv ? "reversed" : "standard", e && *e ? " (TOTK_DLAA_DEPTH_INV)" : "");
}

void vk_taa_note_scene(const VkExec *ex, const float vp[16], const float proj[16]) {
    if (T.have_cur || !ex->depth.image || !ex->depth.img) return;
    for (int i = 0; i < 16; i++) if (!isfinite(vp[i])) return;
    memcpy(T.cur, vp, sizeof T.cur);
    T.have_cur = 1;
    T.depth = ex->depth.img, T.depth_image = ex->depth.image, T.depth_format = ex->depth.format;
    T.dw = ex->depth.width, T.dh = ex->depth.height;
    memcpy(T.proj, proj, sizeof T.proj);
    depth_order(proj);
}

void vk_taa_viewport(const VkExec *ex, float *x, float *y) {
    if (!T.jitter_on || !ex->depth.image || ex->depth.width != T.sw || ex->depth.height != T.sh || T.sw == T.sh) return;
    *x += T.jx, *y += T.jy;
}

static float halton(uint32_t i, uint32_t b) {
    float f = 1, r = 0;
    for (; i; i /= b) f /= (float)b, r += f * (float)(i % b);
    return r;
}

int vk_taa_frame(void) {
    if (!vk_taa_on()) return 0;
    int up = T.resolved && T.upscaled;
    T.shown_kind = !T.resolved ? 0 : T.ow != T.w || T.oh != T.h ? 2 : 1;
    T.shown_w = T.w, T.shown_h = T.h, T.shown_ow = T.ow, T.shown_oh = T.oh;
    if (T.have_cur) memcpy(T.prev, T.cur, sizeof T.prev);
    T.have_prev = T.have_cur;
    T.jitter_on = T.resolved;
    T.index++;
    uint32_t i = (uint32_t)(T.index % (T.phases ? T.phases : 8)) + 1;
    if (T.mode == 2) vk_fsr2_jitter((uint32_t)T.index, T.sw ? T.sw : 1920, T.ow ? T.ow : 1920, &T.jx, &T.jy);
    else T.jx = halton(i, 2) - 0.5f + T.bias, T.jy = halton(i, 3) - 0.5f + T.bias;
    T.have_cur = T.resolved = T.upscaled = 0;
    return up;
}

// The DLSS output size for a w x h scene: TOTK_DLSS_OUT = game (the scene's size: DLAA), window (the window's
// picture, as the present fits it), or <w>x<h>; at most 3 times the scene per side. Read every frame (window resizes).
static void out_size(uint32_t w, uint32_t h, uint32_t *ow, uint32_t *oh) {
    *ow = w, *oh = h;
    const char *e = cfg_value(T.mode == 2 ? "TOTK_FSR2_OUT" : "TOTK_DLSS_OUT");
    unsigned x = 0, y = 0;
    // Frame generation needs the picture without the HUD at the size the present shows (vk_slfg.cpp): the window's
    if (T.mode == 1 && slfg_generating()) {
        static int said;
        if (strcmp(e, "window") && !said++) rt_log("dlaa: frame generation on: DLSS's output follows the window (TOTK_DLSS_OUT=%s ignored)", e);
        e = "window";
    }
    if (!strcmp(e, "window")) {
        uint32_t sw = 0, sh = 0;
        gfx_present_extent(&sw, &sh);
        if (!sw || !sh) return;
        int to[4];
        gfx_fit_rect((int)w, (int)h, (int)sw, (int)sh, to);
        x = (unsigned)to[2], y = (unsigned)to[3];
    } else if (sscanf(e, "%ux%u", &x, &y) != 2) return;
    if (x <= w || y <= h || x > 3 * w || y > 3 * h) return;
    *ow = x, *oh = y;
}

// ---- resources

static int init(VkCtx *c) {
    T.init = 1;
    T.push = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(c->device, "vkCmdPushDescriptorSetKHR");
    if (!T.push) { rt_log("dlaa: no VK_KHR_push_descriptor: off"); return 0; }
    VkDescriptorSetLayoutBinding b[2] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                           .bindingCount = 2, .pBindings = b};
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 96};
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
                              .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0};
    if (vkCreateDescriptorSetLayout(c->device, &dci, NULL, &T.dsl) != VK_SUCCESS) return 0;
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                     .pSetLayouts = &T.dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr};
    if (vkCreatePipelineLayout(c->device, &li, NULL, &T.layout) != VK_SUCCESS ||
        vkCreateSampler(c->device, &si, NULL, &T.sampler) != VK_SUCCESS)
        return 0;
    VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof taa_mv_spv,
                                   .pCode = taa_mv_spv};
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(c->device, &mi, NULL, &m) != VK_SUCCESS) return 0;
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"},
                                      .layout = T.layout};
    VkResult r = vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, NULL, &T.mv_pipe);
    vkDestroyShaderModule(c->device, m, NULL);
    if (r != VK_SUCCESS) { T.mv_pipe = VK_NULL_HANDLE; return 0; }
    // the composite: shown (linear), base, hi, the output
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    if (vkCreateSampler(c->device, &si, NULL, &T.linear) != VK_SUCCESS) return 0;
    static const VkDescriptorType ct[7] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                           VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    VkDescriptorSetLayoutBinding cb[7];
    for (int i = 0; i < 7; i++)
        cb[i] = (VkDescriptorSetLayoutBinding){.binding = (uint32_t)i, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                               .descriptorType = ct[i]};
    dci.bindingCount = 7, dci.pBindings = cb;
    for (int i = 0; i < 8; i++) {
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 32,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO,
                                      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        VmaAllocationInfo info;
        if (vmaCreateBuffer(c->vma, &bi, &ai, &T.stats[i], &T.stats_alloc[i], &info) != VK_SUCCESS) return 0;
        T.stats_map[i] = info.pMappedData;
        memset(T.stats_map[i], 0, 32);
    }
    if (vkCreateDescriptorSetLayout(c->device, &dci, NULL, &T.cdsl) != VK_SUCCESS) return 0;
    VkPushConstantRange cpr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo cli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                      .pSetLayouts = &T.cdsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &cpr};
    if (vkCreatePipelineLayout(c->device, &cli, NULL, &T.clayout) != VK_SUCCESS) return 0;
    mi.codeSize = sizeof taa_compose_spv, mi.pCode = taa_compose_spv;
    if (vkCreateShaderModule(c->device, &mi, NULL, &m) != VK_SUCCESS) return 0;
    ci.stage.module = m, ci.layout = T.clayout;
    r = vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, NULL, &T.compose_pipe);
    vkDestroyShaderModule(c->device, m, NULL);
    if (r != VK_SUCCESS) { T.compose_pipe = VK_NULL_HANDLE; return 0; }
    return T.ok = 1;
}

static void free_images(VkCtx *c) {
    for (int i = 0; i < 6; i++) {
        if (T.view[i]) vkDestroyImageView(c->device, T.view[i], NULL);
        if (T.img[i]) vmaDestroyImage(c->vma, T.img[i], T.alloc[i]);
        T.view[i] = VK_NULL_HANDLE, T.img[i] = VK_NULL_HANDLE;
    }
    T.w = T.h = T.ow = T.oh = 0;
}

static int ensure_images(VkCtx *c, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh) {
    if (T.img[0] && T.w == w && T.h == h && T.ow == ow && T.oh == oh) return 1;
    if (T.img[0]) {   // a new size: earlier submissions may still use the old images
        vk_queue_lock();
        vkDeviceWaitIdle(c->device);
        vk_queue_unlock();
        free_images(c);
    }
    static const VkFormat fmt[6] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    for (int i = 0; i < 6; i++) {
        int big = i == 2 || i == 4 || i == 5;
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                                .format = fmt[i], .extent = {big ? ow : w, big ? oh : h, 1}, .mipLevels = 1, .arrayLayers = 1,
                                .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (vmaCreateImage(c->vma, &ci, &ai, &T.img[i], &T.alloc[i], NULL) != VK_SUCCESS) {
            T.img[i] = VK_NULL_HANDLE;
            free_images(c);
            return 0;
        }
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = T.img[i],
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt[i],
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        if (vkCreateImageView(c->device, &vi, NULL, &T.view[i]) != VK_SUCCESS) {
            T.view[i] = VK_NULL_HANDLE;
            free_images(c);
            return 0;
        }
    }
    T.w = w, T.h = h, T.ow = ow, T.oh = oh, T.fresh = 1;
    return 1;
}

static void full_barrier(VkCommandBuffer cb) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &d);
}

static void to_general(VkCommandBuffer cb) {   // our new images: UNDEFINED -> GENERAL
    VkImageMemoryBarrier2 b[6];
    for (int i = 0; i < 6; i++)
        b[i] = (VkImageMemoryBarrier2){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                       .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                       .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                       .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = T.img[i],
                                       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 6, .pImageMemoryBarriers = b};
    vkCmdPipelineBarrier2(cb, &d);
}

static void copy_image(VkCommandBuffer cb, VkImage src, VkImage dst, uint32_t w, uint32_t h) {
    VkImageCopy r = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {w, h, 1}};
    vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
}

// Column-major 4x4: o = a x b (o may not alias)
static void mul4(const float *a, const float *b, float *o) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
}

static int inv4(const float *m, float *o) {   // the same layout in and out (after ZA Recomp exec.c inv4)
    float a[16];
    a[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    a[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    a[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    a[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    a[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    a[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    a[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    a[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    a[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    a[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    a[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    a[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    a[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    a[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    a[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    a[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * a[0] + m[1] * a[4] + m[2] * a[8] + m[3] * a[12];
    if (fabsf(det) < 1e-30f) return 0;
    for (int i = 0; i < 16; i++) o[i] = a[i] / det;
    return 1;
}

// A camera cut: the view axis (row 3 of the view-projection: m[3], m[7], m[11] as columns) turned by more than ~18 deg
static int camera_cut(const float *a, const float *b) {
    float la = sqrtf(a[3] * a[3] + a[7] * a[7] + a[11] * a[11]), lb = sqrtf(b[3] * b[3] + b[7] * b[7] + b[11] * b[11]);
    return !(la > 0 && lb > 0) || (a[3] * b[3] + a[7] * b[7] + a[11] * b[11]) / (la * lb) < 0.95f;
}

void vk_taa_after_draw(VkExec *ex, uint32_t fs) {
    if (fs != T.at || T.resolved || !vk_taa_on() || !T.have_cur) return;
    const VkExecTarget *ct = &ex->color[0];
    uint32_t w = ct->width, h = ct->height;
    if (!ct->image || !ct->img || ex->depth.image || w != T.dw || h != T.dh) {
        if (T.logged++ < 4)
            rt_log("%s: the tone mapping draw does not match (target %ux%u, scene depth %ux%u): no resolve",
                   T.mode == 2 ? "fsr2" : "dlaa", w, h, T.dw, T.dh);
        return;
    }
    const VkImageUsageFlags xfer = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((ct->img->usage & xfer) != xfer || !(T.depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
        if (T.logged++ < 4) rt_log("%s: the picture cannot be copied or the depth cannot be sampled: no resolve",
                                   T.mode == 2 ? "fsr2" : "dlaa");
        return;
    }
    VkCtx *c = vk_ctx();
    if (!T.init) init(c);
    uint32_t ow, oh;
    out_size(w, h, &ow, &oh);
    if (!T.ok || !ensure_images(c, w, h, ow, oh)) return;
    VkImageView dv = vk_exec_target_view(T.depth, T.depth_format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1, 0);
    if (!dv) return;
    int reset = !T.have_prev || camera_cut(T.cur, T.prev);
    float inv[16], pc[24];
    if (!inv4(T.cur, inv)) return;
    mul4(reset ? T.cur : T.prev, inv, pc);
    pc[16] = T.jitter_on ? T.jx : 0, pc[17] = T.jitter_on ? T.jy : 0, pc[18] = nvn_depth_minus_w() ? 1.0f : 0.0f, pc[19] = 0;
    pc[20] = 1.0f / (float)w, pc[21] = 1.0f / (float)h, pc[22] = (float)w, pc[23] = (float)h;

    int reopen;
    VkCommandBuffer cb = vk_exec_inline_begin(&reopen);
    if (!cb) return;
    if (T.fresh) to_general(cb), T.fresh = 0;
    full_barrier(cb);
    copy_image(cb, ct->image, T.img[0], w, h);
    VkDescriptorImageInfo di = {T.sampler, dv, VK_IMAGE_LAYOUT_GENERAL}, mi = {VK_NULL_HANDLE, T.view[1], VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &di},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &mi}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, T.mv_pipe);
    rec_push(T.push, cb, VK_PIPELINE_BIND_POINT_COMPUTE, T.layout, 0, 2, wr);
    vkCmdPushConstants(cb, T.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vkCmdDispatch(cb, (w + 7) / 8, (h + 7) / 8, 1);
    full_barrier(cb);
    VkTaaFrame f = {.color = T.img[0], .depth = T.depth_image, .mv = T.img[1], .out = T.img[2],
                    .color_view = T.view[0], .depth_view = dv, .mv_view = T.view[1], .out_view = T.view[2],
                    .color_format = VK_FORMAT_R8G8B8A8_UNORM, .depth_format = T.depth_format,
                    .out_format = VK_FORMAT_R8G8B8A8_UNORM, .w = w, .h = h, .ow = ow, .oh = oh,
                    .jx = (T.jitter_on ? T.jx : 0) * T.sign_x, .jy = (T.jitter_on ? T.jy : 0) * T.sign_y, .dt_ms = 16.7f,
                    .reset = reset, .depth_inv = T.depth_inv > 0};
    rec_drain();   // DLSS records into cb itself: the recorder has made every call queued before
    int ok;
    if (T.mode == 2) {
        uint64_t now = plat_now_ns();
        float dt = T.last_resolve_ns ? (float)((now - T.last_resolve_ns) / 1e6) : 16.7f;
        T.last_resolve_ns = now;
        float near_z = fabsf(T.proj[14] / (fabsf(T.proj[11]) > 1e-5f ? T.proj[11] : 1.0f));
        float scale_y = fabsf(T.proj[5]);
        VkFsr2Frame fr = {.color = f.color, .depth = f.depth, .mv = f.mv, .out = f.out,
                          .color_view = f.color_view, .depth_view = f.depth_view, .mv_view = f.mv_view, .out_view = f.out_view,
                          .color_format = f.color_format, .depth_format = f.depth_format, .out_format = f.out_format,
                          .w = w, .h = h, .ow = ow, .oh = oh, .jx = f.jx, .jy = f.jy,
                          .dt_ms = fminf(fmaxf(dt, 1.0f), 100.0f), .near_z = near_z > 0.001f ? near_z : 0.1f,
                          .fov_y = scale_y > 0.001f ? 2.0f * atanf(1.0f / scale_y) : 1.0f,
                          .reset = reset, .depth_inv = f.depth_inv};
        ok = vk_fsr2_record(c->phys, c->device, cb, &fr);
    } else ok = vk_dlaa_record(c->device, cb, &f);
    full_barrier(cb);
    int up = ow != w || oh != h;
    if (ok && !up) copy_image(cb, T.img[2], ct->image, w, h);
    if (ok && up) {   // scaled down for the game's frame (base), kept for the composite at the present
        VkImageBlit b = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{0, 0, 0}, {(int)ow, (int)oh, 1}},
                         .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstOffsets = {{0, 0, 0}, {(int)w, (int)h, 1}}};
        vkCmdBlitImage(cb, T.img[2], VK_IMAGE_LAYOUT_GENERAL, T.img[3], VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_LINEAR);
        full_barrier(cb);
        copy_image(cb, T.img[3], ct->image, w, h);
    }
    full_barrier(cb);
    vk_exec_draw_new_cb();   // DLSS and the motion vector pass bound pipelines and sets of their own
    VK_HZ_SET(ex, VK_HZ_OP);
    vk_exec_inline_end(reopen);
    if (!ok) return;
    if (up) T.hud_view = vk_exec_target_view(ct->img, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1, 0);
    T.resolved = 1, T.upscaled = up && T.hud_view, T.sw = w, T.sh = h, T.resolves++;
    // DLSS Frame Generation's inputs of this frame (vk_slfg.cpp tags them at the present, in its command buffer)
    if (T.mode == 1) {
        memcpy(T.fg.cur, T.cur, 64), memcpy(T.fg.prev, reset ? T.cur : T.prev, 64), memcpy(T.fg.proj, T.proj, 64);
        T.fg.jx = f.jx, T.fg.jy = f.jy, T.fg.reset = reset, T.fg.depth_inv = T.depth_inv > 0;
        T.fg.depth = T.depth_image, T.fg.depth_view = dv, T.fg.depth_format = T.depth_format, T.fg.depth_usage = T.depth->usage;
        T.fg.mv = T.img[1], T.fg.mv_view = T.view[1], T.fg.w = w, T.fg.h = h;
        T.fg.hudless = T.img[5], T.fg.hudless_view = T.view[5], T.fg.ow = ow, T.fg.oh = oh;
    }
    float ratio = (float)ow / (float)w;
    T.phases = (uint32_t)ceilf(8.0f * ratio * ratio);
    if (T.resolves == 1 || T.resolves % 3600 == 0)
        rt_log("%s: %llu frames resolved (%ux%u -> %ux%u, jitter %s)", T.mode == 2 ? "fsr2" : "dlaa", (unsigned long long)T.resolves, w, h, ow, oh,
               T.jitter_on ? "on" : "not yet");
}

// ---- the composite at the present (present thread; the replay is not recording then: vk_present_drain comes before
// every submission)

void vk_taa_present_arm(int up) { T.present_up = up; }

int vk_taa_describe(char *buf, size_t cap) {
    int k = T.shown_kind;
    if (k == 2) snprintf(buf, cap, "%s %ux%u > %ux%u", T.mode == 2 ? "AMD FSR 2" : "NVIDIA DLSS", T.shown_w, T.shown_h, T.shown_ow, T.shown_oh);
    else if (k == 1) snprintf(buf, cap, "%s", T.mode == 2 ? "AMD FSR 2" : "NVIDIA DLAA");
    return k;
}

int vk_taa_compose(VkCommandBuffer cb, unsigned slot, VkImage shown, VkFormat fmt, int w, int h, VkImage *out, int *ow, int *oh) {
    int up = T.present_up;
    T.present_up = 0;
    if (!up || !T.ok || !T.img[4] || !T.hud_view || (uint32_t)w != T.w || (uint32_t)h != T.h) return 0;
    VkCtx *c = vk_ctx();
    slot %= 8;
    if (T.shown_view[slot]) vkDestroyImageView(c->device, T.shown_view[slot], NULL);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = shown,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(c->device, &vi, NULL, &T.shown_view[slot]) != VK_SUCCESS) {
        T.shown_view[slot] = VK_NULL_HANDLE;
        return 0;
    }
    // the measurements of this slot's last composite (its fence was waited for): summed, logged every 600 composites
    uint32_t *st = T.stats_map[slot];
    if (st[0]) {
        for (int i = 0; i < 5; i++) T.sum[i] += st[i];
        if (++T.composes % 600 == 0 && T.sum[0]) {
            rt_log("dlaa: composite: shown against the game's image, mean difference in 1/1000: as is %.1f, sRGB-decoded "
                   "%.1f, sRGB-encoded %.1f; %.1f%% counted as HUD",
                   (double)T.sum[1] / (double)T.sum[0], (double)T.sum[2] / (double)T.sum[0], (double)T.sum[3] / (double)T.sum[0],
                   100.0 * (double)T.sum[4] / (double)T.sum[0]);
            memset(T.sum, 0, sizeof T.sum);
        }
    }
    vkCmdFillBuffer(cb, T.stats[slot], 0, 32, 0);
    full_barrier(cb);
    VkDescriptorImageInfo ii[6] = {{T.linear, T.shown_view[slot], VK_IMAGE_LAYOUT_GENERAL},
                                   {T.sampler, T.view[3], VK_IMAGE_LAYOUT_GENERAL},
                                   {T.sampler, T.view[2], VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, T.view[4], VK_IMAGE_LAYOUT_GENERAL},
                                   {T.sampler, T.hud_view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, T.view[5], VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo sbi = {T.stats[slot], 0, 32};
    VkWriteDescriptorSet wr[7];
    for (int i = 0; i < 7; i++)
        wr[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = (uint32_t)i,
                                       .descriptorCount = 1, .pImageInfo = i < 5 ? &ii[i] : i == 6 ? &ii[5] : NULL,
                                       .pBufferInfo = i == 5 ? &sbi : NULL,
                                       .descriptorType = i == 3 || i == 6 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                       : i == 5 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    // TOTK_DLSS_HUD_DIFF: the difference (in 1/255) that counts as HUD; default 3 (a copy through sRGB rounds by 1)
    static float eps = -1;
    if (eps < 0) { const char *e = cfg_get("TOTK_DLSS_HUD_DIFF"); eps = (e && *e ? (float)atof(e) : 3.0f) / 255.0f; }
    // TOTK_DLSS_CONVERT: 1 (default) decodes, 2 encodes, 0 keeps DLSS's picture, as the copy into the presented image
    // converts: Violet's last draw reads the game's image sRGB-decoded (test CA: shown against it decoded differs by
    // 0.7 / 1000, as is by 160-220 / 1000)
    static int conv = -1;
    if (conv < 0) { const char *e = cfg_get("TOTK_DLSS_CONVERT"); conv = e && *e ? atoi(e) : 1; }
    float pc[8] = {(float)T.ow, (float)T.oh, (float)T.w, (float)T.h, eps, rt_knob("DLSS_DEBUG", "0") ? 1.0f : 0.0f, 0, (float)conv};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, T.compose_pipe);
    rec_push(T.push, cb, VK_PIPELINE_BIND_POINT_COMPUTE, T.clayout, 0, 7, wr);
    vkCmdPushConstants(cb, T.clayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    vkCmdDispatch(cb, (T.ow + 7) / 8, (T.oh + 7) / 8, 1);
    full_barrier(cb);
    if (T.mode == 1) slfg_frame(cb, &T.fg);   // DLSS Frame Generation only consumes NVIDIA's resolve
    *out = T.img[4], *ow = (int)T.ow, *oh = (int)T.oh;
    static int logged;
    if (!logged++) rt_log("%s: the present shows the %ux%u resolved scene with the game's HUD over it",
                          T.mode == 2 ? "fsr2" : "dlaa", T.ow, T.oh);
    return 1;
}
