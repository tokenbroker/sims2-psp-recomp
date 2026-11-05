#pragma once
#include "psprecomp/allegrex_context.hpp"
namespace psprecomp {
// VT4444/VT5551/VT5650 consume four integer ABGR8888 lanes and write two
// packed lanes. Only source swizzling applies; destination prefix is ignored.
inline void execute_vfpu_color(AllegrexContext &ctx, std::uint32_t destination,
                               std::uint32_t source, std::uint32_t format) noexcept {
    float raw[4]{}; ctx.read_vfpu_vector(raw, source, 4u);
    std::uint32_t pixels[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        const auto color = std::bit_cast<std::uint32_t>(raw[(ctx.vfpu_ctrl[0] >> (2u * i)) & 3u]);
        if (format == 0u) pixels[i] = ((color >> 4) & 0xFu) | ((color >> 8) & 0xF0u) | ((color >> 12) & 0xF00u) | ((color >> 16) & 0xF000u);
        else if (format == 1u) pixels[i] = ((color >> 3) & 0x1Fu) | ((color >> 6) & 0x3E0u) | ((color >> 9) & 0x7C00u) | ((color >> 16) & 0x8000u);
        else pixels[i] = ((color >> 3) & 0x1Fu) | ((color >> 5) & 0x7E0u) | ((color >> 8) & 0xF800u);
    }
    const float packed[]{std::bit_cast<float>(pixels[0] | (pixels[1] << 16)),
                         std::bit_cast<float>(pixels[2] | (pixels[3] << 16))};
    ctx.write_vfpu_vector(packed, destination, 2u); ctx.eat_vfpu_prefixes();
}
}
