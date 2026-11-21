#include "platform/native_crash.hpp"
#include "gpu/native_renderer.hpp"
#include "gpu/texture_decode.hpp"
#include "gpu/triangle_indices.hpp"
#include "gpu/vertex_lighting.hpp"
#include "gpu/parallel_vertices.hpp"
#include "psprecomp/common.hpp"
#include "sims2_spirv.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
namespace sims2::gpu {
namespace {
constexpr unsigned width = 512, height = 272;
void require(bool ok, const char *what) {
    if (!ok)
        throw psprecomp::Error(std::string(what) + ": " + SDL_GetError());
}
using V4 = std::array<float, 4>;
V4 multiply(const std::array<float, 16> &matrix, const V4 &v) {
    V4 out{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            out[row] += matrix[col * 4 + row] * v[col];
    return out;
}
V4 rgba(std::uint32_t color) {
    return {float(color & 255) / 255, float((color >> 8) & 255) / 255, float((color >> 16) & 255) / 255,
            float(color >> 24) / 255};
}
std::uint32_t expand(std::uint32_t raw, unsigned format) {
    if (format == 3)
        return raw;
    unsigned r{}, g{}, b{}, a{};
    if (format == 0) {
        r = (raw & 31) * 255 / 31;
        g = ((raw >> 5) & 63) * 255 / 63;
        b = ((raw >> 11) & 31) * 255 / 31;
        a = 255;
    } else if (format == 1) {
        r = (raw & 31) * 255 / 31;
        g = ((raw >> 5) & 31) * 255 / 31;
        b = ((raw >> 10) & 31) * 255 / 31;
        a = (raw & 0x8000) ? 255 : 0;
    } else {
        r = (raw & 15) * 17;
        g = ((raw >> 4) & 15) * 17;
        b = ((raw >> 8) & 15) * 17;
        a = ((raw >> 12) & 15) * 17;
    }
    return r | (g << 8) | (b << 16) | (a << 24);
}
std::uint16_t pack(std::uint32_t raw, unsigned format) {
    const auto r = raw & 255u, g = (raw >> 8) & 255u, b = (raw >> 16) & 255u, a = raw >> 24;
    if (format == 0)
        return static_cast<std::uint16_t>((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11));
    if (format == 1)
        return static_cast<std::uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >> 7) << 15));
    return static_cast<std::uint16_t>((r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12));
}
struct GpuVertex {
    V4 position;
    std::array<float, 3> uv;
    V4 color;
    V4 extra;
};
struct Parameters {
    std::uint32_t textured, function, texture_alpha, alpha_enabled;
    std::uint32_t alpha_function, alpha_reference, alpha_mask, double_color;
    V4 environment;
    V4 fog;
};

} // namespace

struct NativeRenderer::Impl {
    bool video{};
    SDL_GPUDevice *device{};
    SDL_GPUShader *vs{}, *ps{};
    SDL_GPUBuffer *vertices{};
    SDL_GPUCommandBuffer *commands{};
    unsigned vertex_cursor{}, vertex_capacity = 16 * 1024 * 1024, anisotropy{};
    std::uint64_t count{}, flush_count{}, draw_ns{}, flush_ns{}, vertex_ns{}, wait_ns{};
    float scale{1}, aspect_factor{1};
    std::vector<GpuVertex> output_vertices, converted_vertices;
    ParallelVertices parallel;
    std::vector<std::uint32_t> converted_generation;
    std::vector<std::uint16_t> triangle_vertices;
    std::uint32_t conversion_generation{};
    // Staging arena and vertex buffer are filled front to back at disjoint
    // offsets and keep filling across submissions. They are cycled only when
    // they wrap. Cycling on every submission made SDL allocate a fresh 32 MB
    // arena and 16 MB vertex buffer whenever the previous submission was still
    // in flight; a slower GPU then ran out of device memory in vkQueueSubmit.
    SDL_GPUTransferBuffer *uploads{};
    unsigned upload_cursor{}, upload_capacity=32u*1024u*1024u;
    struct Upload { SDL_GPUTransferBuffer *buffer; unsigned offset; };
    struct Target {
        RenderTarget guest;
        bool dirty{}, upload{true};
        std::vector<std::uint8_t> snapshot;
        SDL_GPUTexture *color{}, *depth{}, *native{}, *presentation{};
        bool presentation_ready{};
        SDL_GPUTransferBuffer *readback{};
        // Set while a readback has been submitted and its pixels are not in
        // guest memory yet. snapshot then holds what guest memory held when
        // the readback started.
        SDL_GPUFence *fence{};
        // The game wrote into this buffer while a readback was in flight, so
        // guest memory, not the GPU copy, has the newest picture.
        bool guest_modified{};
    };
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, Target> targets;
    std::map<std::uint64_t, SDL_GPUTexture *> textures;
    std::map<unsigned, SDL_GPUSampler *> samplers;
    std::map<std::array<unsigned, 12>, SDL_GPUGraphicsPipeline *> pipelines;
    SDL_GPUTexture *white{};
    SDL_GPUGraphicsPipeline *opaque_presentation{};
    SDL_GPUSampler *presentation_sampler{};
    unsigned scaled_width() const { return std::max(1u, unsigned(std::lround(width * scale))); }
    unsigned scaled_height() const { return std::max(1u, unsigned(std::lround(height * scale))); }
    SDL_GPUCommandBuffer *cmd() {
        if (!commands) {
            commands = platform::native_call("SDL_AcquireGPUCommandBuffer", [&] { return SDL_AcquireGPUCommandBuffer(device); });
            require(commands, "Acquire Vulkan commands");
        }
        return commands;
    }
    // Test hook: report the Nth submission as failed (SIMS2_TEST_FAIL_SUBMIT=N).
    unsigned fail_submit_at = [] { const auto *n = std::getenv("SIMS2_TEST_FAIL_SUBMIT"); return n ? unsigned(std::strtoul(n, nullptr, 10)) : 0u; }();
    unsigned submit_count{};
    std::uint64_t submissions{};
    // Draws recorded since the last submission. Recorded work only reaches the
    // GPU when it is submitted, so a few submissions inside the frame let the
    // GPU work alongside the game. Each submission also costs a scheduling
    // round trip, so they are kept to a handful per frame.
    unsigned draws_since_submit{};
    // Test hook: SIMS2_TEST_SUBMIT_DRAWS=0 submits once per frame.
    unsigned submit_draws = [] {
        const auto *n = std::getenv("SIMS2_TEST_SUBMIT_DRAWS");
        return n ? unsigned(std::strtoul(n, nullptr, 10)) : 32u;
    }();
    // Consecutive draws to one target share one vertex upload and one render
    // pass. A pass per draw made the GPU switch the target between its
    // attachment and sampling layouts, and stall for the vertex copy, once
    // for every draw of the frame.
    struct PendingDraw {
        SDL_GPUGraphicsPipeline *pipeline;
        SDL_GPUTexture *image;
        SDL_GPUSampler *sampler;
        SDL_Rect scissor;
        SDL_FColor constant;
        Parameters params;
        unsigned first, count;
    };
    Target *batch_target{};
    unsigned batch_start{};
    std::vector<GpuVertex> batch_vertices, batch_spare;
    std::vector<PendingDraw> batch, batch_recording;
    // Test hook: SIMS2_TEST_BATCH=0 records one render pass per draw.
    bool batching = [] {
        const auto *n = std::getenv("SIMS2_TEST_BATCH");
        return !n || std::strcmp(n, "0") != 0;
    }();
    void end_batch();
    // A frame's pixels are copied back to guest memory when the GPU has
    // finished them, not by stopping the game until it has. Anything that
    // reads those pixels first (a block transfer, a framebuffer texture, the
    // next draw into the same buffer) completes the copy at that point.
    // Test hook: SIMS2_TEST_SYNC_READBACK=1 waits for every frame again.
    bool deferred_readback = std::getenv("SIMS2_TEST_SYNC_READBACK") == nullptr;
    psprecomp::GuestMemory *frame_memory{};
    void start_readback(Target &t, psprecomp::GuestMemory &memory);
    void finish_readback(Target &t, psprecomp::GuestMemory &memory);
    void submit() {
        // Queued draws belong to this submission.
        end_batch();
        // SDL owns the buffer from the moment submission is attempted, even when
        // it fails (lost device, out of memory). Forget it before reporting the
        // failure: recording into a buffer that is no longer recording faults
        // inside AMD's Vulkan driver (null read at 0x10) instead of erroring.
        auto *pending = std::exchange(commands, nullptr);
        draws_since_submit = 0;
        if (!pending)
            return;
        ++submissions;
        bool ok = platform::native_call("SDL_SubmitGPUCommandBuffer", [&] { return SDL_SubmitGPUCommandBuffer(pending); });
        if (simulated_failure())
            ok = false;
        require(ok, "Submit Vulkan commands");
    }
    bool simulated_failure() {
        if (!fail_submit_at || ++submit_count != fail_submit_at)
            return false;
        SDL_SetError("simulated submission failure");
        return true;
    }
    SDL_GPUTexture *texture(unsigned w, unsigned h, SDL_GPUTextureUsageFlags usage,
                            SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = w;
        info.height = h;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        auto *t = platform::native_call("SDL_CreateGPUTexture", [&] { return SDL_CreateGPUTexture(device, &info); });
        require(t, "Create Vulkan texture");
        return t;
    }
    Upload upload(const void *data, unsigned bytes) {
        const unsigned reserved=(bytes+255u)&~255u;
        if(reserved>upload_capacity)throw psprecomp::Error("Vulkan upload exceeds staging arena");
        if(upload_cursor+reserved>upload_capacity){submit();upload_cursor=0;}
        if(!uploads) {
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size=upload_capacity;
            uploads=platform::native_call("SDL_CreateGPUTransferBuffer", [&] { return SDL_CreateGPUTransferBuffer(device,&info); });
            require(uploads,"Create Vulkan upload arena");
        }
        const auto offset=upload_cursor;
        auto *mapped = platform::native_call("SDL_MapGPUTransferBuffer", [&] { return SDL_MapGPUTransferBuffer(device, uploads, offset==0); });
        require(mapped, "Map Vulkan upload");
        std::memcpy(static_cast<std::uint8_t*>(mapped)+offset, data, bytes);
        platform::native_call("SDL_UnmapGPUTransferBuffer", [&] { return SDL_UnmapGPUTransferBuffer(device, uploads); });
        upload_cursor+=reserved;
        return {uploads,offset};
    }
    void upload_texture(SDL_GPUTexture *t, const std::uint32_t *data, unsigned w, unsigned h) {
        const auto transfer = upload(data, w * h * 4);
        auto *copy = platform::native_call("SDL_BeginGPUCopyPass", [&] { return SDL_BeginGPUCopyPass(cmd()); });
        require(copy, "Begin Vulkan copy pass");
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = transfer.buffer;
        src.offset = transfer.offset;
        src.pixels_per_row = w;
        src.rows_per_layer = h;
        SDL_GPUTextureRegion dst{};
        dst.texture = t;
        dst.w = w;
        dst.h = h;
        dst.d = 1;
        platform::native_call("SDL_UploadToGPUTexture", [&] { return SDL_UploadToGPUTexture(copy, &src, &dst, false); });
        platform::native_call("SDL_EndGPUCopyPass", [&] { return SDL_EndGPUCopyPass(copy); });
    }
    void blit(SDL_GPUTexture *src, SDL_GPUTexture *dst, unsigned sw, unsigned sh, unsigned dw, unsigned dh) {
        SDL_GPUBlitInfo info{};
        info.source.texture = src;
        info.source.w = sw;
        info.source.h = sh;
        info.destination.texture = dst;
        info.destination.w = dw;
        info.destination.h = dh;
        info.load_op = SDL_GPU_LOADOP_DONT_CARE;
        info.filter = SDL_GPU_FILTER_NEAREST;
        platform::native_call("SDL_BlitGPUTexture", [&] { return SDL_BlitGPUTexture(cmd(), &info); });
    }
    SDL_GPUTexture *presentation(Target &target) {
        if(target.presentation_ready)return target.presentation;
        platform::NativeCallScope phase("vulkan-presentation");
        end_batch();
        if(!target.presentation)
            target.presentation=texture(scaled_width(),scaled_height(),
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
        // PSP framebuffer alpha is game/stencil data, not window opacity.
        // Keep it intact in color/native and make only the display copy opaque.
        if(!opaque_presentation) {
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader=vs;info.fragment_shader=ps;
            info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            SDL_GPUVertexBufferDescription buffer{0,sizeof(GpuVertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
            const SDL_GPUVertexAttribute attributes[]{
                {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,0},
                {1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(GpuVertex,uv)},
                {2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(GpuVertex,color)},
                {3,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(GpuVertex,extra)}};
            info.vertex_input_state={&buffer,1,attributes,4};
            info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
            info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
            SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            color.blend_state.enable_color_write_mask=true;
            color.blend_state.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|
                SDL_GPU_COLORCOMPONENT_B|SDL_GPU_COLORCOMPONENT_A;
            color.blend_state.src_color_blendfactor=color.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_color_blendfactor=color.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
            color.blend_state.color_blend_op=color.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;
            opaque_presentation=platform::native_call("SDL_CreateGPUGraphicsPipeline", [&] { return SDL_CreateGPUGraphicsPipeline(device,&info); });
            require(opaque_presentation,"Create opaque presentation pipeline");
            SDL_GPUSamplerCreateInfo sampler{};
            presentation_sampler=platform::native_call("SDL_CreateGPUSampler", [&] { return SDL_CreateGPUSampler(device,&sampler); });
            require(presentation_sampler,"Create presentation sampler");
        }
        std::array<GpuVertex,4> quad{};
        for(unsigned i=0;i<4;++i) {
            quad[i].position={i&1u?1.f:-1.f,i&2u?-1.f:1.f,0,1};
            quad[i].color={1,1,1,1};quad[i].extra[3]=1;
            quad[i].uv={i&1u?1.f:0.f,i&2u?1.f:0.f,1};
        }
        if(vertex_cursor+sizeof(quad)>vertex_capacity){submit();vertex_cursor=0;}
        const auto transfer=upload(quad.data(),sizeof(quad));
        auto *copy=platform::native_call("SDL_BeginGPUCopyPass", [&] { return SDL_BeginGPUCopyPass(cmd()); });
        require(copy, "Begin Vulkan presentation copy pass");
        SDL_GPUTransferBufferLocation source{transfer.buffer,transfer.offset};
        SDL_GPUBufferRegion destination{vertices,vertex_cursor,sizeof(quad)};
        platform::native_call("SDL_UploadToGPUBuffer", [&] { return SDL_UploadToGPUBuffer(copy,&source,&destination,vertex_cursor==0); });platform::native_call("SDL_EndGPUCopyPass", [&] { return SDL_EndGPUCopyPass(copy); });
        SDL_GPUColorTargetInfo color{};color.texture=target.presentation;
        color.load_op=SDL_GPU_LOADOP_DONT_CARE;color.store_op=SDL_GPU_STOREOP_STORE;
        auto *pass=platform::native_call("SDL_BeginGPURenderPass", [&] { return SDL_BeginGPURenderPass(cmd(),&color,1,nullptr); });
        require(pass,"Begin opaque presentation pass");
        platform::native_call("SDL_BindGPUGraphicsPipeline", [&] { return SDL_BindGPUGraphicsPipeline(pass,opaque_presentation); });
        SDL_GPUViewport viewport{0,0,float(scaled_width()),float(scaled_height()),0,1};
        SDL_SetGPUViewport(pass,&viewport);
        SDL_Rect scissor{0,0,int(scaled_width()),int(scaled_height())};SDL_SetGPUScissor(pass,&scissor);
        SDL_GPUBufferBinding binding{vertices,vertex_cursor};platform::native_call("SDL_BindGPUVertexBuffers", [&] { return SDL_BindGPUVertexBuffers(pass,0,&binding,1); });
        SDL_GPUTextureSamplerBinding sample{target.color,presentation_sampler};platform::native_call("SDL_BindGPUFragmentSamplers", [&] { return SDL_BindGPUFragmentSamplers(pass,0,&sample,1); });
        Parameters parameters{};parameters.textured=1;parameters.function=3;
        SDL_PushGPUFragmentUniformData(cmd(),0,&parameters,sizeof(parameters));
        platform::native_call("SDL_DrawGPUPrimitives", [&] { return SDL_DrawGPUPrimitives(pass,4,1,0,0); });platform::native_call("SDL_EndGPURenderPass", [&] { return SDL_EndGPURenderPass(pass); });
        vertex_cursor+=sizeof(quad);
        submit();target.presentation_ready=true;
        return target.presentation;
    }
    Target &target(psprecomp::GuestMemory &memory, const RenderTarget &guest) {
        if (guest.color_stride < 1 || guest.color_stride > 2048 || guest.color_format > 3 ||
            !memory.contains(guest.color_address,
                             std::size_t(guest.color_stride) * height * (guest.color_format == 3 ? 4 : 2)))
            throw psprecomp::Error("Unmapped Vulkan GE target");
        auto [it, inserted] =
            targets.try_emplace({guest.color_address, guest.color_stride, guest.color_format});
        auto &out = it->second;
        if (inserted) {
            out.guest = guest;
            out.color = texture(scaled_width(), scaled_height(),
                                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
            out.native =
                texture(width, height, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
            out.depth = texture(scaled_width(), scaled_height(), SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
            SDL_GPUDepthStencilTargetInfo z{};
            z.texture = out.depth;
            z.clear_depth = 0;
            z.load_op = SDL_GPU_LOADOP_CLEAR;
            z.store_op = SDL_GPU_STOREOP_STORE;
            z.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            z.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            auto *pass = platform::native_call("SDL_BeginGPURenderPass", [&] { return SDL_BeginGPURenderPass(cmd(), nullptr, 0, &z); });
            require(pass, "Clear Vulkan depth");
            platform::native_call("SDL_EndGPURenderPass", [&] { return SDL_EndGPURenderPass(pass); });
        }
        // Pixels still on their way back belong in guest memory before the
        // game draws over this buffer again.
        if (out.fence)
            finish_readback(out, memory);
        if (out.upload) {
            const auto size = std::size_t(guest.color_stride) * height * (guest.color_format == 3 ? 4 : 2);
            const auto *raw = memory.raw_pointer(guest.color_address, size);
            if (raw && !out.guest_modified && out.snapshot.size() == size &&
                !std::memcmp(raw, out.snapshot.data(), size)) {
                out.upload = false;
                return out;
            }
            // The blit below replaces this target; draws queued for it land first.
            if (batch_target == &out)
                end_batch();
            std::vector<std::uint32_t> pixels(width * height);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < std::min(width, guest.color_stride); ++x) {
                    auto at = guest.color_address +
                              (y * guest.color_stride + x) * (guest.color_format == 3 ? 4 : 2);
                    pixels[y * width + x] = expand(
                        guest.color_format == 3 ? memory.load32(at) : memory.load16(at), guest.color_format);
                }
            upload_texture(out.native, pixels.data(), width, height);
            blit(out.native, out.color, width, height, scaled_width(), scaled_height());
            out.upload = false;
            out.guest_modified = false;
        }
        return out;
    }
    void clear_targets(bool checked = true) {
        submit();
        const auto idle = platform::native_call("SDL_WaitForGPUIdle", [&] { return SDL_WaitForGPUIdle(device); });
        if (checked) require(idle, "Wait Vulkan targets idle");
        for (auto &[key, t] : targets) {
            SDL_ReleaseGPUTexture(device, t.color);
            SDL_ReleaseGPUTexture(device, t.depth);
            SDL_ReleaseGPUTexture(device, t.native);
            if(t.readback)SDL_ReleaseGPUTransferBuffer(device,t.readback);
            if(t.fence)SDL_ReleaseGPUFence(device,t.fence);
            if(t.presentation)SDL_ReleaseGPUTexture(device,t.presentation);
        }
        targets.clear();
    }
    ~Impl() {
        if (!device) {
            if (video)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }
        // Destruction must not throw, including after a lost device. Unsubmitted
        // work can be discarded here; normal flushes submit before destruction.
        batch.clear();
        batch_vertices.clear();
        batch_target = nullptr;
        if(commands){SDL_CancelGPUCommandBuffer(commands);commands=nullptr;}
        platform::native_call("SDL_WaitForGPUIdle", [&] { return SDL_WaitForGPUIdle(device); });
        clear_targets(false);
        for (auto &[key, t] : textures)
            SDL_ReleaseGPUTexture(device, t);
        for (auto &[key, t] : samplers)
            SDL_ReleaseGPUSampler(device, t);
        for (auto &[key, t] : pipelines)
            SDL_ReleaseGPUGraphicsPipeline(device, t);
        if (white)
            SDL_ReleaseGPUTexture(device, white);
        if (vertices)
            SDL_ReleaseGPUBuffer(device, vertices);
        if(uploads)
            SDL_ReleaseGPUTransferBuffer(device,uploads);
        if(opaque_presentation)SDL_ReleaseGPUGraphicsPipeline(device,opaque_presentation);
        if(presentation_sampler)SDL_ReleaseGPUSampler(device,presentation_sampler);
        if (vs)
            SDL_ReleaseGPUShader(device, vs);
        if (ps)
            SDL_ReleaseGPUShader(device, ps);
        SDL_DestroyGPUDevice(device);
        if (video)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
void NativeRenderer::Impl::end_batch() {
    auto *target = std::exchange(batch_target, nullptr);
    if (batch.empty())
        return;
    // Taken out before recording: uploading can submit, which ends the batch
    // again, and a failed call must not replay these draws.
    batch_recording.swap(batch);
    batch_spare.swap(batch_vertices);
    const auto &draws = batch_recording;
    const auto bytes = unsigned(batch_spare.size() * sizeof(GpuVertex));
    struct Clear {
        Impl &r;
        ~Clear() {
            r.batch_recording.clear();
            r.batch_spare.clear();
        }
    } clear{*this};
    const auto transfer = upload(batch_spare.data(), bytes);
    auto *copy = platform::native_call("SDL_BeginGPUCopyPass", [&] { return SDL_BeginGPUCopyPass(cmd()); });
    require(copy, "Begin Vulkan copy pass");
    SDL_GPUTransferBufferLocation src{transfer.buffer, transfer.offset};
    SDL_GPUBufferRegion dst{vertices, batch_start, bytes};
    platform::native_call("SDL_UploadToGPUBuffer", [&] { return SDL_UploadToGPUBuffer(copy, &src, &dst, batch_start == 0); });
    platform::native_call("SDL_EndGPUCopyPass", [&] { return SDL_EndGPUCopyPass(copy); });
    SDL_GPUColorTargetInfo color{};
    color.texture = target->color;
    color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = target->depth;
    depth.load_op = SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    auto *pass = platform::native_call("SDL_BeginGPURenderPass", [&] { return SDL_BeginGPURenderPass(cmd(), &color, 1, &depth); });
    require(pass, "Begin Vulkan GE pass");
    SDL_GPUViewport viewport{0, 0, float(scaled_width()), float(scaled_height()), 0, 1};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_GPUBufferBinding binding{vertices, 0};
    platform::native_call("SDL_BindGPUVertexBuffers", [&] { return SDL_BindGPUVertexBuffers(pass, 0, &binding, 1); });
    SDL_GPUGraphicsPipeline *bound{};
    for (const auto &d : draws) {
        if (d.pipeline != bound) {
            platform::native_call("SDL_BindGPUGraphicsPipeline", [&] { return SDL_BindGPUGraphicsPipeline(pass, d.pipeline); });
            bound = d.pipeline;
        }
        SDL_SetGPUScissor(pass, &d.scissor);
        SDL_SetGPUBlendConstants(pass, d.constant);
        SDL_GPUTextureSamplerBinding sample{d.image, d.sampler};
        platform::native_call("SDL_BindGPUFragmentSamplers", [&] { return SDL_BindGPUFragmentSamplers(pass, 0, &sample, 1); });
        SDL_PushGPUFragmentUniformData(cmd(), 0, &d.params, sizeof(d.params));
        platform::native_call("SDL_DrawGPUPrimitives", [&] { return SDL_DrawGPUPrimitives(pass, d.count, 1, d.first, 0); });
    }
    platform::native_call("SDL_EndGPURenderPass", [&] { return SDL_EndGPURenderPass(pass); });
}
// Submits everything recorded so far together with a copy of the target at
// PSP resolution. The pixels reach guest memory in finish_readback.
void NativeRenderer::Impl::start_readback(Target &t, psprecomp::GuestMemory &memory) {
    blit(t.color, t.native, scaled_width(), scaled_height(), width, height);
    if (!t.readback) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = width * height * 4;
        t.readback = platform::native_call("SDL_CreateGPUTransferBuffer", [&] { return SDL_CreateGPUTransferBuffer(device, &info); });
    }
    require(t.readback, "Create Vulkan readback");
    auto *pass = platform::native_call("SDL_BeginGPUCopyPass", [&] { return SDL_BeginGPUCopyPass(cmd()); });
    require(pass, "Begin Vulkan readback pass");
    SDL_GPUTextureRegion src{};
    src.texture = t.native;
    src.w = width;
    src.h = height;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = t.readback;
    dst.pixels_per_row = width;
    dst.rows_per_layer = height;
    platform::native_call("SDL_DownloadFromGPUTexture", [&] { return SDL_DownloadFromGPUTexture(pass, &src, &dst); });
    platform::native_call("SDL_EndGPUCopyPass", [&] { return SDL_EndGPUCopyPass(pass); });
    auto *fence = platform::native_call("SDL_SubmitGPUCommandBufferAndAcquireFence", [&] { return SDL_SubmitGPUCommandBufferAndAcquireFence(commands); });
    commands = nullptr;
    draws_since_submit = 0;
    ++submissions;
    if (fence && simulated_failure()) {
        // The work was really submitted: let it finish before dropping the fence.
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);
        fence = nullptr;
    }
    require(fence, "Submit Vulkan readback");
    t.fence = fence;
    t.dirty = false;
    t.upload = true;
    // What the game can see until the pixels arrive. finish_readback replaces
    // only pixels that still hold these values, so anything the game writes
    // in the meantime stays on top, as if the copy had happened here.
    const auto size = std::size_t(t.guest.color_stride) * height * (t.guest.color_format == 3 ? 4 : 2);
    if (const auto *raw = memory.raw_pointer(t.guest.color_address, size))
        t.snapshot.assign(raw, raw + size);
    else
        t.snapshot.clear();
}
void NativeRenderer::Impl::finish_readback(Target &t, psprecomp::GuestMemory &memory) {
    auto *fence = std::exchange(t.fence, nullptr);
    const auto waiting = std::chrono::steady_clock::now();
    const bool waited = platform::native_call("SDL_WaitForGPUFences", [&] { return SDL_WaitForGPUFences(device, true, &fence, 1); });
    wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waiting).count();
    SDL_ReleaseGPUFence(device, fence);
    require(waited, "Wait Vulkan readback");
    auto *pixels =
        static_cast<const std::uint32_t *>(platform::native_call("SDL_MapGPUTransferBuffer", [&] { return SDL_MapGPUTransferBuffer(device, t.readback, false); }));
    require(pixels, "Map Vulkan readback");
    const auto &g = t.guest;
    const unsigned bytes_per_pixel = g.color_format == 3 ? 4u : 2u, columns = std::min(width, g.color_stride);
    const auto size = std::size_t(g.color_stride) * height * bytes_per_pixel;
    auto *raw = memory.raw_pointer(g.color_address, size);
    bool kept_guest_pixels = false;
    if (raw && t.snapshot.size() == size) {
        const bool untouched = !std::memcmp(raw, t.snapshot.data(), size);
        for (unsigned y = 0; y < height; ++y) {
            auto *row = raw + std::size_t(y) * g.color_stride * bytes_per_pixel;
            const auto *before = t.snapshot.data() + std::size_t(y) * g.color_stride * bytes_per_pixel;
            if (untouched && bytes_per_pixel == 4) {
                std::memcpy(row, pixels + y * width, columns * 4u);
                continue;
            }
            for (unsigned x = 0; x < columns; ++x) {
                auto *at = row + x * bytes_per_pixel;
                if (!untouched && std::memcmp(at, before + x * bytes_per_pixel, bytes_per_pixel)) {
                    kept_guest_pixels = true;
                    continue;
                }
                if (bytes_per_pixel == 4) {
                    std::memcpy(at, pixels + y * width + x, 4);
                } else {
                    const auto packed = pack(pixels[y * width + x], g.color_format);
                    std::memcpy(at, &packed, 2);
                }
            }
        }
    } else {
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < columns; ++x) {
                const auto at = g.color_address + (y * g.color_stride + x) * bytes_per_pixel;
                if (g.color_format == 3)
                    memory.store32(at, pixels[y * width + x]);
                else
                    memory.store16(at, pack(pixels[y * width + x], g.color_format));
            }
        raw = memory.raw_pointer(g.color_address, size);
    }
    platform::native_call("SDL_UnmapGPUTransferBuffer", [&] { return SDL_UnmapGPUTransferBuffer(device, t.readback); });
    ++flush_count;
    t.guest_modified = kept_guest_pixels;
    if (raw)
        t.snapshot.assign(raw, raw + size);
}
NativeRenderer::NativeRenderer(bool) : impl_(std::make_unique<Impl>()) {
    auto &r = *impl_;
    SDL_SetAppMetadata("The Sims 2 PSP Recomp", "0.4.9", "io.github.sims2psprecomp");
    if (std::getenv("SIMS2_GPU_DEBUG")) {
        SDL_SetLogOutputFunction(+[](void *, int, SDL_LogPriority, const char *message) {
            std::cerr << "[sdl-gpu] " << message << '\n';
        }, nullptr);
        SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_VERBOSE);
    }
    require(SDL_InitSubSystem(SDL_INIT_VIDEO), "Initialize SDL Vulkan video");
    r.video = true;
    r.device =
        platform::native_call("SDL_CreateGPUDevice", [&] { return SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, std::getenv("SIMS2_GPU_DEBUG") != nullptr, "vulkan"); });
    require(r.device, "Create Vulkan GPU device");
    auto shader = [&](auto &code, SDL_GPUShaderStage stage) {
        SDL_GPUShaderCreateInfo info{};
        info.code = reinterpret_cast<const Uint8 *>(code);
        info.code_size = sizeof(code);
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = stage;
        if (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) {
            info.num_samplers = 1;
            info.num_uniform_buffers = 1;
        }
        auto *s = platform::native_call("SDL_CreateGPUShader", [&] { return SDL_CreateGPUShader(r.device, &info); });
        require(s, "Create Vulkan shader");
        return s;
    };
    r.vs = shader(shaders::vertex, SDL_GPU_SHADERSTAGE_VERTEX);
    r.ps = shader(shaders::fragment, SDL_GPU_SHADERSTAGE_FRAGMENT);
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = r.vertex_capacity;
    r.vertices = platform::native_call("SDL_CreateGPUBuffer", [&] { return SDL_CreateGPUBuffer(r.device, &info); });
    require(r.vertices, "Create Vulkan vertex buffer");
    r.white = r.texture(1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    const std::uint32_t white = 0xffffffff;
    r.upload_texture(r.white, &white, 1, 1);
    r.submit();
    std::cerr << "[graphics] Vulkan (SDL GPU) adapter: " << SDL_GetStringProperty(SDL_GetGPUDeviceProperties(r.device),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown") << "\n";
    const auto properties = SDL_GetGPUDeviceProperties(r.device);
    std::cerr << "[graphics] SDL " << SDL_GetVersion()
        << " Vulkan driver: " << SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "unknown")
        << " version: " << SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "unknown")
        << " info: " << SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, "unknown") << '\n';

}
NativeRenderer::~NativeRenderer() {
    if(std::getenv("SIMS2_PROFILE"))std::cerr<<"[vulkan] cached_targets="<<impl_->targets.size()
        <<" cached_textures="<<impl_->textures.size()<<" cached_pipelines="<<impl_->pipelines.size()<<" submissions="<<impl_->submissions<<'\n';
}
void *NativeRenderer::gpu_device() const noexcept { return impl_->device; }
std::array<unsigned, 2> NativeRenderer::gpu_dimensions() const noexcept {
    return {impl_->scaled_width(), impl_->scaled_height()};
}
ID3D11Device *NativeRenderer::device() const noexcept { return nullptr; }
ID3D11DeviceContext *NativeRenderer::context() const noexcept { return nullptr; }
ID3D11ShaderResourceView *NativeRenderer::presentation_view(const psprecomp::GuestMemory &, std::uint32_t,
                                                            unsigned, unsigned) const {
    return nullptr;
}
void *NativeRenderer::gpu_presentation(const psprecomp::GuestMemory &memory, std::uint32_t address,
                                       unsigned stride, unsigned format) const {
    auto &r = *impl_;
    auto it = r.targets.find({address, stride, format});
    if (it == r.targets.end())
        return nullptr;
    auto &t = it->second;
    if (t.dirty || t.guest_modified)
        return nullptr;
    const auto size = std::size_t(stride) * height * (format == 3 ? 4 : 2);
    auto *raw = memory.raw_pointer(address, size);
    return raw && t.snapshot.size() == size && !std::memcmp(raw, t.snapshot.data(), size) ? r.presentation(t) : nullptr;
}
std::uint64_t NativeRenderer::draws() const noexcept { return impl_->count; }
std::uint64_t NativeRenderer::gpu_submissions() const noexcept { return impl_->submissions; }
std::uint64_t NativeRenderer::framebuffer_readbacks() const noexcept { return impl_->flush_count; }
NativeRenderer::ProfileStats NativeRenderer::profile_stats() const noexcept {
    return {impl_->draw_ns, impl_->flush_ns, impl_->vertex_ns, impl_->wait_ns};
}
void NativeRenderer::set_render_scale(float scale) {
    if (!std::isfinite(scale) || scale < .5f || scale > 16)
        throw psprecomp::Error("Render scale must be between 0.5 and 16");
    if (scale != impl_->scale) {
        for (auto &[key, t] : impl_->targets) {
            if (t.dirty)
                throw psprecomp::Error("Flush framebuffers before changing render scale");
            if (t.fence && impl_->frame_memory)
                impl_->finish_readback(t, *impl_->frame_memory);
        }
        impl_->clear_targets();
        impl_->scale = scale;
    }
}
float NativeRenderer::render_scale() const noexcept { return impl_->scale; }
void NativeRenderer::set_anisotropy(unsigned level) {
    if (level != 0 && level != 2 && level != 4 && level != 8 && level != 16)
        throw psprecomp::Error("Invalid anisotropic filtering level");
    impl_->anisotropy = level;
}
void NativeRenderer::set_display_aspect(float aspect) {
    if (!std::isfinite(aspect) || aspect < .01f || aspect > 128.f)
        throw psprecomp::Error("Unsupported display aspect ratio");
    impl_->aspect_factor = (480.f / 272) / aspect;
}
void NativeRenderer::flush(psprecomp::GuestMemory &memory) { flush(memory, nullptr); }
void NativeRenderer::prepare_transfer(psprecomp::GuestMemory &memory, const BlockTransfer &transfer) {
    flush(memory, &transfer);
}
void NativeRenderer::flush(psprecomp::GuestMemory &memory, const BlockTransfer *transfer) {
    auto &r = *impl_;
    platform::NativeCallScope phase("vulkan-flush");
    auto start = std::chrono::steady_clock::now();
    for (auto &[key, t] : r.targets) {
        if (!t.dirty && !t.fence)
            continue;
        if (transfer && transfer->width && transfer->height) {
            const std::uint64_t begin = t.guest.color_address & 0x0fffffffu,
                                end = begin + std::uint64_t(t.guest.color_stride) * height *
                                                  (t.guest.color_format == 3 ? 4 : 2);
            auto overlaps = [&](unsigned address, unsigned stride, unsigned x, unsigned y) {
                auto first =
                    (address & 0x0fffffffu) + (std::uint64_t(y) * stride + x) * transfer->bytes_per_pixel;
                auto last = first + (std::uint64_t(transfer->height - 1) * stride + transfer->width) *
                                        transfer->bytes_per_pixel;
                return first < end && begin < last;
            };
            if (!overlaps(transfer->source, transfer->source_stride, transfer->source_x,
                          transfer->source_y) &&
                !overlaps(transfer->destination, transfer->destination_stride, transfer->destination_x,
                          transfer->destination_y))
                continue;
        }
        if (t.dirty) {
            if (t.fence)
                r.finish_readback(t, memory);
            if (r.batch_target == &t)
                r.end_batch();
            r.start_readback(t, memory);
        }
        r.finish_readback(t, memory);
    }
    // A block transfer only needs the targets it touches resolved. Submitting
    // here as well split every frame into dozens of submissions during gameplay.
    if (!transfer)
        r.submit();
    r.flush_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
            .count();
}
void NativeRenderer::flush_frame(psprecomp::GuestMemory &memory, std::uint32_t address, unsigned stride,
                                 unsigned format) {
    auto &r = *impl_;
    r.frame_memory = &memory;
    if (!r.deferred_readback) {
        flush(memory, nullptr);
        return;
    }
    platform::NativeCallScope phase("vulkan-flush");
    auto start = std::chrono::steady_clock::now();
    const std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> shown{address, stride, format};
    const std::uint64_t shown_begin = address & 0x0fffffffu,
                        shown_end = shown_begin + std::uint64_t(stride) * height * (format == 3 ? 4 : 2);
    for (auto &[key, t] : r.targets) {
        // Earlier frames arrive here once the GPU has finished them. A buffer
        // drawn again before its last copy arrived waits for that copy, which
        // also keeps the game from running further ahead of a slow GPU.
        if (t.fence && (t.dirty || SDL_QueryGPUFence(r.device, t.fence)))
            r.finish_readback(t, memory);
        if (t.dirty) {
            if (r.batch_target == &t)
                r.end_batch();
            r.start_readback(t, memory);
        }
        if (!t.fence || key == shown)
            continue;
        // The window shows the displayed buffer from its GPU copy. Any other
        // buffer sharing that memory is shown from guest memory, so its pixels
        // have to be there now.
        const std::uint64_t begin = t.guest.color_address & 0x0fffffffu,
                            end = begin + std::uint64_t(t.guest.color_stride) * height *
                                              (t.guest.color_format == 3 ? 4 : 2);
        if (begin < shown_end && shown_begin < end)
            r.finish_readback(t, memory);
    }
    r.submit();
    r.flush_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
            .count();
}
void NativeRenderer::draw(psprecomp::GuestMemory &memory, const DrawCall &call) {
    if (call.vertices.empty())
        return;
    if (call.stencil.test_enabled && !call.clear_mode)
        throw psprecomp::Error("PSP framebuffer-alpha stencil testing is not implemented in Vulkan");
    auto start = std::chrono::steady_clock::now();
    auto &r = *impl_;
    platform::NativeCallScope phase("vulkan-draw");
    if (call.texture.enabled && !call.clear_mode) {
        bool feedback = false;
        const auto a = call.texture.address & 0x0fffffffu;
        for (const auto &[key, t] : r.targets) {
            auto begin = t.guest.color_address & 0x0fffffffu;
            auto size = t.guest.color_stride * height * (t.guest.color_format == 3 ? 4 : 2);
            feedback |= (t.dirty || t.fence) && a >= begin && a - begin < size;
        }
        if (feedback)
            flush(memory);
    }
    auto &target = r.target(memory, call.target);
    bool framebuffer_texture = false;
    if (r.aspect_factor != 1 && call.texture.enabled) {
        const auto address = call.texture.address & 0x0fffffffu;
        for (const auto &[key, source] : r.targets) {
            const auto begin = source.guest.color_address & 0x0fffffffu;
            const auto size = source.guest.color_stride * height * (source.guest.color_format == 3 ? 4u : 2u);
            if (address >= begin && address - begin < size) {
                framebuffer_texture = true;
                break;
            }
        }
    }
    const auto approximately = [](float a, float b) { return std::abs(a - b) < 0.1f; };
    const auto &vp = call.viewport;
    const bool match_aspect =
        r.aspect_factor != 1 && !call.clear_mode && !framebuffer_texture &&
        approximately(std::abs(vp.x_scale), 240) && approximately(std::abs(vp.y_scale), 136) &&
        approximately(vp.x_offset - vp.offset_x, 240) && approximately(vp.y_offset - vp.offset_y, 136);
    const bool hud = call.through || std::abs(call.projection[15]) > 0.5f;

    auto &output = r.output_vertices;
    output.clear();
    r.converted_vertices.resize(call.vertices.size());
    r.converted_generation.resize(call.vertices.size());
    if (++r.conversion_generation == 0) {
        std::fill(r.converted_generation.begin(), r.converted_generation.end(), 0);
        ++r.conversion_generation;
    }
    std::optional<vertex_lighting::Lighting> lighting;
    if (call.lighting_enabled && !call.clear_mode) lighting.emplace(call);
    auto convert = [&](const Vertex &v) {
        GpuVertex out{};
        auto p = v.position;
        p[3] = 1;
        out.color = rgba(call.has_vertex_color || call.clear_mode ? v.color : call.material_color);
        out.extra[3] = 1;
        out.uv[2] = 1;
        if (call.through) {
            out.position = {p[0] * 2 / width - 1, 1 - p[1] * 2 / height, std::clamp(p[2] / 65535, 0.0f, 1.0f),
                            1};
            out.uv = {v.texcoord[0] / std::max(1u, unsigned(call.texture.width)),
                      v.texcoord[1] / std::max(1u, unsigned(call.texture.height)), 1};
        } else {
            const auto world = multiply(call.world, p), view = multiply(call.view, world),
                       clip = multiply(call.projection, view);
            if (lighting)
                lighting->apply(v, world, out.color, out.extra);
            const auto &vp = call.viewport;
            out.position = {
                ((vp.x_offset - vp.offset_x) * 2 / width - 1) * clip[3] + clip[0] * vp.x_scale * 2 / width,
                (1 - (vp.y_offset - vp.offset_y) * 2 / height) * clip[3] - clip[1] * vp.y_scale * 2 / height,
                (clip[2] * vp.z_scale + clip[3] * vp.z_offset) / 65535, clip[3]};
            out.uv = {v.texcoord[0] * call.texture.scale_u + call.texture.offset_u,
                      v.texcoord[1] * call.texture.scale_v + call.texture.offset_v, 1};
            if (call.texture.mapping_mode == 1u) {
                V4 source = p;
                if (call.texture.projection_source == 1u)
                    source = {v.texcoord[0], v.texcoord[1], 0, 1};
                if (call.texture.projection_source >= 2u) {
                    source = {v.normal[0], v.normal[1], v.normal[2], 1};
                    if (call.texture.projection_source == 2u) {
                        const auto length =
                            std::sqrt(source[0] * source[0] + source[1] * source[1] + source[2] * source[2]);
                        if (length > 0)
                            for (unsigned i = 0; i < 3; ++i)
                                source[i] /= length;
                    }
                }
                const auto stq = multiply(call.texture_matrix, source);
                out.uv = {stq[0], stq[1], stq[2]};
            }
            if (call.fog.enabled)
                out.extra[3] = (view[2] + call.fog.end) * call.fog.scale;
        }
        if (match_aspect) {
            constexpr float center = 480.0f / 512.0f - 1.0f;
            const float horizontal_factor = hud ? std::min(1.0f, r.aspect_factor) : r.aspect_factor;
            out.position[0] =
                (out.position[0] - center * out.position[3]) * horizontal_factor + center * out.position[3];
            if (hud)
                out.position[1] *= std::min(1.0f, 1 / r.aspect_factor);
        }
        return out;
    };
    const auto converted = [&](std::size_t i) {
        if (r.converted_generation[i] != r.conversion_generation) {
            r.converted_vertices[i] = convert(call.vertices[i]);
            r.converted_generation[i] = r.conversion_generation;
        }
        return r.converted_vertices[i];
    };
    // Convert large draws up front on several threads; the lazy path below
    // then finds every vertex done. Small draws keep converting on demand.
    if (call.vertices.size() >= ParallelVertices::kMinimumVertices)
        r.parallel.run(call.vertices.size(), [&](std::size_t begin, std::size_t end) {
            for (auto i = begin; i < end; ++i) {
                r.converted_vertices[i] = convert(call.vertices[i]);
                r.converted_generation[i] = r.conversion_generation;
            }
        });
    const auto count = call.indices.empty() ? call.vertices.size() : call.indices.size();
    const auto at = [&](std::size_t i) {
        return converted(primitive_vertex(call.indices, call.vertices.size(), i));
    };
    auto topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    if (call.primitive == PrimitiveType::Sprites) {
        for (std::size_t i = 0; i + 1 < count; i += 2) {
            auto a = at(i), b = at(i + 1);
            a.color = b.color;
            a.position[2] = b.position[2];
            for (unsigned j = 0; j < 3; ++j) {
                a.position[j] /= a.position[3];
                b.position[j] /= b.position[3];
            }
            a.position[3] = b.position[3] = 1;
            auto c = a, d = b;
            c.position[0] = b.position[0];
            c.uv[0] = b.uv[0];
            d.position[0] = a.position[0];
            d.uv[0] = a.uv[0];
            output.insert(output.end(), {a, c, b, a, b, d});
        }
    } else if (call.primitive == PrimitiveType::Points || call.primitive == PrimitiveType::Lines ||
               call.primitive == PrimitiveType::LineStrip) {
        topology = call.primitive == PrimitiveType::Points  ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                   : call.primitive == PrimitiveType::Lines ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                            : SDL_GPU_PRIMITIVETYPE_LINESTRIP;
        for (std::size_t i = 0; i < count; ++i)
            output.push_back(at(i));
    } else {
        auto &indices = r.triangle_vertices;
        triangle_indices(call.primitive, count, call.indices, call.vertices.size(), indices);
        output.reserve(indices.size());
        for (const auto i : indices)
            output.push_back(converted(i));
    }

    if (output.empty())
        return;
    Parameters params{};
    params.function = call.texture.function;
    params.texture_alpha = call.texture.alpha_from_texture;
    params.alpha_enabled = call.alpha_test.enabled && !call.clear_mode;
    params.alpha_function = call.alpha_test.function;
    params.alpha_reference = call.alpha_test.reference;
    params.alpha_mask = call.alpha_test.mask;
    params.fog = rgba(call.fog.color);
    params.environment = rgba(call.texture.environment_color);
    params.double_color = call.texture.double_color;
    SDL_GPUTexture *image = r.white;
    if (call.texture.enabled && !call.clear_mode) {
        const auto key = texture_key(memory, call.texture);
        auto it = r.textures.find(key);
        if (it == r.textures.end()) {
            std::vector<std::uint32_t> pixels;
            require(decode_texture(memory, call.texture, pixels), "Decode GE texture");
            image = r.texture(call.texture.width, call.texture.height, SDL_GPU_TEXTUREUSAGE_SAMPLER);
            r.upload_texture(image, pixels.data(), call.texture.width, call.texture.height);
            if (r.textures.size() > 2048) {
                r.submit();
                require(platform::native_call("SDL_WaitForGPUIdle", [&] { return SDL_WaitForGPUIdle(r.device); }), "Wait Vulkan texture cache idle");
                for (auto &[k, t] : r.textures)
                    SDL_ReleaseGPUTexture(r.device, t);
                r.textures.clear();
            }
            r.textures[key] = image;
        } else
            image = it->second;
        params.textured = 1;
    }
    const unsigned aniso = r.anisotropy && !call.through && std::abs(call.projection[15]) < .5f &&
                                   (call.texture.min_filter & 1) && (call.texture.mag_filter & 1)
                               ? r.anisotropy
                               : 0;
    const unsigned sampler_key = (call.texture.min_filter & 1) | ((call.texture.mag_filter & 1) << 1) |
                                 (call.texture.wrap_s << 2) | (call.texture.wrap_t << 3) | (aniso << 8);
    auto &sampler = r.samplers[sampler_key];
    if (!sampler) {
        SDL_GPUSamplerCreateInfo i{};
        i.min_filter = (call.texture.min_filter & 1) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        i.mag_filter = (call.texture.mag_filter & 1) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        i.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        i.address_mode_u = call.texture.wrap_s ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                               : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        i.address_mode_v = call.texture.wrap_t ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                               : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        i.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        i.enable_anisotropy = aniso > 0;
        i.max_anisotropy = float(std::max(aniso, 1u));
        i.max_lod = 0;
        sampler = platform::native_call("SDL_CreateGPUSampler", [&] { return SDL_CreateGPUSampler(r.device, &i); });
        require(sampler, "Create Vulkan sampler");
    }
    SDL_GPUColorTargetBlendState blend{};
    blend.src_color_blendfactor = blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    blend.color_blend_op = blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    unsigned mask =
        call.clear_mode ? ((call.clear_flags & 1) ? 7u : 0u) | ((call.clear_flags & 2) ? 8u : 0u) : 15u;
    if (!call.clear_mode)
        for (unsigned channel = 0; channel < 4; ++channel) {
            auto m = channel == 3 ? call.stencil.alpha_write_mask & 255u
                                  : (call.stencil.color_write_mask >> (channel * 8)) & 255u;
            if (m == 255)
                mask &= ~(1u << channel);
            else if (m)
                throw psprecomp::Error("Partial-bit PSP framebuffer write masks are not implemented");
        }
    blend.enable_color_write_mask = true;
    blend.color_write_mask = SDL_GPUColorComponentFlags(mask);
    SDL_FColor constant{};
    if (call.blend.enabled && !call.clear_mode) {
        blend.enable_blend = true;
        auto factor = [](unsigned f, unsigned fixed, bool source) {
            const SDL_GPUBlendFactor fs[]{
                source ? SDL_GPU_BLENDFACTOR_DST_COLOR : SDL_GPU_BLENDFACTOR_SRC_COLOR,
                source ? SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
                SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                SDL_GPU_BLENDFACTOR_DST_ALPHA,
                SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
                SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                SDL_GPU_BLENDFACTOR_DST_ALPHA,
                SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA};
            return f < 10 ? fs[f]
                          : (fixed == 0          ? SDL_GPU_BLENDFACTOR_ZERO
                             : fixed == 0xffffff ? SDL_GPU_BLENDFACTOR_ONE
                                                 : SDL_GPU_BLENDFACTOR_CONSTANT_COLOR);
        };
        blend.src_color_blendfactor = factor(call.blend.source_factor, call.blend.fixed_source, true);
        blend.dst_color_blendfactor =
            factor(call.blend.destination_factor, call.blend.fixed_destination, false);
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        const SDL_GPUBlendOp ops[]{
            SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_SUBTRACT, SDL_GPU_BLENDOP_REVERSE_SUBTRACT,
            SDL_GPU_BLENDOP_MIN, SDL_GPU_BLENDOP_MAX,      SDL_GPU_BLENDOP_ADD};
        if (call.blend.equation > 4)
            throw psprecomp::Error("Unsupported GE blend equation");
        blend.color_blend_op = ops[call.blend.equation];
        auto fixed =
            rgba((call.blend.source_factor == 10 ? call.blend.fixed_source : call.blend.fixed_destination) |
                 0xff000000u);
        constant = {fixed[0], fixed[1], fixed[2], fixed[3]};
    }
    const SDL_GPUCompareOp comparisons[]{SDL_GPU_COMPAREOP_NEVER,   SDL_GPU_COMPAREOP_ALWAYS,
                                         SDL_GPU_COMPAREOP_EQUAL,   SDL_GPU_COMPAREOP_NOT_EQUAL,
                                         SDL_GPU_COMPAREOP_LESS,    SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
                                         SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_GREATER_OR_EQUAL};
    const bool depth_test = call.depth.test_enabled || call.clear_mode,
               depth_write = call.clear_mode ? (call.clear_flags & 4) != 0
                                             : call.depth.write_enabled && call.depth.test_enabled;
    const bool cull = call.culling_enabled && !call.through && !call.clear_mode;
    std::array<unsigned, 12> key{unsigned(topology),
                                 unsigned(cull),
                                 unsigned(call.cull_clockwise),
                                 unsigned(depth_test),
                                 unsigned(depth_write),
                                 call.clear_mode ? 1u : call.depth.function,
                                 mask,
                                 unsigned(blend.enable_blend),
                                 unsigned(blend.src_color_blendfactor),
                                 unsigned(blend.dst_color_blendfactor),
                                 unsigned(blend.color_blend_op),
                                 0};
    auto &pipeline = r.pipelines[key];
    platform::set_native_call_detail(key.data(), key.size());
    if (!pipeline) {
        SDL_GPUGraphicsPipelineCreateInfo i{};
        i.vertex_shader = r.vs;
        i.fragment_shader = r.ps;
        i.primitive_type = topology;
        SDL_GPUVertexBufferDescription buffer{0, sizeof(GpuVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        const SDL_GPUVertexAttribute attributes[]{
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, uv)},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, color)},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, extra)}};
        i.vertex_input_state = {&buffer, 1, attributes, 4};
        i.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        i.rasterizer_state.cull_mode = cull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        i.rasterizer_state.front_face =
            call.cull_clockwise ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        i.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        i.depth_stencil_state.enable_depth_test = depth_test;
        i.depth_stencil_state.enable_depth_write = depth_write;
        i.depth_stencil_state.compare_op = comparisons[key[5] & 7];
        SDL_GPUColorTargetDescription color{};
        color.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        color.blend_state = blend;
        i.target_info.color_target_descriptions = &color;
        i.target_info.num_color_targets = 1;
        i.target_info.has_depth_stencil_target = true;
        i.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        pipeline = platform::native_call("SDL_CreateGPUGraphicsPipeline", [&] { return SDL_CreateGPUGraphicsPipeline(r.device, &i); });
        require(pipeline, "Create Vulkan GE pipeline");
    }
    unsigned bytes = unsigned(output.size() * sizeof(GpuVertex));
    if (bytes > r.vertex_capacity)
        throw psprecomp::Error("Vulkan GE draw exceeds vertex buffer");
    if (r.vertex_cursor + bytes > r.vertex_capacity) {
        r.submit();
        r.vertex_cursor = 0;
    }
    const auto horizontal = [&](float x) {
        return match_aspect && hud ? (x - 240) * std::min(1.f, r.aspect_factor) + 240 : x;
    };
    const auto vertical = [&](float y) {
        return match_aspect && hud ? (y - 136) * std::min(1.f, 1 / r.aspect_factor) + 136 : y;
    };
    int x1 = std::clamp(int(std::floor(horizontal(float(call.viewport.scissor_x1)) * r.scale)), 0,
                        int(r.scaled_width()));
    int y1 = std::clamp(int(std::floor(vertical(float(call.viewport.scissor_y1)) * r.scale)), 0,
                        int(r.scaled_height()));
    int x2 = std::clamp(int(std::ceil(horizontal(float(call.viewport.scissor_x2 + 1)) * r.scale)), x1,
                        int(r.scaled_width()));
    int y2 = std::clamp(int(std::ceil(vertical(float(call.viewport.scissor_y2 + 1)) * r.scale)), y1,
                        int(r.scaled_height()));
    SDL_Rect scissor{x1, y1, x2 - x1, y2 - y1};
    if (r.batch_target != &target)
        r.end_batch();
    if (r.batch.empty())
        r.batch_start = r.vertex_cursor;
    r.batch_target = &target;
    r.batch_vertices.insert(r.batch_vertices.end(), output.begin(), output.end());
    r.batch.push_back({pipeline, image, sampler, scissor, constant, params,
                       unsigned(r.vertex_cursor / sizeof(GpuVertex)), unsigned(output.size())});
    if (!r.batching)
        r.end_batch();
    r.vertex_cursor += bytes;
    target.dirty = true;
    target.presentation_ready=false;
    ++r.count;
    // Start the GPU on this frame's work while the game records the rest.
    // The staging arena and vertex buffer keep filling across submissions,
    // so this allocates nothing.
    if (r.submit_draws && ++r.draws_since_submit >= r.submit_draws)
        r.submit();
    r.draw_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
            .count();
}
} // namespace sims2::gpu
