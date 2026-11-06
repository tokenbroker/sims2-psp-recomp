#include "gpu/native_renderer.hpp"
#include "gpu/texture_decode.hpp"
#include "gpu/triangle_indices.hpp"
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
#include <set>
#include <tuple>
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
// Per-vertex PSP lighting, adapted from Bigmission/Yakumo's MIT ge.vert.
void light_vertex(const DrawCall &call, const Vertex &v, const V4 &world, V4 &color, V4 &extra) {
    const auto &l = call.lighting;
    const auto vertex = rgba(v.color);
    const auto ambient =
        call.has_vertex_color && (l.material_update & 1u) ? vertex : rgba(call.material_color);
    const auto diffuse =
        call.has_vertex_color && (l.material_update & 2u) ? vertex : rgba(l.material_diffuse);
    const auto specular =
        call.has_vertex_color && (l.material_update & 4u) ? vertex : rgba(l.material_specular);
    const auto global = rgba(l.ambient_color), emissive = rgba(l.material_emissive);
    const auto normalize = [](V4 vector) {
        const auto length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        if (length > 0)
            for (unsigned i = 0; i < 3; ++i)
                vector[i] /= length;
        else
            vector = {0, 0, 1, 0};
        return vector;
    };
    const auto dot = [](const V4 &a, const V4 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto normal = normalize(multiply(call.world, {v.normal[0], v.normal[1], v.normal[2], 0}));
    if (l.reverse_normals)
        for (unsigned i = 0; i < 3; ++i)
            normal[i] = -normal[i];
    V4 sum{}, shine{};
    for (unsigned i = 0; i < 3; ++i)
        sum[i] = emissive[i] + global[i] * ambient[i];
    for (const auto &source : l.lights) {
        if (!source.enabled)
            continue;
        V4 direction{source.position[0], source.position[1], source.position[2], 0};
        float attenuation = 1;
        if (source.type != 0) {
            for (unsigned i = 0; i < 3; ++i)
                direction[i] -= world[i];
            const auto distance = std::sqrt(dot(direction, direction));
            const auto &k = source.attenuation;
            attenuation = std::clamp(
                1.0f / std::max(k[0] + k[1] * distance + k[2] * distance * distance, 1e-20f), 0.0f, 1.0f);
        }
        direction = normalize(direction);
        if (source.type == 2) {
            const auto axis = normalize({source.direction[0], source.direction[1], source.direction[2], 0});
            const auto angle = -dot(axis, direction);
            attenuation *=
                angle >= source.spot_cutoff ? std::pow(std::max(angle, 0.0f), source.spot_exponent) : 0;
        }
        const auto ndotl = dot(normal, direction);
        auto lambert = std::max(ndotl, 0.0f);
        if (source.kind == 2)
            lambert = std::pow(lambert, l.specular_power);
        const auto la = rgba(source.ambient), ld = rgba(source.diffuse), ls = rgba(source.specular);
        auto half = direction;
        half[2] += 1;
        half = normalize(half);
        const auto highlight = source.kind == 1 && ndotl >= 0
                                   ? std::pow(std::max(dot(normal, half), 0.0f), l.specular_power)
                                   : 0;
        for (unsigned i = 0; i < 3; ++i) {
            sum[i] += (la[i] * ambient[i] + ld[i] * diffuse[i] * lambert) * attenuation;
            shine[i] += ls[i] * specular[i] * highlight * attenuation;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        color[i] = std::clamp(sum[i] + (l.mode == 1 ? 0 : shine[i]), 0.0f, 1.0f);
        extra[i] = l.mode == 1 ? std::clamp(shine[i], 0.0f, 1.0f) : 0;
    }
    color[3] = ambient[3] * float(l.ambient_alpha) / 255.0f;
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
    std::uint64_t count{}, flush_count{}, draw_ns{}, flush_ns{}, vertex_ns{};
    float scale{1}, aspect_factor{1};
    std::vector<GpuVertex> output_vertices, converted_vertices;
    std::vector<std::uint32_t> converted_generation;
    std::vector<std::uint16_t> triangle_vertices;
    std::uint32_t conversion_generation{};
    std::vector<SDL_GPUTransferBuffer *> pending_uploads;
    struct Target {
        RenderTarget guest;
        bool dirty{}, upload{true};
        std::vector<std::uint8_t> snapshot;
        SDL_GPUTexture *color{}, *depth{}, *native{};
    };
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, Target> targets;
    std::map<std::uint64_t, SDL_GPUTexture *> textures;
    std::map<unsigned, SDL_GPUSampler *> samplers;
    std::map<std::array<unsigned, 12>, SDL_GPUGraphicsPipeline *> pipelines;
    SDL_GPUTexture *white{};
    unsigned scaled_width() const { return std::max(1u, unsigned(std::lround(width * scale))); }
    unsigned scaled_height() const { return std::max(1u, unsigned(std::lround(height * scale))); }
    SDL_GPUCommandBuffer *cmd() {
        if (!commands) {
            commands = SDL_AcquireGPUCommandBuffer(device);
            require(commands, "Acquire Vulkan commands");
        }
        return commands;
    }
    void submit() {
        if (commands) {
            require(SDL_SubmitGPUCommandBuffer(commands), "Submit Vulkan commands");
            commands = nullptr;
        }
        for (auto *p : pending_uploads)
            SDL_ReleaseGPUTransferBuffer(device, p);
        pending_uploads.clear();
        vertex_cursor = 0;
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
        auto *t = SDL_CreateGPUTexture(device, &info);
        require(t, "Create Vulkan texture");
        return t;
    }
    SDL_GPUTransferBuffer *upload(const void *data, unsigned bytes) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = bytes;
        auto *t = SDL_CreateGPUTransferBuffer(device, &info);
        require(t, "Create Vulkan upload");
        pending_uploads.push_back(t);
        auto *mapped = SDL_MapGPUTransferBuffer(device, t, false);
        require(mapped, "Map Vulkan upload");
        std::memcpy(mapped, data, bytes);
        SDL_UnmapGPUTransferBuffer(device, t);
        return t;
    }
    void upload_texture(SDL_GPUTexture *t, const std::uint32_t *data, unsigned w, unsigned h) {
        auto *transfer = upload(data, w * h * 4);
        auto *copy = SDL_BeginGPUCopyPass(cmd());
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = transfer;
        src.pixels_per_row = w;
        src.rows_per_layer = h;
        SDL_GPUTextureRegion dst{};
        dst.texture = t;
        dst.w = w;
        dst.h = h;
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
        SDL_EndGPUCopyPass(copy);
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
        SDL_BlitGPUTexture(cmd(), &info);
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
            auto *pass = SDL_BeginGPURenderPass(cmd(), nullptr, 0, &z);
            require(pass, "Clear Vulkan depth");
            SDL_EndGPURenderPass(pass);
        }
        if (out.upload) {
            const auto size = std::size_t(guest.color_stride) * height * (guest.color_format == 3 ? 4 : 2);
            const auto *raw = memory.raw_pointer(guest.color_address, size);
            if (raw && out.snapshot.size() == size && !std::memcmp(raw, out.snapshot.data(), size)) {
                out.upload = false;
                return out;
            }
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
        }
        return out;
    }
    void clear_targets() {
        submit();
        SDL_WaitForGPUIdle(device);
        for (auto &[key, t] : targets) {
            SDL_ReleaseGPUTexture(device, t.color);
            SDL_ReleaseGPUTexture(device, t.depth);
            SDL_ReleaseGPUTexture(device, t.native);
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
        if(commands){SDL_CancelGPUCommandBuffer(commands);commands=nullptr;}
        SDL_WaitForGPUIdle(device);
        clear_targets();
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
        if (vs)
            SDL_ReleaseGPUShader(device, vs);
        if (ps)
            SDL_ReleaseGPUShader(device, ps);
        SDL_DestroyGPUDevice(device);
        if (video)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
NativeRenderer::NativeRenderer(bool) : impl_(std::make_unique<Impl>()) {
    auto &r = *impl_;
    SDL_SetAppMetadata("The Sims 2 PSP Recomp", "0.2.3", "io.github.sims2psprecomp");
    require(SDL_InitSubSystem(SDL_INIT_VIDEO), "Initialize SDL Vulkan video");
    r.video = true;
    r.device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, std::getenv("SIMS2_GPU_DEBUG") != nullptr, "vulkan");
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
        auto *s = SDL_CreateGPUShader(r.device, &info);
        require(s, "Create Vulkan shader");
        return s;
    };
    r.vs = shader(shaders::vertex, SDL_GPU_SHADERSTAGE_VERTEX);
    r.ps = shader(shaders::fragment, SDL_GPU_SHADERSTAGE_FRAGMENT);
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = r.vertex_capacity;
    r.vertices = SDL_CreateGPUBuffer(r.device, &info);
    require(r.vertices, "Create Vulkan vertex buffer");
    r.white = r.texture(1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    const std::uint32_t white = 0xffffffff;
    r.upload_texture(r.white, &white, 1, 1);
    r.submit();
    std::cerr << "[graphics] Vulkan (SDL GPU) adapter: " << SDL_GetStringProperty(SDL_GetGPUDeviceProperties(r.device),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown") << "\n";
}
NativeRenderer::~NativeRenderer() = default;
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
    const auto &r = *impl_;
    auto it = r.targets.find({address, stride, format});
    if (it == r.targets.end())
        return nullptr;
    const auto &t = it->second;
    if (t.dirty)
        return nullptr;
    const auto size = std::size_t(stride) * height * (format == 3 ? 4 : 2);
    auto *raw = memory.raw_pointer(address, size);
    return raw && t.snapshot.size() == size && !std::memcmp(raw, t.snapshot.data(), size) ? t.color : nullptr;
}
std::uint64_t NativeRenderer::draws() const noexcept { return impl_->count; }
std::uint64_t NativeRenderer::framebuffer_readbacks() const noexcept { return impl_->flush_count; }
NativeRenderer::ProfileStats NativeRenderer::profile_stats() const noexcept {
    return {impl_->draw_ns, impl_->flush_ns, impl_->vertex_ns};
}
void NativeRenderer::set_render_scale(float scale) {
    if (!std::isfinite(scale) || scale < .5f || scale > 16)
        throw psprecomp::Error("Render scale must be between 0.5 and 16");
    if (scale != impl_->scale) {
        for (const auto &[key, t] : impl_->targets)
            if (t.dirty)
                throw psprecomp::Error("Flush framebuffers before changing render scale");
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
    auto start = std::chrono::steady_clock::now();
    for (auto &[key, t] : r.targets) {
        if (!t.dirty)
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
        r.blit(t.color, t.native, r.scaled_width(), r.scaled_height(), width, height);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = width * height * 4;
        auto *download = SDL_CreateGPUTransferBuffer(r.device, &info);
        require(download, "Create Vulkan readback");
        auto *pass = SDL_BeginGPUCopyPass(r.cmd());
        SDL_GPUTextureRegion src{};
        src.texture = t.native;
        src.w = width;
        src.h = height;
        src.d = 1;
        SDL_GPUTextureTransferInfo dst{};
        dst.transfer_buffer = download;
        dst.pixels_per_row = width;
        dst.rows_per_layer = height;
        SDL_DownloadFromGPUTexture(pass, &src, &dst);
        SDL_EndGPUCopyPass(pass);
        auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(r.commands);
        r.commands = nullptr;
        require(fence, "Submit Vulkan readback");
        require(SDL_WaitForGPUFences(r.device, true, &fence, 1), "Wait Vulkan readback");
        SDL_ReleaseGPUFence(r.device, fence);
        auto *pixels =
            static_cast<const std::uint32_t *>(SDL_MapGPUTransferBuffer(r.device, download, false));
        require(pixels, "Map Vulkan readback");
        const auto &g = t.guest;
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < std::min(width, g.color_stride); ++x) {
                auto at = g.color_address + (y * g.color_stride + x) * (g.color_format == 3 ? 4 : 2);
                if (g.color_format == 3)
                    memory.store32(at, pixels[y * width + x]);
                else
                    memory.store16(at, pack(pixels[y * width + x], g.color_format));
            }
        SDL_UnmapGPUTransferBuffer(r.device, download);
        SDL_ReleaseGPUTransferBuffer(r.device, download);
        ++r.flush_count;
        t.dirty = false;
        t.upload = true;
        const auto size = std::size_t(g.color_stride) * height * (g.color_format == 3 ? 4 : 2);
        auto *raw = memory.raw_pointer(g.color_address, size);
        if (raw)
            t.snapshot.assign(raw, raw + size);
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
    if (call.texture.enabled && !call.clear_mode) {
        bool feedback = false;
        const auto a = call.texture.address & 0x0fffffffu;
        for (const auto &[key, t] : r.targets) {
            auto begin = t.guest.color_address & 0x0fffffffu;
            auto size = t.guest.color_stride * height * (t.guest.color_format == 3 ? 4 : 2);
            feedback |= t.dirty && a >= begin && a - begin < size;
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
            if (call.lighting_enabled && !call.clear_mode)
                light_vertex(call, v, world, out.color, out.extra);
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
                SDL_WaitForGPUIdle(r.device);
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
        sampler = SDL_CreateGPUSampler(r.device, &i);
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
        pipeline = SDL_CreateGPUGraphicsPipeline(r.device, &i);
        require(pipeline, "Create Vulkan GE pipeline");
    }
    unsigned bytes = unsigned(output.size() * sizeof(GpuVertex));
    if (bytes > r.vertex_capacity)
        throw psprecomp::Error("Vulkan GE draw exceeds vertex buffer");
    if (r.vertex_cursor + bytes > r.vertex_capacity)
        r.submit();
    auto *transfer = r.upload(output.data(), bytes);
    auto *copy = SDL_BeginGPUCopyPass(r.cmd());
    SDL_GPUTransferBufferLocation src{transfer, 0};
    SDL_GPUBufferRegion dst{r.vertices, r.vertex_cursor, bytes};
    SDL_UploadToGPUBuffer(copy, &src, &dst, r.vertex_cursor == 0);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo color{};
    color.texture = target.color;
    color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = target.depth;
    depth.load_op = SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    auto *pass = SDL_BeginGPURenderPass(r.cmd(), &color, 1, &depth);
    require(pass, "Begin Vulkan GE pass");
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUViewport viewport{0, 0, float(r.scaled_width()), float(r.scaled_height()), 0, 1};
    SDL_SetGPUViewport(pass, &viewport);
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
    SDL_SetGPUScissor(pass, &scissor);
    SDL_SetGPUBlendConstants(pass, constant);
    SDL_GPUBufferBinding binding{r.vertices, r.vertex_cursor};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    SDL_GPUTextureSamplerBinding sample{image, sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &sample, 1);
    SDL_PushGPUFragmentUniformData(r.cmd(), 0, &params, sizeof(params));
    SDL_DrawGPUPrimitives(pass, unsigned(output.size()), 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    r.vertex_cursor += bytes;
    target.dirty = true;
    ++r.count;
    r.draw_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
            .count();
}
} // namespace sims2::gpu
