#pragma once
#include "display/host_settings.hpp"
#include <cstdint>
#include <memory>
#include <span>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
namespace sims2::display {
struct SettingsAction {
    bool apply{}, save{};
    HostSettings settings;
};
class SettingsOverlay {
public:
    SettingsOverlay(void *window, ID3D11Device *device, ID3D11DeviceContext *context);
    ~SettingsOverlay();
    std::uint64_t presented_frames() const noexcept;
    void leave_fullscreen();
    void toggle(const HostSettings &current);
    bool open() const noexcept;
    bool handle_message(void *window, unsigned message, std::uintptr_t w, std::intptr_t l);
    SettingsAction present(std::span<const std::uint32_t> pixels, const HostSettings &settings, bool game_frame, ID3D11ShaderResourceView *rendered = nullptr);
    void capture_next_frame(std::string path);
    void status(std::string message);
    void performance(bool enabled);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
