#include "display/presentation_limiter.hpp"
#include "display/output_surface.hpp"
#include "gpu/native_renderer.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(){
 using namespace sims2::display;using Microsoft::WRL::ComPtr;
 const auto require=[](bool ok,const char *why){if(!ok)throw std::runtime_error(why);};
 try{
  for(unsigned cap:{0u,30u,60u,72u,120u,144u,165u,240u}){
   PresentationLimiter limiter;unsigned shown{};
   for(unsigned frame=0;frame<600;++frame)shown+=limiter.due(std::uint64_t(frame)*1001000/60,cap);
   require(cap==30?(shown>=299 && shown<=301):shown==600,"Presentation cap changed native clock cadence");
   require(limiter.due(12000000,cap),"Limiter did not recover from a long interruption");
  }
  sims2::gpu::NativeRenderer renderer(true);auto *device=renderer.device();auto *context=renderer.context();
  OutputSurface surface(nullptr,device,context,nullptr);
  D3D11_TEXTURE2D_DESC desc{};desc.Width=4;desc.Height=1;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const std::uint32_t pixels[]{0xff000000,0xff808080,0xffffffff,0xff0000ff};D3D11_SUBRESOURCE_DATA data{pixels,16,0};
  ComPtr<ID3D11Texture2D> source;require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&source)),"Create HDR test image");
  ComPtr<ID3D11ShaderResourceView> view;require(SUCCEEDED(device->CreateShaderResourceView(source.Get(),nullptr,&view)),"Create HDR test source");
  desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> output;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&output)),"Create HDR float target");
  ComPtr<ID3D11RenderTargetView> target;require(SUCCEEDED(device->CreateRenderTargetView(output.Get(),nullptr,&target)),"Create HDR output view");
  desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
  require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"Create HDR readback");
  for(unsigned peak:{1000u,100u}){
   surface.composite_hdr(view.Get(),target.Get(),4,1,200,peak);context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
   require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map HDR output");const auto *p=static_cast<const float*>(mapped.pData);
   const bool correct=std::abs(p[0])<.001f && std::abs(p[4]-.53965f)<.003f && std::abs(p[8]-(peak==100?1.25f:2.5f))<.003f && std::abs(p[12]-(peak==100?1.25f:2.5f))<.003f && std::abs(p[13])<.001f;
   context->Unmap(staging.Get(),0);require(correct,"GPU HDR transform did not preserve gamma, paper white or peak brightness");
  }
  for(float scale:{.5f,1.5f,3.970588f}){
   psprecomp::GuestMemory memory;sims2::gpu::NativeRenderer scaled(true);scaled.set_render_scale(scale);
   sims2::gpu::DrawCall call{};call.through=true;call.has_vertex_color=true;call.primitive=sims2::gpu::PrimitiveType::Sprites;call.target={0x04000000,512,3,0x04180000,512};call.vertices.resize(2);
   call.vertices[0].position={10,20,0,1};call.vertices[1].position={40,60,0,1};call.vertices[1].color=0xff0000ff;
   scaled.draw(memory,call);scaled.flush(memory);
   require(memory.load32(0x04000000+(30*512+20)*4)==0xff0000ff,"Fractional scale lost native framebuffer data");
   require(scaled.presentation_view(memory,0x04000000,512,3)!=nullptr,"Fractional render target not available for presentation");
  }

  {
   psprecomp::GuestMemory memory;sims2::gpu::NativeRenderer filtered(true);filtered.set_anisotropy(16);
   sims2::gpu::DrawCall call{};call.target={0x04000000,512,3,0x04180000,512};call.primitive=sims2::gpu::PrimitiveType::Triangles;call.vertices.resize(3);
   call.texture.enabled=true;call.texture.address=0x09000000;call.texture.width=call.texture.height=call.texture.buffer_width=2;call.texture.format=sims2::gpu::TextureFormat::Rgba8888;call.texture.min_filter=call.texture.mag_filter=1;
   filtered.draw(memory,call);ComPtr<ID3D11SamplerState> sampler;filtered.context()->PSGetSamplers(0,1,&sampler);D3D11_SAMPLER_DESC desc{};sampler->GetDesc(&desc);
   require(desc.Filter==D3D11_FILTER_ANISOTROPIC && desc.MaxAnisotropy==16,"Anisotropic setting not applied to 3D sampler");
   call.through=true;filtered.draw(memory,call);sampler.Reset();filtered.context()->PSGetSamplers(0,1,&sampler);sampler->GetDesc(&desc);
   require(desc.Filter!=D3D11_FILTER_ANISOTROPIC,"Anisotropic override changed 2D UI sampling");
  }
  std::cout<<"FPS caps, GPU HDR gamma/brightness/clipping and fractional rendering passed\n";return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
