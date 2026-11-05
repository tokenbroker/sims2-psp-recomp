#include "modules/module_manager.hpp"
#include "synthetic_iso.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

int main() {
    auto image = sims2::test::synthetic_image();
    const auto offset = 21u * sims2::umd::kSectorSize;
    std::fill(image.begin() + offset, image.begin() + offset + 3000, 0u);
    const std::string header = "~PSP", name = "sceAudiocodec_Driver";
    std::copy(header.begin(), header.end(), image.begin() + offset);
    std::copy(name.begin(), name.end(), image.begin() + offset + 10u);
    const auto path = sims2::test::write_image(image, "sims2_module_manager.iso");
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("Module manager regression"); };
        {
            sims2::umd::DiscImage disc(path);
            psprecomp::Runtime runtime;
            sims2::kernel::EarlyKernel kernel;
            kernel.install(runtime, 0x09000000u, 0x09100000u);
            sims2::io::IoManager io(disc, runtime);
            sims2::modules::ModuleManager modules(kernel, io);
            const auto uid = modules.load(runtime, "disc0:/sce_lbn0x15_size0xbb8");
            require(uid > 0 && !runtime.stopped());
            require(modules.start(uid) == uid);
            require(static_cast<std::uint32_t>(modules.start(uid)) == sims2::modules::kModuleAlreadyStarted);
            require(modules.stop(uid) == 0 && modules.unload(uid) == uid);
            require(static_cast<std::uint32_t>(modules.start(uid)) == sims2::modules::kUnknownModule);
            require(modules.load(runtime, "disc0:/UMD_DATA.BIN") < 0 && runtime.stopped());
        }
        std::filesystem::remove(path);
        return 0;
    } catch (const std::exception &e) {
        std::filesystem::remove(path);
        std::cerr << e.what() << '\n'; return 1;
    }
}
