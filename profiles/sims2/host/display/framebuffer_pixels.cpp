#include "display/framebuffer_pixels.hpp"
#include <cstring>
namespace sims2::display {
namespace {
template <unsigned Format>
void convert(std::span<const std::uint8_t> source, unsigned stride, unsigned width,
    unsigned height, std::span<std::uint32_t> output) {
    constexpr unsigned bpp = Format == 3 ? 4 : 2;
    for (unsigned y = 0; y < height; ++y) {
        const auto *row = source.data() + std::size_t(y) * stride * bpp;
        auto *destination = output.data() + std::size_t(y) * width;
        for (unsigned x = 0; x < width; ++x) {
            std::uint32_t raw = 0; std::memcpy(&raw, row + x * bpp, bpp);
            std::uint32_t r, g, b;
            if constexpr (Format == 3) { r = raw & 255; g = (raw >> 8) & 255; b = (raw >> 16) & 255; }
            else if constexpr (Format == 0) { r = (raw & 31) * 255 / 31; g = ((raw >> 5) & 63) * 255 / 63; b = ((raw >> 11) & 31) * 255 / 31; }
            else if constexpr (Format == 1) { r = (raw & 31) * 255 / 31; g = ((raw >> 5) & 31) * 255 / 31; b = ((raw >> 10) & 31) * 255 / 31; }
            else { r = (raw & 15) * 17; g = ((raw >> 4) & 15) * 17; b = ((raw >> 8) & 15) * 17; }
            destination[x] = (r << 16) | (g << 8) | b;
        }
    }
}
}
bool convert_framebuffer_pixels(std::span<const std::uint8_t> source,
    std::uint32_t stride, std::uint32_t width, std::uint32_t height,
    std::uint32_t format, std::span<std::uint32_t> output) {
    if (!width || !height || stride < width || format > 3 ||
        std::uint64_t(width) * height > output.size() ||
        std::uint64_t(stride) * height * (format == 3 ? 4 : 2) > source.size()) return false;
    switch (format) {
    case 0: convert<0>(source, stride, width, height, output); break;
    case 1: convert<1>(source, stride, width, height, output); break;
    case 2: convert<2>(source, stride, width, height, output); break;
    case 3: convert<3>(source, stride, width, height, output); break;
    }
    return true;
}
}
