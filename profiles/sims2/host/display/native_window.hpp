#pragma once
#include "display/display.hpp"
#include "ctrl/controller.hpp"
#include "display/host_settings.hpp"
#include <functional>
#include <memory>

namespace sims2::gpu { class NativeRenderer; }
namespace sims2::display {
class NativeWindow {
public:
    explicit NativeWindow(bool visible, gpu::NativeRenderer *renderer = nullptr, bool hidden = false);
    ~NativeWindow();
    // Share the same guest-time deadline for audio output and presentation.
    void synchronize(std::uint64_t now_us);
    bool present(const psprecomp::GuestMemory &memory, const Display::Framebuffer &frame,
        std::uint64_t now_us, ctrl::Controller &controller);
    std::optional<std::string> select_savedata(std::uint32_t mode, const std::vector<std::string> &slots);
    void capture(const std::string &path) const;
    void apply_settings(HostSettings settings);
    HostSettings settings() const;
    std::uint64_t presented_frames() const noexcept;
    void set_audio_control(std::function<void(float)> control);
    void *native_handle() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
