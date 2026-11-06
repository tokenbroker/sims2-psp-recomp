#pragma once
#include "kernel/guest_clock.hpp"
#include "psprecomp/runtime.hpp"
#include <bit>

namespace sims2 {
// ULUS-10031 only; main verifies the executable hash before installing this.
// The render-context period getter feeds the application's tick quantizer.
// Updating both fields preserves the reciprocal used by its other getter and
// by the engine's conversion between ticks and seconds. Native mode installs
// no replacement at all. See docs/HIGH_FPS_RESEARCH.md for measured coverage.
inline void install_game_timing(psprecomp::Runtime &runtime) {
    if (!kernel::guest_display_hz) return;
    runtime.register_function(0x08937F00u, +[](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
        const float hz = kernel::guest_display_frequency();
        rt.memory().store32(0x08A2FA58u, std::bit_cast<std::uint32_t>(hz));
        rt.memory().store32(0x08A2FA5Cu, std::bit_cast<std::uint32_t>(1.0f / hz));
        ctx.set_gpr(4u, 0x08A2FA14u);
        ctx.fpr[0] = 1.0f / hz;
        ctx.pc = ctx.gpr[31];
    }, "Sims 2 experimental render timestep");
}
}
