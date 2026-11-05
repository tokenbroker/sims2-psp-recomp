// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#include "modules/utility_modules.hpp"

namespace sims2::modules {

void register_system_parameters(psprecomp::Runtime &runtime, std::int32_t utc_offset_minutes) {
    runtime.register_hle("sceUtility", 0xA5DA2406u,
        [utc_offset_minutes](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto id = cpu.gpr[4], out = cpu.gpr[5];
            if (id < 2u || id > 9u || !rt.memory().contains(out, 4u)) {
                cpu.set_gpr(2u, 0x80110103u); return;
            }
            // USA profile defaults: automatic ad-hoc channel, WLAN saving,
            // month/day/year, 12-hour clock, English, Cross to confirm.
            // UTC offset already includes host daylight saving, so do not add it twice.
            const std::int32_t values[] = {0, 1, 1, 1, utc_offset_minutes, 0, 1, 1};
            rt.memory().store32(out, static_cast<std::uint32_t>(values[id - 2u]));
            cpu.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceUtility", 0x34B78343u,
        [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            constexpr char nickname[] = "Player";
            if (cpu.gpr[4] != 1u || cpu.gpr[6] < sizeof(nickname) ||
                !rt.memory().contains(cpu.gpr[5], sizeof(nickname))) {
                cpu.set_gpr(2u, 0x80110103u); return;
            }
            for (std::uint32_t i = 0; i < sizeof(nickname); ++i) rt.memory().store8(cpu.gpr[5] + i, nickname[i]);
            cpu.set_gpr(2u, 0u);
        });
}

bool UtilityModules::known(std::uint32_t id) noexcept {
    // psputility_modules.h: network, USB, audio/video, NP and IrDA modules.
    return (id >= 0x0100u && id <= 0x0106u) || (id >= 0x0200u && id <= 0x0203u) ||
           (id >= 0x0300u && id <= 0x0307u) || (id >= 0x0400u && id <= 0x0402u) || id == 0x0500u || id == 0x0600u;
}

std::int32_t UtilityModules::load(std::uint32_t id) {
    if (!known(id)) return static_cast<std::int32_t>(kBadModuleId);
    if (!loaded_.insert(id).second) return static_cast<std::int32_t>(kModuleAlreadyLoaded);
    return 0;
}

std::int32_t UtilityModules::unload(std::uint32_t id) {
    if (!known(id)) return static_cast<std::int32_t>(kBadModuleId);
    if (loaded_.erase(id) == 0u) return static_cast<std::int32_t>(kModuleNotLoaded);
    return 0;
}

void register_utility_modules(UtilityModules &modules, psprecomp::Runtime &runtime) {
    // Firmware 2.xx legacy network IDs 1..7 select the same HLE libraries
    // as generic module IDs 0x100..0x106. This tracks loading, not networking.
    runtime.register_hle("sceUtility", 0x1579A159u,
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto id = ctx.gpr[4];
            const auto result = id >= 1u && id <= 7u ?
                static_cast<std::uint32_t>(modules.load(0x100u + id - 1u)) : kBadModuleId;
            ctx.set_gpr(2u, (result & 0xFFFFFF00u) == 0x80111100u ? result - 0x900u : result);
        });
    runtime.register_hle("sceUtility", 0x64D50C56u,
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto id = ctx.gpr[4];
            const auto result = id >= 1u && id <= 7u ?
                static_cast<std::uint32_t>(modules.unload(0x100u + id - 1u)) : kBadModuleId;
            ctx.set_gpr(2u, (result & 0xFFFFFF00u) == 0x80111100u ? result - 0x900u : result);
        });
    runtime.register_hle("sceUtility", 0x2A2B3DE0u,  // sceUtilityLoadModule
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.load(ctx.gpr[4])));
        });
    runtime.register_hle("sceUtility", 0xE49BFE92u,  // sceUtilityUnloadModule
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.unload(ctx.gpr[4])));
        });
}

} // namespace sims2::modules
