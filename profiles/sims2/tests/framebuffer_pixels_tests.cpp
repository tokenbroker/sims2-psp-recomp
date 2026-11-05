#include "display/framebuffer_pixels.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
int main() {
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("Framebuffer conversion regression"); };
        for (unsigned format = 0; format < 4; ++format) {
            std::array<std::uint8_t, 24> source; source.fill(0xCD);
            const unsigned bpp = format == 3 ? 4 : 2;
            const std::uint32_t red = format == 3 ? 0x7F0000FF : format == 2 ? 0xF00F : format == 1 ? 0x801F : 0x001F;
            const std::uint32_t blue = format == 3 ? 0x00FF0000 : format == 2 ? 0x0F00 : format == 1 ? 0x7C00 : 0xF800;
            for (unsigned y = 0; y < 2; ++y) {
                std::memcpy(source.data() + y * 3 * bpp, &red, bpp);
                std::memcpy(source.data() + (y * 3 + 1) * bpp, &blue, bpp);
            }
            std::array<std::uint32_t, 5> output; output.fill(0xDEADBEEF);
            require(sims2::display::convert_framebuffer_pixels(source, 3, 2, 2, format, output));
            require(output[0] == 0xFF0000 && output[1] == 0xFF && output[2] == output[0] && output[3] == output[1]);
            require(output[4] == 0xDEADBEEF);
            const auto before = output;
            require(!sims2::display::convert_framebuffer_pixels(std::span(source).first(3), 3, 2, 2, format, output));
            require(!sims2::display::convert_framebuffer_pixels(source, 1, 2, 2, format, output));
            require(!sims2::display::convert_framebuffer_pixels(source, 3, 2, 2, 4, output));
            require(output == before);
        }
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
