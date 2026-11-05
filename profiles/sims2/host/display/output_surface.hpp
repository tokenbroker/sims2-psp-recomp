#pragma once
#include "display/host_settings.hpp"
#include <memory>
struct ID3D11Device;struct ID3D11DeviceContext;struct ID3D11RenderTargetView;struct ID3D11ShaderResourceView;struct ID3D11Texture2D;struct IDXGISwapChain1;
namespace sims2::display {
class OutputSurface {
public:
    OutputSurface(void *window,ID3D11Device *device,ID3D11DeviceContext *context,IDXGISwapChain1 *swapchain);
    ~OutputSurface();
    void configure(unsigned width,unsigned height,const HostSettings &settings);
    ID3D11RenderTargetView *target() const;
    ID3D11Texture2D *capture_texture() const;
    void finish(const HostSettings &settings);
    void leave_fullscreen();
    bool hdr_active() const;
    unsigned display_peak() const;
    const char *status() const;
    // The same GPU transform used for the final HDR swapchain composite.
    void composite_hdr(ID3D11ShaderResourceView *source,ID3D11RenderTargetView *target,unsigned width,unsigned height,unsigned white,unsigned peak);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
