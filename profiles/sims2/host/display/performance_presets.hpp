#pragma once
#include "display/host_settings.hpp"

namespace sims2::display {
// Only graphics/performance choices change. Keep player input, sound, aspect,
// window mode and renderer choice; switching a renderer is a separate action.
inline HostSettings handheld_preset(HostSettings settings) {
    settings.width = 1280;
    settings.height = 720;
    settings.render_scale = 2;
    settings.render_percent = 0;
    settings.engine_fps = 0;
    settings.fps_cap = 60;
    settings.vsync = true;
    settings.anisotropy = 0;
    settings.hdr = HdrMode::Off;
    return settings;
}
}
