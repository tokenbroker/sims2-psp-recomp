#pragma once
#include <cstdint>
#include <span>
namespace sims2::display {
// PSP little-endian pixels to the BGR words consumed by a Windows DIB.
bool convert_framebuffer_pixels(std::span<const std::uint8_t> source,
    std::uint32_t stride, std::uint32_t width, std::uint32_t height,
    std::uint32_t format, std::span<std::uint32_t> output);
}
