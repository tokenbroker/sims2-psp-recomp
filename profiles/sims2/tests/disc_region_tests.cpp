#include "display/disc_region.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

int main(int argc, char **argv) {
    using sims2::display::european_disc;
    try {
        if (argc == 2) { std::cout << (european_disc(argv[1]) ? "Europe" : "USA") << '\n'; return 0; }
        const auto path = std::filesystem::temp_directory_path() /
            ("sims2-disc-region-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".iso");
        auto fixture = [](const std::string &identity) {
            std::vector<std::uint8_t> image(19 * 2048);
            auto put32 = [&](std::size_t offset, std::uint32_t value) {
                for (unsigned i = 0; i < 4; ++i) image[offset + i] = std::uint8_t(value >> (8 * i));
            };
            const auto pvd = 16 * 2048;
            image[pvd] = 1; image[pvd + 6] = 1;
            std::copy_n("CD001", 5, image.begin() + pvd + 1);
            image[pvd + 156] = 34;
            put32(pvd + 158, 17); put32(pvd + 166, 2048);
            const std::string name = "UMD_DATA.BIN;1";
            const auto root = 17 * 2048;
            image[root] = 48; image[root + 32] = std::uint8_t(name.size());
            put32(root + 2, 18); put32(root + 10, std::uint32_t(identity.size()));
            std::copy(name.begin(), name.end(), image.begin() + root + 33);
            std::copy(identity.begin(), identity.end(), image.begin() + 18 * 2048);
            return image;
        };
        auto write = [&](const auto &image) {
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char *>(image.data()), std::streamsize(image.size()));
        };
        write(fixture("ULUS-10031|synthetic"));
        if (european_disc(path)) throw std::runtime_error("USA routed to Europe");
        write(fixture("ULES-00211|synthetic"));
        if (!european_disc(path)) throw std::runtime_error("Europe routed to USA");
        const auto reject = [&] {
            try { (void)european_disc(path); } catch (const std::exception &) { return; }
            throw std::runtime_error("Invalid disc was accepted");
        };
        write(fixture("OTHER-0000|synthetic")); reject();
        auto broken = fixture("ULES-00211|synthetic");
        broken[17 * 2048] = 3; write(broken); reject();
        broken.resize(100); write(broken); reject();
        std::filesystem::remove(path);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
