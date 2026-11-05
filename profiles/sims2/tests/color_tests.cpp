#include "movie/color.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
int main() {
    try {
        using namespace sims2::movie;
        const auto require = [](bool value) { if (!value) throw std::runtime_error("Movie color conversion regression"); };
        Picture p{2u, 2u, false, {16u, 235u, 81u, 145u}, {128u}, {128u}};
        std::array<std::uint8_t, 24> pixels; pixels.fill(0xCDu);
        require(convert_picture(p, 3u, 3u, 0u, 0u, 2u, 2u, pixels));
        require(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255);
        require(pixels[4] == 255 && pixels[5] == 255 && pixels[6] == 255 && pixels[7] == 255);
        require(pixels[8] == 0xCDu && pixels[11] == 0xCDu && pixels[20] == 0xCDu);
        std::array<std::uint8_t, 4> crop{};
        require(convert_picture(p, 3u, 1u, 1u, 0u, 1u, 1u, crop) && crop[0] == 255);
        for (unsigned format = 0; format < 3; ++format) {
            require(convert_picture(p, format, 1u, 1u, 0u, 1u, 1u, crop));
            require(crop[0] == 255 && crop[1] == 255);
        }
        const auto before = pixels;
        require(!convert_picture(p, 3u, 1u, 0u, 0u, 2u, 2u, pixels));
        require(!convert_picture(p, 3u, 3u, 2u, 0u, 1u, 1u, pixels));
        require(!convert_picture(p, 3u, 3u, 0u, 0u, 2u, 2u, std::span(pixels).first(19)));
        require(pixels == before);
        p.full_range = true; p.y = {255, 0, 0, 0}; p.cb = {0}; p.cr = {255};
        require(convert_picture(p, 3u, 1u, 0u, 0u, 1u, 1u, crop));
        require(crop[0] == 255 && crop[2] == 28 && crop[3] == 255);
        p.cb.clear(); require(!convert_picture(p, 3u, 1u, 0u, 0u, 1u, 1u, crop));
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
