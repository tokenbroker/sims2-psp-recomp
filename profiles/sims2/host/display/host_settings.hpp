#pragma once
#include <cstdint>
#include "ctrl/input_config.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace sims2::display {
enum class WindowMode { Windowed, Borderless, Fullscreen };
enum class AspectMode { Native, Widescreen, Stretch, Ultrawide, FourThree, SixteenTen, SuperUltrawide };
enum class HdrMode { Off, Auto, On };
enum class OutputFilter { Nearest, Linear };
struct HostSettings {
    ctrl::InputConfig input;
    WindowMode mode = WindowMode::Windowed;
    unsigned width = 1920, height = 1080;
    AspectMode aspect = AspectMode::Native;
    OutputFilter filter = OutputFilter::Nearest;
    unsigned render_scale = 4;
    unsigned render_percent = 0; // zero selects the fixed PSP multiplier
    unsigned fps_cap = 0; // presentation only; native guest timing is unchanged
    unsigned anisotropy = 0;
    HdrMode hdr = HdrMode::Off;
    unsigned paper_white = 200, peak_nits = 0;
    bool match_camera_aspect = false;
    bool vsync = true;
    unsigned stick_deadzone = 18;
    bool right_stick_camera = false;
    unsigned volume = 100;
    bool muted = false;
    bool show_performance = false;
    bool operator==(const HostSettings &) const = default;
};
struct PictureRect { unsigned x{}, y{}, width{}, height{}; };
std::string_view aspect_name(AspectMode mode);
float effective_render_scale(const HostSettings &settings, unsigned output_height);
HostSettings validated_settings(HostSettings settings);
PictureRect picture_rect(unsigned width, unsigned height, AspectMode mode);
std::filesystem::path host_settings_path();
HostSettings load_host_settings(const std::filesystem::path &path);
bool save_host_settings(const std::filesystem::path &path, HostSettings settings, std::string &error);
}
