#pragma once
#include "display/host_settings.hpp"

namespace sims2::display {
// Each install keeps its primary runtime at the root. Optional companions have
// their own DLL directory, preventing one backend's dependencies replacing another's.
inline std::filesystem::path selected_runtime(const std::filesystem::path &folder,
                                              GraphicsBackend selected, GraphicsBackend primary) {
    if(selected==GraphicsBackend::Default || selected==primary)return folder/"sims2_probe.exe";
    return folder/(selected==GraphicsBackend::Vulkan?"vulkan":"d3d11")/"sims2_probe.exe";
}
}
