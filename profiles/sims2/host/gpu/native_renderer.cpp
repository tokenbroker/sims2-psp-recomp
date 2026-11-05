#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "gpu/native_renderer.hpp"
#include "gpu/texture_decode.hpp"
#include "gpu/triangle_indices.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <set>
#include <bit>

namespace sims2::gpu {
using Microsoft::WRL::ComPtr;
namespace {
struct ProfileScope {
    std::uint64_t &total;
    bool enabled;
    std::chrono::steady_clock::time_point start;
    ProfileScope(std::uint64_t &value, bool active) : total(value), enabled(active) {
        if (enabled) start = std::chrono::steady_clock::now();
    }
    void stop() {
        if (enabled) {
            total += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            enabled = false;
        }
    }
    ~ProfileScope() { stop(); }
};
constexpr unsigned width = 512, height = 272;
void check(HRESULT result, const char *operation) {
    if (FAILED(result)) throw psprecomp::Error(std::string(operation) + ": " + psprecomp::hex32(static_cast<std::uint32_t>(result)));
}
using V4 = std::array<float, 4>;
V4 multiply(const std::array<float, 16> &matrix, const V4 &v) {
    V4 out{}; for (unsigned row = 0; row < 4; ++row) for (unsigned col = 0; col < 4; ++col) out[row] += matrix[col * 4 + row] * v[col];
    return out;
}
V4 rgba(std::uint32_t color) { return {float(color & 255) / 255, float((color >> 8) & 255) / 255, float((color >> 16) & 255) / 255, float(color >> 24) / 255}; }
// Per-vertex PSP lighting, adapted from Bigmission/Yakumo's MIT ge.vert.
void light_vertex(const DrawCall &call, const Vertex &v, const V4 &world, V4 &color, V4 &extra) {
    const auto &l = call.lighting;
    const auto vertex = rgba(v.color);
    const auto ambient = call.has_vertex_color && (l.material_update & 1u) ? vertex : rgba(call.material_color);
    const auto diffuse = call.has_vertex_color && (l.material_update & 2u) ? vertex : rgba(l.material_diffuse);
    const auto specular = call.has_vertex_color && (l.material_update & 4u) ? vertex : rgba(l.material_specular);
    const auto global = rgba(l.ambient_color), emissive = rgba(l.material_emissive);
    const auto normalize = [](V4 vector) {
        const auto length = std::sqrt(vector[0]*vector[0]+vector[1]*vector[1]+vector[2]*vector[2]);
        if (length > 0) for (unsigned i=0;i<3;++i) vector[i] /= length;
        else vector = {0,0,1,0};
        return vector;
    };
    const auto dot = [](const V4 &a, const V4 &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; };
    auto normal = normalize(multiply(call.world, {v.normal[0],v.normal[1],v.normal[2],0}));
    if (l.reverse_normals) for (unsigned i=0;i<3;++i) normal[i] = -normal[i];
    V4 sum{}, shine{};
    for (unsigned i=0;i<3;++i) sum[i] = emissive[i]+global[i]*ambient[i];
    for (const auto &source : l.lights) {
        if (!source.enabled) continue;
        V4 direction{source.position[0],source.position[1],source.position[2],0}; float attenuation = 1;
        if (source.type != 0) {
            for (unsigned i=0;i<3;++i) direction[i] -= world[i];
            const auto distance = std::sqrt(dot(direction,direction)); const auto &k = source.attenuation;
            attenuation = std::clamp(1.0f/std::max(k[0]+k[1]*distance+k[2]*distance*distance, 1e-20f),0.0f,1.0f);
        }
        direction = normalize(direction);
        if (source.type == 2) {
            const auto axis = normalize({source.direction[0],source.direction[1],source.direction[2],0});
            const auto angle = -dot(axis,direction);
            attenuation *= angle >= source.spot_cutoff ? std::pow(std::max(angle,0.0f),source.spot_exponent) : 0;
        }
        const auto ndotl = dot(normal,direction); auto lambert = std::max(ndotl,0.0f);
        if (source.kind == 2) lambert = std::pow(lambert,l.specular_power);
        const auto la=rgba(source.ambient), ld=rgba(source.diffuse), ls=rgba(source.specular);
        auto half = direction; half[2] += 1; half = normalize(half);
        const auto highlight = source.kind == 1 && ndotl >= 0 ? std::pow(std::max(dot(normal,half),0.0f),l.specular_power) : 0;
        for (unsigned i=0;i<3;++i) {
            sum[i] += (la[i]*ambient[i]+ld[i]*diffuse[i]*lambert)*attenuation;
            shine[i] += ls[i]*specular[i]*highlight*attenuation;
        }
    }
    for (unsigned i=0;i<3;++i) {
        color[i] = std::clamp(sum[i]+(l.mode == 1 ? 0 : shine[i]),0.0f,1.0f);
        extra[i] = l.mode == 1 ? std::clamp(shine[i],0.0f,1.0f) : 0;
    }
    color[3] = ambient[3]*float(l.ambient_alpha)/255.0f;
}
std::uint32_t expand(std::uint32_t raw, unsigned format) {
    if (format == 3) return raw;
    unsigned r{}, g{}, b{}, a{};
    if (format == 0) { r = (raw & 31) * 255 / 31; g = ((raw >> 5) & 63) * 255 / 63; b = ((raw >> 11) & 31) * 255 / 31; a = 255; }
    else if (format == 1) { r = (raw & 31) * 255 / 31; g = ((raw >> 5) & 31) * 255 / 31; b = ((raw >> 10) & 31) * 255 / 31; a = (raw & 0x8000) ? 255 : 0; }
    else { r = (raw & 15) * 17; g = ((raw >> 4) & 15) * 17; b = ((raw >> 8) & 15) * 17; a = ((raw >> 12) & 15) * 17; }
    return r | (g << 8) | (b << 16) | (a << 24);
}
std::uint16_t pack(std::uint32_t raw, unsigned format) {
    const auto r = raw & 255u, g = (raw >> 8) & 255u, b = (raw >> 16) & 255u, a = raw >> 24;
    if (format == 0) return static_cast<std::uint16_t>((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11));
    if (format == 1) return static_cast<std::uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >> 7) << 15));
    return static_cast<std::uint16_t>((r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12));
}
D3D11_COMPARISON_FUNC comparison(unsigned function) {
    constexpr D3D11_COMPARISON_FUNC funcs[]{D3D11_COMPARISON_NEVER, D3D11_COMPARISON_ALWAYS,
        D3D11_COMPARISON_EQUAL, D3D11_COMPARISON_NOT_EQUAL, D3D11_COMPARISON_LESS,
        D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_GREATER, D3D11_COMPARISON_GREATER_EQUAL};
    return funcs[function & 7u];
}
struct GpuVertex { V4 position; std::array<float, 3> uv; V4 color; V4 extra; };
struct Parameters { std::uint32_t textured, function, texture_alpha, alpha_enabled;
    std::uint32_t alpha_function, alpha_reference, alpha_mask, double_color;
    V4 environment; V4 fog; };
constexpr const char *shader = R"(
struct Vertex { float4 p:POSITION; float3 uv:TEXCOORD; float4 color:COLOR; float4 extra:TEXCOORD1; };
struct Fragment { float4 p:SV_Position; float3 uv:TEXCOORD; float4 color:COLOR; float4 extra:TEXCOORD1; };
Fragment vs(Vertex v) { Fragment f; f.p=v.p; f.uv=v.uv; f.color=v.color; f.extra=v.extra; return f; }
cbuffer Params : register(b0) { uint textured, function, texture_alpha, alpha_enabled;
uint alpha_function, alpha_reference, alpha_mask, double_color; float4 environment; float4 fog; };
Texture2D image:register(t0); SamplerState sampler0:register(s0);
float4 ps(Fragment f):SV_Target {
    float4 c=f.color;
    if (textured) {
        uint tw,th; image.GetDimensions(tw,th);
        float2 uv=f.uv.z>0 ? f.uv.xy/f.uv.z : 1.0-0.5/float2(tw,th);
        float4 t=image.Sample(sampler0,uv); float a=texture_alpha ? c.a*t.a:c.a;
        if (function==0) c=float4(c.rgb*t.rgb,a);
        else if (function==1) c=float4(texture_alpha?lerp(c.rgb,t.rgb,t.a):t.rgb,c.a);
        else if (function==2) c=float4(lerp(c.rgb,environment.rgb,t.rgb),a);
        else if (function==3) c=float4(t.rgb,texture_alpha?t.a:c.a);
        else c=float4(c.rgb+t.rgb,a);
        if (double_color) c.rgb*=2;
    }
    c.rgb=saturate(c.rgb+f.extra.rgb);
    c.rgb=lerp(fog.rgb,c.rgb,saturate(f.extra.w));
    if (alpha_enabled) {
        uint a=(uint(round(saturate(c.a)*255)))&alpha_mask, r=alpha_reference&alpha_mask;
        bool accepted=alpha_function==1 || (alpha_function==2 && a==r) || (alpha_function==3 && a!=r) ||
            (alpha_function==4 && a<r) || (alpha_function==5 && a<=r) || (alpha_function==6 && a>r) || (alpha_function==7 && a>=r);
        if (!accepted) discard;
    }
    return saturate(c);
})";
}
constexpr const char *copy_shader = R"(
Texture2D image:register(t0); SamplerState image_sampler:register(s0);
struct F { float4 p:SV_Position; float2 uv:TEXCOORD0; };
F vs(uint id:SV_VertexID) { F f; f.uv=float2((id<<1)&2,id&2); f.p=float4(f.uv.x*2-1,1-f.uv.y*2,0,1);return f; }
float4 ps(F f):SV_Target { return image.Sample(image_sampler,f.uv); }
)";
struct NativeRenderer::Impl {
    bool profile = std::getenv("SIMS2_PROFILE") != nullptr || std::getenv("SIMS2_FRAME_CSV") != nullptr;
    std::uint64_t draw_ns{}, vertex_ns{}, hash_ns{}, upload_ns{}, flush_ns{}, texture_misses{}, flush_count{};
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout; ComPtr<ID3D11Buffer> vertices, parameters;
    float scale{1};
    unsigned anisotropy{};
    unsigned scaled_width()const{return std::max(1u,unsigned(std::lround(width*scale)));}
    unsigned scaled_height()const{return std::max(1u,unsigned(std::lround(height*scale)));}
    float aspect_factor{1};
    bool trace_projection=std::getenv("SIMS2_TRACE_PROJECTION")!=nullptr;
    std::set<std::array<std::uint32_t,22>> projection_signatures;
    ComPtr<ID3D11VertexShader> copy_vs;
    ComPtr<ID3D11PixelShader> copy_ps;
    ComPtr<ID3D11SamplerState> copy_sampler;
    ComPtr<ID3D11RasterizerState> copy_raster;
    void copy(ID3D11ShaderResourceView *source, ID3D11RenderTargetView *destination, unsigned w, unsigned h) {
        context->OMSetRenderTargets(1,&destination,nullptr);
        context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context->OMSetDepthStencilState(nullptr,0);
        context->RSSetState(copy_raster.Get());
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(copy_vs.Get(),nullptr,0);context->PSSetShader(copy_ps.Get(),nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);
        context->PSSetShaderResources(0,1,&source);auto *sampler=copy_sampler.Get();context->PSSetSamplers(0,1,&sampler);
        context->Draw(3,0);
        ID3D11ShaderResourceView *none{};context->PSSetShaderResources(0,1,&none);
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    unsigned vertex_capacity{}, vertex_cursor{}; std::uint64_t count{};
    std::vector<GpuVertex> output_vertices, converted_vertices;
    std::vector<std::uint32_t> converted_generation;
    std::vector<std::uint16_t> triangle_vertices;
    std::uint32_t conversion_generation{};
    struct Target {
        RenderTarget guest; bool dirty{}, upload{true};
        std::vector<std::uint8_t> snapshot;
        ComPtr<ID3D11Texture2D> color, readback, depth, native_color;
        ComPtr<ID3D11RenderTargetView> native_rtv;
        ComPtr<ID3D11ShaderResourceView> native_srv;
        ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11DepthStencilView> dsv;
    };
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, Target> targets;
    std::map<std::uint64_t, ComPtr<ID3D11ShaderResourceView>> textures;
    std::map<unsigned, ComPtr<ID3D11SamplerState>> samplers;
    std::map<unsigned, ComPtr<ID3D11RasterizerState>> raster_states;
    std::map<unsigned, ComPtr<ID3D11DepthStencilState>> depth_states;
    std::map<unsigned, ComPtr<ID3D11BlendState>> blend_states;
    Target &target(psprecomp::GuestMemory &memory, const RenderTarget &guest) {
        if (guest.color_stride < 1 || guest.color_stride > 2048 || guest.color_format > 3 ||
            !memory.contains(guest.color_address, static_cast<std::size_t>(guest.color_stride) * height * (guest.color_format == 3 ? 4u : 2u)))
            throw psprecomp::Error("Unmapped GE render target " + psprecomp::hex32(guest.color_address));
        auto [it, inserted] = targets.try_emplace({guest.color_address, guest.color_stride, guest.color_format});
        auto &out = it->second;
        if (inserted) {
            out.guest = guest;
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = scaled_width(); desc.Height = scaled_height(); desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&desc, nullptr, &out.color), "Create render target");
            check(device->CreateRenderTargetView(out.color.Get(), nullptr, &out.rtv), "Create target view");
            check(device->CreateShaderResourceView(out.color.Get(), nullptr, &out.srv), "Create target sampling view");
            desc.Width=width;desc.Height=height;
            if(scale!=1){
                check(device->CreateTexture2D(&desc,nullptr,&out.native_color),"Create native framebuffer");
                check(device->CreateRenderTargetView(out.native_color.Get(),nullptr,&out.native_rtv),"Create native framebuffer view");
                check(device->CreateShaderResourceView(out.native_color.Get(),nullptr,&out.native_srv),"Create native framebuffer sampling view");
            }
            desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(device->CreateTexture2D(&desc, nullptr, &out.readback), "Create target readback");
            desc.BindFlags = D3D11_BIND_DEPTH_STENCIL; desc.Usage = D3D11_USAGE_DEFAULT; desc.CPUAccessFlags = 0;
            desc.Width=scaled_width();desc.Height=scaled_height();
            desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            check(device->CreateTexture2D(&desc, nullptr, &out.depth), "Create depth buffer");
            check(device->CreateDepthStencilView(out.depth.Get(), nullptr, &out.dsv), "Create depth view");
            context->ClearDepthStencilView(out.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0, 0);
        }
        if (out.upload) {
            const auto size=std::size_t(guest.color_stride)*height*(guest.color_format==3?4:2);
            const auto *source=memory.raw_pointer(guest.color_address,size);
            if(source && out.snapshot.size()==size && std::memcmp(source,out.snapshot.data(),size)==0){out.upload=false;return out;}
            auto *destination=scale!=1?out.native_color.Get():out.color.Get();
            const auto uploaded=[&]{
                if(scale!=1)copy(out.native_srv.Get(),out.rtv.Get(),scaled_width(),scaled_height());
                out.upload=false;
            };
            if (guest.color_format == 3u && guest.color_stride == width) {
                const auto *pixels = memory.raw_pointer(guest.color_address, width * height * 4u);
                if (!pixels) throw psprecomp::Error("Non-contiguous GE framebuffer");
                context->UpdateSubresource(destination, 0, nullptr, pixels, width * 4u, 0);
                uploaded(); return out;
            }
            std::vector<std::uint32_t> pixels(width * height);
            const auto bpp = guest.color_format == 3 ? 4u : 2u;
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < std::min(width, guest.color_stride); ++x) {
                const auto at = guest.color_address + (y * guest.color_stride + x) * bpp;
                pixels[y * width + x] = expand(bpp == 4 ? memory.load32(at) : memory.load16(at), guest.color_format);
            }
            context->UpdateSubresource(destination, 0, nullptr, pixels.data(), width * 4u, 0); uploaded();
        }
        return out;
    }
};
NativeRenderer::NativeRenderer(bool warp) : impl_(std::make_unique<Impl>()) {
    auto &r = *impl_;
    auto result = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &r.device, nullptr, &r.context);
    if (FAILED(result) && !warp) result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &r.device, nullptr, &r.context);
    check(result, "Create D3D11 device");
    ComPtr<ID3DBlob> vertex, pixel, errors;
    auto compile = [&](const char *entry, const char *profile, ComPtr<ID3DBlob> &out) {
        const auto status = D3DCompile(shader, std::strlen(shader), "Sims2 GE", nullptr, nullptr, entry, profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
        if (FAILED(status)) throw psprecomp::Error(errors ? static_cast<const char *>(errors->GetBufferPointer()) : "GE shader compile failed");
    };
    compile("vs", "vs_4_0", vertex); compile("ps", "ps_4_0", pixel);
    check(r.device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &r.vs), "Create vertex shader");
    check(r.device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &r.ps), "Create pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(GpuVertex,uv),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,offsetof(GpuVertex,color),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,offsetof(GpuVertex,extra),D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(r.device->CreateInputLayout(elements, 4, vertex->GetBufferPointer(), vertex->GetBufferSize(), &r.layout), "Create vertex layout");
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = sizeof(Parameters); desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(r.device->CreateBuffer(&desc, nullptr, &r.parameters), "Create shader parameters");
    ComPtr<ID3DBlob> copy_vertex,copy_pixel;
    check(D3DCompile(copy_shader,std::strlen(copy_shader),nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&copy_vertex,&errors),"Compile framebuffer vertex shader");
    check(D3DCompile(copy_shader,std::strlen(copy_shader),nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&copy_pixel,&errors),"Compile framebuffer pixel shader");
    check(r.device->CreateVertexShader(copy_vertex->GetBufferPointer(),copy_vertex->GetBufferSize(),nullptr,&r.copy_vs),"Create framebuffer vertex shader");
    check(r.device->CreatePixelShader(copy_pixel->GetBufferPointer(),copy_pixel->GetBufferSize(),nullptr,&r.copy_ps),"Create framebuffer pixel shader");
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    check(r.device->CreateSamplerState(&sampler,&r.copy_sampler),"Create framebuffer sampler");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    check(r.device->CreateRasterizerState(&raster,&r.copy_raster),"Create framebuffer rasterizer");
}
NativeRenderer::~NativeRenderer() {
    const auto &r = *impl_;
    if (r.profile) std::cerr << "[profile] renderer_draw_ms=" << r.draw_ns / 1000000.0
        << " vertex_conversion_ms=" << r.vertex_ns / 1000000.0
        << " texture_hash_ms=" << r.hash_ns / 1000000.0 << " texture_upload_ms=" << r.upload_ns / 1000000.0
        << " framebuffer_flush_ms=" << r.flush_ns / 1000000.0 << " texture_misses=" << r.texture_misses
        << " framebuffer_readbacks=" << r.flush_count << '\n';
}
void NativeRenderer::set_render_scale(float scale) {
    if(!std::isfinite(scale) || scale<0.5f || scale>16)throw psprecomp::Error("Render scale must be between 0.5 and 16");
    auto &r=*impl_;if(r.scale==scale)return;
    for(const auto &[key,target]:r.targets)if(target.dirty)throw psprecomp::Error("Flush framebuffers before changing render scale");
    r.context->OMSetRenderTargets(0,nullptr,nullptr);
    ID3D11ShaderResourceView *none{};r.context->PSSetShaderResources(0,1,&none);
    r.targets.clear();r.scale=scale;
}
float NativeRenderer::render_scale() const noexcept {return impl_->scale;}
void NativeRenderer::set_anisotropy(unsigned level) {
    if(level!=0 && level!=2 && level!=4 && level!=8 && level!=16)throw psprecomp::Error("Invalid anisotropic filtering level");
    impl_->anisotropy=level;
}
void NativeRenderer::set_display_aspect(float aspect) {
    if(!std::isfinite(aspect) || aspect<0.01f || aspect>128.0f)throw psprecomp::Error("Unsupported display aspect ratio");
    impl_->aspect_factor=(480.0f/272.0f)/aspect;
}
ID3D11ShaderResourceView *NativeRenderer::presentation_view(const psprecomp::GuestMemory &memory,
    std::uint32_t address,unsigned stride,unsigned format) const {
    const auto &r=*impl_;if(r.scale==1 && r.aspect_factor==1)return nullptr;
    const auto it=r.targets.find({address,stride,format});if(it==r.targets.end())return nullptr;
    const auto &t=it->second;
    const auto *source=memory.raw_pointer(address,t.snapshot.size());
    if(t.dirty || t.snapshot.empty() || !source || std::memcmp(source,t.snapshot.data(),t.snapshot.size()))return nullptr;
    return t.srv.Get();
}
std::uint64_t NativeRenderer::draws() const noexcept { return impl_->count; }
std::uint64_t NativeRenderer::framebuffer_readbacks() const noexcept { return impl_->flush_count; }
ID3D11Device *NativeRenderer::device() const noexcept { return impl_->device.Get(); }
ID3D11DeviceContext *NativeRenderer::context() const noexcept { return impl_->context.Get(); }
NativeRenderer::ProfileStats NativeRenderer::profile_stats() const noexcept {
    return {impl_->draw_ns, impl_->flush_ns, impl_->vertex_ns};
}
void NativeRenderer::flush(psprecomp::GuestMemory &memory) {
    flush(memory, nullptr);
}
void NativeRenderer::prepare_transfer(psprecomp::GuestMemory &memory, const BlockTransfer &transfer) {
    flush(memory, &transfer);
}
void NativeRenderer::flush(psprecomp::GuestMemory &memory, const BlockTransfer *transfer) {
    auto &r = *impl_; ProfileScope timer(r.flush_ns, r.profile);
    bool unbound = false;
    for (auto &[key, target] : r.targets) {
        if (!target.dirty) continue;
        if (transfer && transfer->width && transfer->height) {
            const std::uint64_t begin = target.guest.color_address & 0x0FFFFFFFu;
            const auto end = begin + std::uint64_t(target.guest.color_stride) * height * (target.guest.color_format == 3 ? 4u : 2u);
            const auto overlaps = [&](std::uint32_t address, std::uint32_t stride, std::uint32_t x, std::uint32_t y) {
                const auto first = (address & 0x0FFFFFFFu) + (std::uint64_t(y) * stride + x) * transfer->bytes_per_pixel;
                const auto last = first + (std::uint64_t(transfer->height - 1u) * stride + transfer->width) * transfer->bytes_per_pixel;
                return first < end && begin < last;
            };
            if (!overlaps(transfer->source, transfer->source_stride, transfer->source_x, transfer->source_y) &&
                !overlaps(transfer->destination, transfer->destination_stride, transfer->destination_x, transfer->destination_y)) continue;
        }
        if (!unbound) {
            r.context->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView *none{}; r.context->PSSetShaderResources(0, 1, &none);
            unbound = true;
        }
        ++r.flush_count;
        if(r.scale!=1)r.copy(target.srv.Get(),target.native_rtv.Get(),width,height);
        r.context->CopyResource(target.readback.Get(), r.scale!=1?target.native_color.Get():target.color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; check(r.context->Map(target.readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read GE framebuffer");
        const auto &guest = target.guest; const auto bpp = guest.color_format == 3 ? 4u : 2u;
        auto *destination = memory.raw_pointer(guest.color_address, static_cast<std::size_t>(guest.color_stride) * height * bpp);
        for (unsigned y = 0; y < height; ++y) {
            const auto *pixels = reinterpret_cast<const std::uint32_t *>(static_cast<const std::uint8_t *>(mapped.pData) + y * mapped.RowPitch);
            if (bpp == 4u && destination) {
                std::memcpy(destination + y * guest.color_stride * 4u, pixels, std::min(width, guest.color_stride) * 4u);
                continue;
            }
            for (unsigned x = 0; x < std::min(width, guest.color_stride); ++x) {
                const auto at = guest.color_address + (y * guest.color_stride + x) * bpp;
                if (bpp == 4) memory.store32(at, pixels[x]); else memory.store16(at, pack(pixels[x], guest.color_format));
            }
        }
        r.context->Unmap(target.readback.Get(), 0); target.dirty = false; target.upload = true;
        if((r.scale!=1 || r.aspect_factor!=1) && destination){
            const auto size=std::size_t(guest.color_stride)*height*bpp;
            target.snapshot.assign(destination,destination+size);
        }
    }
}
void NativeRenderer::draw(psprecomp::GuestMemory &memory, const DrawCall &call) {
    ProfileScope timer(impl_->draw_ns, impl_->profile);
    if (call.vertices.empty()) return;
    if (call.stencil.test_enabled && !call.clear_mode)
        throw psprecomp::Error("PSP framebuffer-alpha stencil testing is not implemented in the D3D11 backend");
    // Resolve a render-to-texture dependency before the decoder reads guest
    // memory. This also handles framebuffer feedback without an SRV/RTV hazard.
    if (call.texture.enabled && !call.clear_mode) {
        const auto address = call.texture.address & 0x0FFFFFFFu;
        bool feedback = false;
        for (const auto &[key, target] : impl_->targets) {
            const auto begin = target.guest.color_address & 0x0FFFFFFFu;
            const auto size = target.guest.color_stride * height * (target.guest.color_format == 3u ? 4u : 2u);
            feedback |= target.dirty && address >= begin && address - begin < size;
        }
        if (feedback) flush(memory);
    }
    auto &r = *impl_; auto &target = r.target(memory, call.target);
    if(r.trace_projection && r.projection_signatures.size()<80){
        std::array<std::uint32_t,22> signature{};
        for(unsigned i=0;i<16;++i)signature[i]=std::bit_cast<std::uint32_t>(call.projection[i]);
        signature[16]=call.through;signature[17]=call.clear_mode;signature[18]=std::bit_cast<std::uint32_t>(call.viewport.x_scale);
        signature[19]=std::bit_cast<std::uint32_t>(call.viewport.y_scale);signature[20]=0;signature[21]=unsigned(call.primitive);
        if(r.projection_signatures.insert(signature).second){
            std::cerr<<"[projection] draw="<<r.count<<" through="<<call.through<<" clear="<<call.clear_mode
                <<" prim="<<unsigned(call.primitive)<<" cmd="<<psprecomp::hex32(call.command_address)
                <<" target="<<psprecomp::hex32(call.target.color_address)<<" texture="<<psprecomp::hex32(call.texture.address)<<" matrix=";
            for(float value:call.projection)std::cerr<<value<<',';
            std::cerr<<" viewport="<<call.viewport.x_scale<<','<<call.viewport.y_scale<<','<<call.viewport.x_offset-call.viewport.offset_x<<','<<call.viewport.y_offset-call.viewport.offset_y<<'\n';
        }
    }
    if (std::getenv("SIMS2_TRACE_RENDERER") && r.count < 12u) {
        std::cerr << "[render] draw=" << r.count << " prim=" << unsigned(call.primitive) << " through=" << call.through
            << " clear=" << call.clear_mode << '/' << call.clear_flags << " tex=" << call.texture.enabled << '/' << unsigned(call.texture.format)
            << " lighting=" << call.lighting_enabled << " stencil=" << call.stencil.test_enabled
            << " target=" << psprecomp::hex32(call.target.color_address) << " z=" << call.vertices.front().position[2] << '\n';
    }
    // Match display framing only on the game's full-screen viewport. Clears
    // still cover the full image; framebuffer-to-framebuffer effects already
    // contain the corrected camera/HUD and must not be corrected a second time.
    bool framebuffer_texture=false;
    if(r.aspect_factor!=1 && call.texture.enabled){
        const auto address=call.texture.address&0x0fffffffu;
        for(const auto &[key,source]:r.targets){
            const auto begin=source.guest.color_address&0x0fffffffu;
            const auto size=source.guest.color_stride*height*(source.guest.color_format==3?4u:2u);
            if(address>=begin && address-begin<size){framebuffer_texture=true;break;}
        }
    }
    const auto approximately=[](float a,float b){return std::abs(a-b)<0.1f;};
    const auto &vp=call.viewport;
    const bool match_aspect=r.aspect_factor!=1 && !call.clear_mode && !framebuffer_texture &&
        approximately(std::abs(vp.x_scale),240) && approximately(std::abs(vp.y_scale),136) &&
        approximately(vp.x_offset-vp.offset_x,240) && approximately(vp.y_offset-vp.offset_y,136);
    const bool hud=call.through || std::abs(call.projection[15])>0.5f;
    ProfileScope vertex_timer(r.vertex_ns, r.profile);
    auto &output = r.output_vertices; output.clear();
    r.converted_vertices.resize(call.vertices.size());
    r.converted_generation.resize(call.vertices.size());
    if (++r.conversion_generation == 0) {
        std::fill(r.converted_generation.begin(), r.converted_generation.end(), 0);
        ++r.conversion_generation;
    }
    auto convert = [&](const Vertex &v) {
        GpuVertex out{}; auto p = v.position; p[3] = 1;
        out.color = rgba(call.has_vertex_color || call.clear_mode ? v.color : call.material_color); out.extra[3] = 1; out.uv[2] = 1;
        if (call.through) {
            out.position = {p[0] * 2 / width - 1, 1 - p[1] * 2 / height, std::clamp(p[2] / 65535, 0.0f, 1.0f), 1};
            out.uv = {v.texcoord[0] / std::max(1u, unsigned(call.texture.width)), v.texcoord[1] / std::max(1u, unsigned(call.texture.height)),1};
        } else {
            const auto world = multiply(call.world, p), view = multiply(call.view, world), clip = multiply(call.projection, view);
            if (call.lighting_enabled && !call.clear_mode) light_vertex(call,v,world,out.color,out.extra);
            const auto &vp = call.viewport;
            out.position = {((vp.x_offset - vp.offset_x) * 2 / width - 1) * clip[3] + clip[0] * vp.x_scale * 2 / width,
                (1 - (vp.y_offset - vp.offset_y) * 2 / height) * clip[3] - clip[1] * vp.y_scale * 2 / height,
                (clip[2] * vp.z_scale + clip[3] * vp.z_offset) / 65535, clip[3]};
            out.uv = {v.texcoord[0] * call.texture.scale_u + call.texture.offset_u, v.texcoord[1] * call.texture.scale_v + call.texture.offset_v,1};
            if (call.texture.mapping_mode == 1u) {
                V4 source = p;
                if (call.texture.projection_source == 1u) source = {v.texcoord[0],v.texcoord[1],0,1};
                if (call.texture.projection_source >= 2u) {
                    source = {v.normal[0],v.normal[1],v.normal[2],1};
                    if (call.texture.projection_source == 2u) {
                        const auto length=std::sqrt(source[0]*source[0]+source[1]*source[1]+source[2]*source[2]);
                        if(length>0) for(unsigned i=0;i<3;++i) source[i]/=length;
                    }
                }
                const auto stq=multiply(call.texture_matrix,source);
                out.uv={stq[0],stq[1],stq[2]};
            }
            if (call.fog.enabled) out.extra[3] = (view[2] + call.fog.end) * call.fog.scale;
        }
        if(match_aspect){
            constexpr float center=480.0f/512.0f-1.0f;
            const float horizontal_factor=hud?std::min(1.0f,r.aspect_factor):r.aspect_factor;
            out.position[0]=(out.position[0]-center*out.position[3])*horizontal_factor+center*out.position[3];
            if(hud)out.position[1]*=std::min(1.0f,1/r.aspect_factor);
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
    const auto at = [&](std::size_t i) { return converted(primitive_vertex(call.indices, call.vertices.size(), i)); };
    auto topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    if (call.primitive == PrimitiveType::Sprites) {
        for (std::size_t i = 0; i + 1 < count; i += 2) {
            auto a = at(i), b = at(i + 1); a.color = b.color; a.position[2] = b.position[2];
            for (unsigned j = 0; j < 3; ++j) { a.position[j] /= a.position[3]; b.position[j] /= b.position[3]; }
            a.position[3] = b.position[3] = 1; auto c = a, d = b;
            c.position[0] = b.position[0]; c.uv[0] = b.uv[0]; d.position[0] = a.position[0]; d.uv[0] = a.uv[0];
            output.insert(output.end(), {a, c, b, a, b, d});
        }
    } else if (call.primitive == PrimitiveType::Points || call.primitive == PrimitiveType::Lines || call.primitive == PrimitiveType::LineStrip) {
        topology = call.primitive == PrimitiveType::Points ? D3D11_PRIMITIVE_TOPOLOGY_POINTLIST : call.primitive == PrimitiveType::Lines ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        for (std::size_t i = 0; i < count; ++i) output.push_back(at(i));
    } else {
        auto &indices = r.triangle_vertices; triangle_indices(call.primitive, count, call.indices, call.vertices.size(), indices);
        output.reserve(indices.size());
        for (const auto i : indices) output.push_back(converted(i));
    }
    vertex_timer.stop();
    if (output.empty()) return;
    ID3D11ShaderResourceView *none{}; r.context->PSSetShaderResources(0, 1, &none);
    ID3D11RenderTargetView *view = target.rtv.Get(); r.context->OMSetRenderTargets(1, &view, target.dsv.Get());
    const auto bytes = static_cast<unsigned>(output.size() * sizeof(GpuVertex));
    if (bytes > r.vertex_capacity) {
        r.vertices.Reset(); D3D11_BUFFER_DESC desc{}; desc.ByteWidth = std::max(bytes, 4u * 1024u * 1024u); desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.Usage = D3D11_USAGE_DYNAMIC; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(r.device->CreateBuffer(&desc, nullptr, &r.vertices), "Create GE vertex buffer"); r.vertex_capacity = desc.ByteWidth; r.vertex_cursor = 0;
    }
    // Append without touching vertices still consumed by earlier draws. Only
    // discard at wraparound, allowing D3D to rename the backing allocation.
    if (bytes > r.vertex_capacity - r.vertex_cursor) r.vertex_cursor = 0;
    const unsigned offset = r.vertex_cursor;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(r.context->Map(r.vertices.Get(), 0, offset ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Upload GE vertices");
    std::memcpy(static_cast<std::uint8_t *>(mapped.pData) + offset, output.data(), bytes); r.context->Unmap(r.vertices.Get(), 0);
    r.vertex_cursor += bytes;
    unsigned stride = sizeof(GpuVertex); auto *buffer = r.vertices.Get(); r.context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    r.context->IASetInputLayout(r.layout.Get()); r.context->IASetPrimitiveTopology(topology);
    r.context->VSSetShader(r.vs.Get(), nullptr, 0); r.context->PSSetShader(r.ps.Get(), nullptr, 0);
    Parameters params{}; params.function = call.texture.function; params.texture_alpha = call.texture.alpha_from_texture;
    params.alpha_enabled = call.alpha_test.enabled && !call.clear_mode; params.alpha_function = call.alpha_test.function;
    params.alpha_reference = call.alpha_test.reference; params.alpha_mask = call.alpha_test.mask; params.fog = rgba(call.fog.color);
    params.environment = rgba(call.texture.environment_color); params.double_color = call.texture.double_color;
    if (call.texture.enabled && !call.clear_mode) {
        const auto key = [&] { ProfileScope hash_timer(r.hash_ns, r.profile); return texture_key(memory, call.texture); }();
        auto found = r.textures.find(key);
        if (found == r.textures.end()) {
            ProfileScope upload_timer(r.upload_ns, r.profile); ++r.texture_misses;
            std::vector<std::uint32_t> pixels;
            if (!decode_texture(memory, call.texture, pixels)) throw psprecomp::Error("Cannot decode GE texture " + psprecomp::hex32(call.texture.address));
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = call.texture.width; desc.Height = call.texture.height;
            desc.MipLevels = desc.ArraySize = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{pixels.data(), desc.Width * 4u, 0}; ComPtr<ID3D11Texture2D> texture;
            check(r.device->CreateTexture2D(&desc, &data, &texture), "Upload GE texture"); ComPtr<ID3D11ShaderResourceView> srv;
            check(r.device->CreateShaderResourceView(texture.Get(), nullptr, &srv), "Create texture view");
            if (r.textures.size() > 2048) r.textures.clear(); found = r.textures.emplace(key, std::move(srv)).first;
        }
        auto *srv = found->second.Get(); r.context->PSSetShaderResources(0, 1, &srv); params.textured = 1;
    }
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = static_cast<D3D11_FILTER>(((call.texture.min_filter & 1u) << 4u) | ((call.texture.mag_filter & 1u) << 2u));
    sampler.AddressU = call.texture.wrap_s ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = call.texture.wrap_t ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP; sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX; sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if(r.anisotropy && !call.through && std::abs(call.projection[15])<0.5f && (call.texture.min_filter&1) && (call.texture.mag_filter&1)){
        sampler.Filter=D3D11_FILTER_ANISOTROPIC;sampler.MaxAnisotropy=r.anisotropy;
    }
    const auto sampler_key = unsigned(sampler.Filter) | (call.texture.wrap_s << 8) | (call.texture.wrap_t << 9) | (sampler.MaxAnisotropy << 16);
    auto &sampler_state = r.samplers[sampler_key];
    if (!sampler_state) check(r.device->CreateSamplerState(&sampler, &sampler_state), "Create GE sampler");
    auto *sample = sampler_state.Get(); r.context->PSSetSamplers(0, 1, &sample);
    r.context->UpdateSubresource(r.parameters.Get(), 0, nullptr, &params, 0, 0); auto *parameters = r.parameters.Get(); r.context->PSSetConstantBuffers(0, 1, &parameters);
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = call.culling_enabled && !call.through && !call.clear_mode ? D3D11_CULL_BACK : D3D11_CULL_NONE;
    raster.FrontCounterClockwise = !call.cull_clockwise; raster.DepthClipEnable = TRUE; raster.ScissorEnable = TRUE;
    auto &rs = r.raster_states[unsigned(raster.CullMode) | (unsigned(raster.FrontCounterClockwise) << 4)];
    if (!rs) check(r.device->CreateRasterizerState(&raster, &rs), "Create raster state"); r.context->RSSetState(rs.Get());
    D3D11_VIEWPORT viewport{0,0,float(r.scaled_width()),float(r.scaled_height()),0,1}; r.context->RSSetViewports(1, &viewport);
    const auto horizontal=[&](float x){return match_aspect && hud?(x-240)*std::min(1.0f,r.aspect_factor)+240:x;};
    const auto vertical=[&](float y){return match_aspect && hud?(y-136)*std::min(1.0f,1/r.aspect_factor)+136:y;};
    D3D11_RECT scissor{LONG(std::floor(horizontal(float(call.viewport.scissor_x1))*r.scale)), LONG(std::floor(vertical(float(call.viewport.scissor_y1))*r.scale)),
        LONG(std::ceil(horizontal(float(call.viewport.scissor_x2+1))*r.scale)), LONG(std::ceil(vertical(float(call.viewport.scissor_y2+1))*r.scale))}; r.context->RSSetScissorRects(1, &scissor);
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = call.depth.test_enabled || call.clear_mode;
    depth.DepthWriteMask = (call.clear_mode ? (call.clear_flags & 4u) != 0 : call.depth.write_enabled && call.depth.test_enabled) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = call.clear_mode ? D3D11_COMPARISON_ALWAYS : comparison(call.depth.function);
    depth.FrontFace = {D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};
    depth.BackFace = depth.FrontFace;
    auto &ds = r.depth_states[unsigned(depth.DepthEnable) | (unsigned(depth.DepthWriteMask) << 1) | (unsigned(depth.DepthFunc) << 2)];
    if (!ds) check(r.device->CreateDepthStencilState(&depth, &ds), "Create depth state"); r.context->OMSetDepthStencilState(ds.Get(), 0);
    D3D11_BLEND_DESC blend{}; auto &b = blend.RenderTarget[0];
    b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE; b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_ZERO;
    b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTargetWriteMask = call.clear_mode ? ((call.clear_flags & 1u) ? 7u : 0u) | ((call.clear_flags & 2u) ? 8u : 0u) : 15u;
    if (!call.clear_mode) for (unsigned channel = 0; channel < 4; ++channel) {
        const auto mask = channel == 3u ? call.stencil.alpha_write_mask & 255u : (call.stencil.color_write_mask >> (channel * 8u)) & 255u;
        if (mask == 255u) b.RenderTargetWriteMask &= static_cast<UINT8>(~(1u << channel));
        else if (mask != 0u) throw psprecomp::Error("Partial-bit PSP framebuffer write masks are not implemented");
    }
    if (call.blend.enabled && !call.clear_mode) {
        b.BlendEnable = TRUE;
        const auto factor = [](unsigned factor, unsigned fixed, bool source) {
            const D3D11_BLEND factors[]{source ? D3D11_BLEND_DEST_COLOR : D3D11_BLEND_SRC_COLOR,
                source ? D3D11_BLEND_INV_DEST_COLOR : D3D11_BLEND_INV_SRC_COLOR,
                D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_DEST_ALPHA,D3D11_BLEND_INV_DEST_ALPHA};
            if (factor < 6u) return factors[factor];
            if (factor == 10u && fixed == 0) return D3D11_BLEND_ZERO;
            if (factor == 10u && (fixed & 0xFFFFFFu) == 0xFFFFFFu) return D3D11_BLEND_ONE;
            throw psprecomp::Error("Unsupported GE blend factor " + std::to_string(factor));
        };
        b.SrcBlend = factor(call.blend.source_factor, call.blend.fixed_source, true);
        b.DestBlend = factor(call.blend.destination_factor, call.blend.fixed_destination, false);
        constexpr D3D11_BLEND_OP ops[]{D3D11_BLEND_OP_ADD,D3D11_BLEND_OP_SUBTRACT,D3D11_BLEND_OP_REV_SUBTRACT,D3D11_BLEND_OP_MIN,D3D11_BLEND_OP_MAX};
        if (call.blend.equation > 4) throw psprecomp::Error("Unsupported GE blend equation");
        b.BlendOp = ops[call.blend.equation]; b.SrcBlendAlpha = D3D11_BLEND_ONE; b.DestBlendAlpha = D3D11_BLEND_ZERO; b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }
    auto &bs = r.blend_states[unsigned(b.BlendEnable) | (unsigned(b.SrcBlend) << 1) | (unsigned(b.DestBlend) << 6) |
        (unsigned(b.BlendOp) << 11) | (unsigned(b.RenderTargetWriteMask) << 16)];
    if (!bs) check(r.device->CreateBlendState(&blend, &bs), "Create blend state"); r.context->OMSetBlendState(bs.Get(), nullptr, 0xFFFFFFFFu);
    r.context->Draw(static_cast<unsigned>(output.size()), 0); target.dirty = true; ++r.count;
}
}
