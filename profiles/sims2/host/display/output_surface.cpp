#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "display/output_surface.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
namespace sims2::display {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char *what){if(FAILED(hr))throw psprecomp::Error(std::string(what)+": "+psprecomp::hex32(unsigned(hr)));}
// Windows scRGB: linear Rec.709, where 1.0 is 80 nits. Convert the entire
// composed SDR frame (including ImGui) once, so UI and game use the same white.
constexpr char hdr_shader[]=R"(
Texture2D image:register(t0);SamplerState sample_image:register(s0);
cbuffer Brightness:register(b0){float paper_white,peak;float2 padding;};
struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);return o;}
float4 ps(V p):SV_TARGET{
 float3 s=saturate(image.Sample(sample_image,p.uv).rgb);
 float3 linear_color=float3(s.r<=.04045?s.r/12.92:pow((s.r+.055)/1.055,2.4),
 s.g<=.04045?s.g/12.92:pow((s.g+.055)/1.055,2.4),s.b<=.04045?s.b/12.92:pow((s.b+.055)/1.055,2.4));
 return float4(min(linear_color*paper_white/80.0,peak/80.0),1);
})";
}
struct OutputSurface::Impl {
    HWND window{};ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11Texture2D> buffer,sdr;ComPtr<ID3D11RenderTargetView> buffer_target,sdr_target;ComPtr<ID3D11ShaderResourceView> sdr_view;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11RasterizerState> raster;
    unsigned width{},height{},peak{1000};bool hdr{},windows_hdr{},exclusive{},hdr_unavailable{},exclusive_failed{};DXGI_FORMAT format{DXGI_FORMAT_B8G8R8A8_UNORM};
    unsigned fullscreen_width{},fullscreen_height{};std::string reason{"HDR off"};
    std::chrono::steady_clock::time_point probe{};HMONITOR monitor{};HdrMode requested=HdrMode::Off;
    void release(){context->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView *none{};context->PSSetShaderResources(0,1,&none);buffer_target.Reset();buffer.Reset();sdr_target.Reset();sdr_view.Reset();sdr.Reset();}
};
OutputSurface::OutputSurface(void *window,ID3D11Device *device,ID3D11DeviceContext *context,IDXGISwapChain1 *swapchain):impl_(std::make_unique<Impl>()){
    auto &p=*impl_;p.window=static_cast<HWND>(window);p.device=device;p.context=context;p.swapchain=swapchain;
    ComPtr<ID3DBlob> vs,ps,error;
    check(D3DCompile(hdr_shader,sizeof(hdr_shader)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&vs,&error),"Compile HDR vertex shader");
    check(D3DCompile(hdr_shader,sizeof(hdr_shader)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&ps,&error),"Compile HDR pixel shader");
    check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p.vertex),"Create HDR vertex shader");
    check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p.pixel),"Create HDR pixel shader");
    D3D11_BUFFER_DESC constants{};constants.ByteWidth=16;constants.Usage=D3D11_USAGE_DEFAULT;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&constants,nullptr,&p.constants),"Create HDR constants");
    D3D11_SAMPLER_DESC sample{};sample.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sample.MaxLOD=D3D11_FLOAT32_MAX;
    check(device->CreateSamplerState(&sample,&p.sampler),"Create HDR sampler");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    check(device->CreateRasterizerState(&raster,&p.raster),"Create HDR rasterizer");
}
OutputSurface::~OutputSurface(){leave_fullscreen();}
void OutputSurface::leave_fullscreen(){if(impl_->swapchain)impl_->swapchain->SetFullscreenState(FALSE,nullptr);impl_->exclusive=false;}
void OutputSurface::configure(unsigned width,unsigned height,const HostSettings &settings){
    auto &p=*impl_;
    if(settings.mode!=WindowMode::Fullscreen || p.fullscreen_width!=settings.width || p.fullscreen_height!=settings.height)p.exclusive_failed=false;
    const bool want_exclusive=!p.exclusive_failed &&settings.mode==WindowMode::Fullscreen && IsWindowVisible(p.window) && GetForegroundWindow()==p.window;
    if(want_exclusive && p.hdr){p.release();check(p.swapchain->ResizeBuffers(0,width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),"Prepare SDR exclusive mode");p.hdr=false;p.format=DXGI_FORMAT_B8G8R8A8_UNORM;}
    BOOL current{};p.swapchain->GetFullscreenState(&current,nullptr);
    if(want_exclusive!=bool(current) || (want_exclusive && (p.fullscreen_width!=settings.width || p.fullscreen_height!=settings.height))){
        p.release();
        auto hr=p.swapchain->SetFullscreenState(want_exclusive,nullptr);
        if(SUCCEEDED(hr) && want_exclusive){
            DXGI_MODE_DESC mode{};mode.Width=settings.width?settings.width:width;mode.Height=settings.height?settings.height:height;mode.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            hr=p.swapchain->ResizeTarget(&mode);
        }
        p.exclusive=want_exclusive && SUCCEEDED(hr);
        if(FAILED(hr)){p.exclusive_failed=true;p.swapchain->SetFullscreenState(FALSE,nullptr);p.reason="Exclusive mode unavailable; using borderless";}
        p.fullscreen_width=settings.width;p.fullscreen_height=settings.height;
    }
    const auto now=std::chrono::steady_clock::now();const auto monitor=MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST);
    if(settings.hdr!=p.requested || monitor!=p.monitor || now-p.probe>std::chrono::seconds(1)){
        const bool previous_hdr=p.windows_hdr;const bool new_request=p.requested!=settings.hdr || p.monitor!=monitor;
        p.requested=settings.hdr;p.monitor=monitor;p.probe=now;p.windows_hdr=false;p.peak=1000;
        ComPtr<IDXGIOutput> output;ComPtr<IDXGIOutput6> advanced;DXGI_OUTPUT_DESC1 desc{};
        if(SUCCEEDED(p.swapchain->GetContainingOutput(&output)) && SUCCEEDED(output.As(&advanced)) && SUCCEEDED(advanced->GetDesc1(&desc))){
            p.windows_hdr=desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            if(std::isfinite(desc.MaxLuminance) && desc.MaxLuminance>=80)p.peak=unsigned(std::clamp(std::lround(desc.MaxLuminance),80l,10000l));
        }
        if(new_request || previous_hdr!=p.windows_hdr)p.hdr_unavailable=false;
    }
    bool hdr=settings.hdr!=HdrMode::Off && p.windows_hdr && !p.exclusive && !p.hdr_unavailable;
    const auto desired=hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
    if(!p.buffer || width!=p.width || height!=p.height || desired!=p.format){
        p.release();auto hr=p.swapchain->ResizeBuffers(0,width,height,desired,DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
        if(FAILED(hr) && hdr){p.hdr_unavailable=true;hdr=false;check(p.swapchain->ResizeBuffers(0,width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),"Restore SDR output");}
        else check(hr,"Resize presentation output");
        p.hdr=hdr;p.format=hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;p.width=width;p.height=height;
        ComPtr<IDXGISwapChain3> advanced;UINT support{};
        const auto space=hdr?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        const bool color_ok=SUCCEEDED(p.swapchain.As(&advanced)) && SUCCEEDED(advanced->CheckColorSpaceSupport(space,&support)) && (support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) && SUCCEEDED(advanced->SetColorSpace1(space));
        if(!color_ok && hdr){p.hdr_unavailable=true;p.release();check(p.swapchain->ResizeBuffers(0,width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),"Restore unsupported HDR output");if(advanced)advanced->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);p.hdr=false;p.format=DXGI_FORMAT_B8G8R8A8_UNORM;}
        check(p.swapchain->GetBuffer(0,IID_PPV_ARGS(&p.buffer)),"Get presentation buffer");check(p.device->CreateRenderTargetView(p.buffer.Get(),nullptr,&p.buffer_target),"Create presentation view");
        if(p.hdr){
            D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            check(p.device->CreateTexture2D(&desc,nullptr,&p.sdr),"Create SDR composition target");check(p.device->CreateRenderTargetView(p.sdr.Get(),nullptr,&p.sdr_target),"Create SDR composition view");check(p.device->CreateShaderResourceView(p.sdr.Get(),nullptr,&p.sdr_view),"Create SDR composition sampling view");
        }
    }
    if(p.hdr)p.reason="HDR active (linear scRGB), display peak "+std::to_string(p.peak)+" nits";
    else if(settings.hdr==HdrMode::Off)p.reason="HDR off";
    else if(p.exclusive)p.reason="HDR uses borderless/windowed mode; exclusive fullscreen is SDR";
    else if(!p.windows_hdr)p.reason="HDR unavailable: enable HDR for this display in Windows";
    else p.reason="HDR unsupported by this output; SDR fallback active";
    if(p.exclusive_failed)p.reason="Exclusive mode unavailable; borderless active. "+p.reason;
}
ID3D11RenderTargetView *OutputSurface::target()const{return impl_->hdr?impl_->sdr_target.Get():impl_->buffer_target.Get();}
ID3D11Texture2D *OutputSurface::capture_texture()const{return impl_->hdr?impl_->sdr.Get():impl_->buffer.Get();}
bool OutputSurface::hdr_active()const{return impl_->hdr;}
unsigned OutputSurface::display_peak()const{return impl_->peak;}
const char *OutputSurface::status()const{return impl_->reason.c_str();}
void OutputSurface::composite_hdr(ID3D11ShaderResourceView *source,ID3D11RenderTargetView *target,unsigned width,unsigned height,unsigned white,unsigned peak){
    auto &p=*impl_;p.context->OMSetRenderTargets(1,&target,nullptr);p.context->OMSetDepthStencilState(nullptr,0);p.context->OMSetBlendState(nullptr,nullptr,0xffffffff);p.context->RSSetState(p.raster.Get());
    D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};p.context->RSSetViewports(1,&vp);p.context->IASetInputLayout(nullptr);p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vertex.Get(),nullptr,0);p.context->PSSetShader(p.pixel.Get(),nullptr,0);p.context->GSSetShader(nullptr,nullptr,0);
    const float values[4]{float(white),float(peak),0,0};p.context->UpdateSubresource(p.constants.Get(),0,nullptr,values,0,0);auto *constants=p.constants.Get();p.context->PSSetConstantBuffers(0,1,&constants);
    auto *sampler=p.sampler.Get();p.context->PSSetSamplers(0,1,&sampler);p.context->PSSetShaderResources(0,1,&source);p.context->Draw(3,0);
    ID3D11ShaderResourceView *none{};p.context->PSSetShaderResources(0,1,&none);
}
void OutputSurface::finish(const HostSettings &settings){auto &p=*impl_;if(p.hdr)composite_hdr(p.sdr_view.Get(),p.buffer_target.Get(),p.width,p.height,settings.paper_white,settings.peak_nits?settings.peak_nits:p.peak);}
}
