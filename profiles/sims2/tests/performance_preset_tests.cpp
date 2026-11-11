#include "display/performance_presets.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace sims2::display;
    try {
        HostSettings original;
        original.volume = 37;
        original.muted = true;
        original.input.keys[0] = 'V';
        original.graphics_backend = GraphicsBackend::Vulkan;
        original.aspect = AspectMode::SixteenTen;
        original.render_percent = 200;
        original.engine_fps = 240;
        auto preset = handheld_preset(original);
        if (preset.input != original.input || preset.volume != original.volume ||
            preset.muted != original.muted || preset.graphics_backend != original.graphics_backend ||
            preset.aspect != original.aspect)
            throw std::runtime_error("Preset changed player controls/audio/backend/aspect");
        if (effective_render_scale(preset, 2160) != 2.0f || preset.engine_fps != 0)
            throw std::runtime_error("Existing percentage scale/high engine rate defeated low-load preset");
        // Persisting and reopening must keep the preset and the player's other settings.
        const auto path = std::filesystem::temp_directory_path() / "sims2-preset-regression.ini";
        std::string error;
        if (!save_host_settings(path, preset, error)) throw std::runtime_error(error);
        const auto loaded = load_host_settings(path);
        std::filesystem::remove(path);
        if (loaded != preset) throw std::runtime_error("Preset did not survive Save/reload");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
