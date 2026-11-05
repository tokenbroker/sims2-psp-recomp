#pragma once
#include "movie/avc_decoder.hpp"
#include <span>

namespace sims2::movie {
// Writes a cropped BT.601 picture at destination origin; stride is in pixels.
bool convert_picture(const Picture &picture, std::uint32_t format, std::uint32_t stride,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    std::span<std::uint8_t> destination);
}
