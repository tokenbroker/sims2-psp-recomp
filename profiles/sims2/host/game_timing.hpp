#pragma once
#include "kernel/guest_clock.hpp"
#include "psprecomp/runtime.hpp"
#include "psprecomp/common.hpp"
#include <bit>

namespace sims2 {
struct GameTimingLayout { std::uint32_t getter, context; };
#if defined(SIMS2_EUROPE)
inline constexpr GameTimingLayout game_timing_layout{0x08905D44u, 0x089DF014u};
#else
inline constexpr GameTimingLayout game_timing_layout{0x08937F00u, 0x08A2FA14u};
#endif
// Main verifies the regional executable hash before this additional check of
// the relocated render-context getter. Native installs no replacement.
inline void install_game_timing(psprecomp::Runtime &runtime) {
    if (!kernel::guest_display_hz) return;
    constexpr auto layout = game_timing_layout;
    constexpr std::uint32_t instructions[]{
        0x3C040000u | ((layout.context + 0x8000u) >> 16),
        0x24840000u | (layout.context & 0xFFFFu), 0x03E00008u, 0xC4800048u};
    for (unsigned i = 0; i < 4; ++i)
        if (runtime.memory().load32(layout.getter + i * 4u) != instructions[i])
            throw psprecomp::Error("Regional timing getter signature mismatch");
    runtime.register_function(layout.getter, +[](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
        const float hz = kernel::guest_display_frequency();
        rt.memory().store32(game_timing_layout.context + 0x44u, std::bit_cast<std::uint32_t>(hz));
        rt.memory().store32(game_timing_layout.context + 0x48u, std::bit_cast<std::uint32_t>(1.0f / hz));
        ctx.set_gpr(4u, game_timing_layout.context);
        ctx.fpr[0] = 1.0f / hz;
        ctx.pc = ctx.gpr[31];
    }, "Sims 2 experimental render timestep");
}
}
