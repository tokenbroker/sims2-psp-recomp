#pragma once
#include "gpu/ge_state.hpp"
#include <memory>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
namespace sims2::gpu {
// D3D11 backend with optional internal scaling. Framebuffer flushes retain
// native PSP memory for CPU reads, GE transfers and texture feedback.
class NativeRenderer {
public:
    explicit NativeRenderer(bool warp = false);
    ~NativeRenderer();
    void draw(psprecomp::GuestMemory &memory, const DrawCall &call);
    // Writes every rendered framebuffer back to guest memory before returning.
    void flush(psprecomp::GuestMemory &memory);
    // End of a game frame that displays the given framebuffer. Rendered pixels
    // may reach guest memory after this returns (Vulkan); they are complete
    // before a transfer, a framebuffer texture or a later draw uses them, and
    // after flush().
    void flush_frame(psprecomp::GuestMemory &memory, std::uint32_t address, unsigned stride, unsigned format);
    // Resolve only GPU targets read or partially overwritten by this transfer.
    void prepare_transfer(psprecomp::GuestMemory &memory, const BlockTransfer &transfer);
    [[nodiscard]] std::uint64_t framebuffer_readbacks() const noexcept;
    // wait_ns: time blocked until the GPU finished a readback (Vulkan).
    struct ProfileStats { std::uint64_t draw_ns, flush_ns, vertex_ns, wait_ns; };
    [[nodiscard]] ProfileStats profile_stats() const noexcept;
    [[nodiscard]] std::uint64_t draws() const noexcept;
    void set_render_scale(float scale);
    float render_scale() const noexcept;
    // Correct full-screen world/HUD framing; pass 480/272 to restore native.
    void set_anisotropy(unsigned level);
    void set_display_aspect(float aspect);
    // Borrowed until the next draw/settings change. Null when CPU pixels differ.
    ID3D11ShaderResourceView *presentation_view(const psprecomp::GuestMemory &memory,
        std::uint32_t address, unsigned stride, unsigned format) const;
    // Portable Vulkan backend handles (SDL_GPU); null on the D3D11 backend.
#if defined(SIMS2_VULKAN)
    void *gpu_device() const noexcept;
    // Opaque display copy: PSP framebuffer alpha remains available to the game.
    void *gpu_presentation(const psprecomp::GuestMemory &, std::uint32_t, unsigned, unsigned) const;
    std::array<unsigned,2> gpu_dimensions() const noexcept;
    [[nodiscard]] std::uint64_t gpu_submissions() const noexcept;
#endif
    ID3D11Device *device() const noexcept;
    ID3D11DeviceContext *context() const noexcept;
private:
    void flush(psprecomp::GuestMemory &memory, const BlockTransfer *transfer);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
