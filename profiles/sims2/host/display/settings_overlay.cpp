#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include "display/settings_overlay.hpp"
#include "display/output_surface.hpp"
#include "display/settings_menu.hpp"
#include "psprecomp/common.hpp"
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <fstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace sims2::display {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT result, const char *action) {
    if (FAILED(result)) throw psprecomp::Error(std::string(action) + " failed: " + std::to_string(result));
}
constexpr char shader[] = R"(
cbuffer Output : register(b0) {float2 uv_scale;float2 padding;};
Texture2D image : register(t0); SamplerState sample_image : register(s0);
struct V { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
V vs(uint id:SV_VertexID) { V o; o.uv=float2((id<<1)&2,id&2); o.position=float4(o.uv.x*2-1,1-o.uv.y*2,0,1); return o; }
float4 ps(V p):SV_TARGET { return float4(image.Sample(sample_image,p.uv*uv_scale).rgb,1); }
)";
constexpr std::array<unsigned, 2> resolutions[]{{960,544},{1280,720},{1600,900},{1920,1080},{2560,1080},{2560,1440},{3440,1440},{3840,2160},{5120,1440}};
void row(const char *label) {
    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label); ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
}
unsigned render_dimension(float value){return unsigned(std::max(1l,std::lround(value)));}
void note(const char *text) { ImGui::PushTextWrapPos(); ImGui::TextDisabled("%s", text); ImGui::PopTextWrapPos(); }
}
struct SettingsOverlay::Impl : SettingsMenu {
    HWND window{};
    std::unique_ptr<OutputSurface> surface;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11Texture2D> upload;
    ComPtr<ID3D11ShaderResourceView> image;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> parameters;
    ComPtr<ID3D11SamplerState> nearest, linear;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    ImGuiContext *gui{};
    bool win32_initialized{}, dx11_initialized{};
    ~Impl() {
        if (!gui) return;
        ImGui::SetCurrentContext(gui);
        if (dx11_initialized) ImGui_ImplDX11_Shutdown();
        if (win32_initialized) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(gui);
    }
    std::string capture_path;
    void capture() {
        if(capture_path.empty())return;
        ComPtr<ID3D11Texture2D> buffer,staging;
        buffer=surface->capture_texture();
        D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        check(device->CreateTexture2D(&desc,nullptr,&staging),"Create output capture");
        context->CopyResource(staging.Get(),buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read output capture");
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
        header.bfSize=header.bfOffBits+desc.Width*desc.Height*4;
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(desc.Width);info.biHeight=-LONG(desc.Height);
        info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        std::ofstream out(capture_path,std::ios::binary);
        out.write(reinterpret_cast<const char *>(&header),sizeof(header));out.write(reinterpret_cast<const char *>(&info),sizeof(info));
        for(unsigned y=0;y<desc.Height;++y)out.write(static_cast<const char *>(mapped.pData)+y*mapped.RowPitch,desc.Width*4);
        context->Unmap(staging.Get(),0);
        out.close();if(!out)throw psprecomp::Error("Cannot write output capture");capture_path.clear();
    }
    std::uint64_t presentations{};
    std::chrono::steady_clock::time_point previous{};
    ImGuiStyle base;
    void resize(unsigned w,unsigned h,const HostSettings &settings){
        target.Reset();surface->configure(w,h,settings);target=surface->target();width=w;height=h;
    }
    void style() {
        const float desired=std::clamp(std::round(std::max(1.0f,float(height)/1080.0f)*4)/4,1.0f,3.0f);
        if (scale==desired) return;
        scale=desired; ImGui::GetStyle()=base; ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetIO().FontGlobalScale=scale;
    }
    SettingsAction menu(const HostSettings &current) {
        output_status=surface->status();hdr_active=surface->hdr_active();
        return SettingsMenu::menu(current);
    }
};
SettingsOverlay::SettingsOverlay(void *window, ID3D11Device *device, ID3D11DeviceContext *context):impl_(std::make_unique<Impl>()) {
    auto &p=*impl_; p.window=static_cast<HWND>(window);p.device=device;p.context=context;
    ComPtr<IDXGIDevice> dxgi; check(device->QueryInterface(IID_PPV_ARGS(&dxgi)),"Query output device");
    ComPtr<IDXGIAdapter> adapter;check(dxgi->GetAdapter(&adapter),"Get output adapter");
    ComPtr<IDXGIFactory2> factory;check(adapter->GetParent(IID_PPV_ARGS(&factory)),"Get output factory");
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    check(factory->CreateSwapChainForHwnd(device,p.window,&desc,nullptr,nullptr,&p.swapchain),"Create output swapchain");
    p.surface=std::make_unique<OutputSurface>(window,device,context,p.swapchain.Get());
    factory->MakeWindowAssociation(p.window,DXGI_MWA_NO_ALT_ENTER);
    ComPtr<ID3DBlob> vs,ps,error;
    check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&vs,&error),"Compile output vertex shader");
    check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&ps,&error),"Compile output pixel shader");
    check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p.vertex),"Create output vertex shader");
    check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p.pixel),"Create output pixel shader");
    D3D11_BUFFER_DESC constants{};constants.ByteWidth=16;constants.Usage=D3D11_USAGE_DEFAULT;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&constants,nullptr,&p.parameters),"Create output parameters");
    D3D11_TEXTURE2D_DESC texture{};texture.Width=480;texture.Height=272;texture.MipLevels=texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    check(device->CreateTexture2D(&texture,nullptr,&p.upload),"Create output upload");
    check(device->CreateShaderResourceView(p.upload.Get(),nullptr,&p.image),"Create output sampling view");
    D3D11_SAMPLER_DESC sampler{};sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD=D3D11_FLOAT32_MAX;sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    check(device->CreateSamplerState(&sampler,&p.nearest),"Create nearest output filter");
    sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    check(device->CreateSamplerState(&sampler,&p.linear),"Create linear output filter");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    check(device->CreateRasterizerState(&raster,&p.raster),"Create output raster state");
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    check(device->CreateDepthStencilState(&depth,&p.depth),"Create output depth state");
    IMGUI_CHECKVERSION();p.gui=ImGui::CreateContext();ImGui::SetCurrentContext(p.gui);ImGui::GetIO().IniFilename=nullptr;
    ImFontConfig font; font.SizePixels=18.0f;
    ImGui::GetIO().Fonts->AddFontDefault(&font);
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    p.base=settings_ui::initialize_style();
    if (!(p.win32_initialized=ImGui_ImplWin32_Init(p.window)) || !(p.dx11_initialized=ImGui_ImplDX11_Init(device,context))) throw psprecomp::Error("Cannot initialize settings overlay");
}
SettingsOverlay::~SettingsOverlay() = default;
std::uint64_t SettingsOverlay::presented_frames() const noexcept{return impl_->presentations;}
void SettingsOverlay::leave_fullscreen(){impl_->surface->leave_fullscreen();}
void SettingsOverlay::toggle(const HostSettings &current) {
    auto &p=*impl_;p.opened=!p.opened;if(p.opened){p.draft=current;p.focus=true;p.status.clear();}
}
bool SettingsOverlay::open() const noexcept { return impl_->opened; }
bool SettingsOverlay::handle_message(void *window,unsigned message,std::uintptr_t w,std::intptr_t l) {
    ImGui::SetCurrentContext(impl_->gui);
    return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(window),message,WPARAM(w),LPARAM(l))!=0;
}
void SettingsOverlay::capture_next_frame(std::string path) { impl_->capture_path=std::move(path); }
void SettingsOverlay::status(std::string message) { impl_->status=std::move(message); }
void SettingsOverlay::performance(bool enabled) { impl_->draft.show_performance=enabled; }
SettingsAction SettingsOverlay::present(std::span<const std::uint32_t> pixels,const HostSettings &settings,bool game_frame,ID3D11ShaderResourceView *rendered) {
    auto &p=*impl_;RECT rect{};GetClientRect(p.window,&rect);
    if (rect.right<=0 || rect.bottom<=0 || IsIconic(p.window)) return {};
    p.resize(unsigned(rect.right),unsigned(rect.bottom),settings);ImGui::SetCurrentContext(p.gui);p.style();
    if(!rendered)p.context->UpdateSubresource(p.upload.Get(),0,nullptr,pixels.data(),480*4,0);
    ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
    const auto now=std::chrono::steady_clock::now();
    if(game_frame && !p.opened){if(p.previous.time_since_epoch().count()){const auto ms=std::chrono::duration<float,std::milli>(now-p.previous).count();if(ms<250){p.times[p.sample++%p.times.size()]=ms;p.count=std::min(unsigned(p.times.size()),p.count+1);}}p.previous=now;}
    if(p.opened)p.previous={};
    const auto action=p.menu(settings);
    if(settings.show_performance){ImGui::SetNextWindowPos({float(p.width)-12,12},ImGuiCond_Always,{1,0});ImGui::SetNextWindowBgAlpha(.65f);
        if(ImGui::Begin("Performance (F2)",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings)){
            float sum{};for(unsigned i=0;i<p.count;++i)sum+=p.times[i];const auto average=p.count?sum/p.count:0;
            ImGui::Text("%s | %.1f fps | %.2f ms",p.opened?"Paused":"Running",average>0?1000/average:0,average);
            ImGui::Text("Direct3D 11 | %u x %u",p.width,p.height);
            ImGui::Text("VSync %s | %s",settings.vsync?"On":"Off",p.surface->hdr_active()?"HDR":"SDR");
            ImGui::Text("Render %u x %u | cap %u",render_dimension(480*effective_render_scale(settings,p.height)),render_dimension(272*effective_render_scale(settings,p.height)),settings.fps_cap);
            float lo=p.count?p.times[0]:0,hi{};for(unsigned i=0;i<p.count;++i){lo=std::min(lo,p.times[i]);hi=std::max(hi,p.times[i]);}ImGui::Text("%.2f - %.2f ms",lo,hi);
            ImGui::PlotLines("##times",p.times.data(),int(p.count),p.count==p.times.size()?int(p.sample%p.times.size()):0,nullptr,0,40,{250,55});
        }ImGui::End();}
    ImGui::Render();
    const float black[4]{0,0,0,1};p.context->ClearRenderTargetView(p.target.Get(),black);
    auto *rtv=p.target.Get();p.context->OMSetRenderTargets(1,&rtv,nullptr);
    p.context->OMSetBlendState(nullptr,nullptr,0xffffffff);p.context->OMSetDepthStencilState(p.depth.Get(),0);p.context->RSSetState(p.raster.Get());
    const auto fit=picture_rect(p.width,p.height,settings.match_camera_aspect && !rendered?AspectMode::Native:settings.aspect);
    D3D11_VIEWPORT viewport{float(fit.x),float(fit.y),float(fit.width),float(fit.height),0,1};p.context->RSSetViewports(1,&viewport);
    p.context->IASetInputLayout(nullptr);p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vertex.Get(),nullptr,0);p.context->PSSetShader(p.pixel.Get(),nullptr,0);
    p.context->GSSetShader(nullptr,nullptr,0);auto *image=rendered?rendered:p.image.Get();p.context->PSSetShaderResources(0,1,&image);
    const float uv[4]{rendered?480.0f/512.0f:1.0f,1,0,0};
    p.context->UpdateSubresource(p.parameters.Get(),0,nullptr,uv,0,0);auto *parameters=p.parameters.Get();p.context->PSSetConstantBuffers(0,1,&parameters);
    auto *sampler=settings.filter==OutputFilter::Nearest?p.nearest.Get():p.linear.Get();p.context->PSSetSamplers(0,1,&sampler);p.context->Draw(3,0);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ID3D11ShaderResourceView *none{};p.context->PSSetShaderResources(0,1,&none);
    p.capture();
    p.surface->finish(settings);
    check(p.swapchain->Present(settings.vsync?1:0,0),"Present output");++p.presentations;
    return action;
}
}
