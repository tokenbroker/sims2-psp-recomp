// Color equations and pixel packing adapted from Bigmission's MIT psmf_player.cpp.
// See profiles/sims2/THIRD_PARTY.md. Crop and bounds handling added for sceMpeg.
#include "movie/color.hpp"
#include <algorithm>

namespace sims2::movie {
bool convert_picture(const Picture &p, std::uint32_t format, std::uint32_t stride,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    std::span<std::uint8_t> destination) {
    const auto bpp = format == 3u ? 4u : 2u;
    const auto cw = (p.width + 1u) / 2u, ch = (p.height + 1u) / 2u;
    if (format > 3u || !width || !height || x > p.width || width > p.width - x ||
        y > p.height || height > p.height - y || stride < width ||
        p.y.size() < static_cast<std::uint64_t>(p.width) * p.height ||
        p.cb.size() < static_cast<std::uint64_t>(cw) * ch || p.cr.size() < static_cast<std::uint64_t>(cw) * ch ||
        destination.size() < (static_cast<std::uint64_t>(height - 1u) * stride + width) * bpp) return false;
    const auto clamp = [](int value) { return static_cast<std::uint32_t>(std::clamp(value, 0, 255)); };
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t col = 0; col < width; ++col) {
            const auto ci = static_cast<std::size_t>((y + row) / 2u) * cw + (x + col) / 2u;
            const int cb = p.cb[ci] - 128, cr = p.cr[ci] - 128;
            const int luma = p.y[static_cast<std::size_t>(y + row) * p.width + x + col];
            const auto r = clamp(p.full_range ? luma + ((359 * cr) >> 8) : (298 * (luma - 16) + 409 * cr + 128) >> 8);
            const auto g = clamp(p.full_range ? luma - ((88 * cb + 183 * cr) >> 8) : (298 * (luma - 16) - 100 * cb - 208 * cr + 128) >> 8);
            const auto b = clamp(p.full_range ? luma + ((454 * cb) >> 8) : (298 * (luma - 16) + 516 * cb + 128) >> 8);
            std::uint32_t pixel{};
            switch (format) {
            case 0: pixel = (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11); break;
            case 1: pixel = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000u; break;
            case 2: pixel = (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | 0xF000u; break;
            case 3: pixel = r | (g << 8) | (b << 16) | 0xFF000000u; break;
            }
            const auto offset = (static_cast<std::size_t>(row) * stride + col) * bpp;
            for (unsigned byte = 0; byte < bpp; ++byte) destination[offset + byte] = static_cast<std::uint8_t>(pixel >> (byte * 8u));
        }
    }
    return true;
}
}
