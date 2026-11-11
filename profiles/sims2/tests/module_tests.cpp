// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#include "modules/utility_modules.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sims2::modules;
    UtilityModules modules;
    require(modules.load(0x300u) == 0 && modules.loaded(0x300u), "AV codec load");
    require(static_cast<std::uint32_t>(modules.load(0x300u)) == kModuleAlreadyLoaded, "double load accepted");
    require(static_cast<std::uint32_t>(modules.load(0x308u)) == kBadModuleId &&
                static_cast<std::uint32_t>(modules.load(0x0FFu)) == kBadModuleId,
            "unknown module accepted");
    require(static_cast<std::uint32_t>(modules.unload(0x301u)) == kModuleNotLoaded, "unload of unloaded module");

    psprecomp::Runtime runtime;
    register_utility_modules(modules, runtime);
    auto &cpu = runtime.cpu();
    cpu.set_gpr(4u, 0x301u);
    runtime.invoke_import("sceUtility", 0x2A2B3DE0u, cpu);
    require(cpu.gpr[2] == 0u && modules.loaded(0x301u), "HLE load");
    runtime.invoke_import("sceUtility", 0xE49BFE92u, cpu);
    require(cpu.gpr[2] == 0u && !modules.loaded(0x301u), "HLE unload");
    cpu.gpr[4] = 1u;
    runtime.invoke_import("sceUtility", 0x1579A159u, cpu);
    require(cpu.gpr[2] == 0u && modules.loaded(0x100u), "legacy network module ID");
    runtime.invoke_import("sceUtility", 0x64D50C56u, cpu);
    require(cpu.gpr[2] == 0u && !modules.loaded(0x100u), "legacy network unload");
    runtime.invoke_import("sceUtility", 0x64D50C56u, cpu);
    require(cpu.gpr[2] == 0x80110803u, "legacy network error namespace");
    register_system_parameters(runtime, -360);
    cpu.gpr[4] = 8u; cpu.gpr[5] = 0x08805000u;
    runtime.invoke_import("sceUtility", 0xA5DA2406u, cpu);
    require(cpu.gpr[2] == 0u && runtime.memory().load32(cpu.gpr[5]) == 1u, "USA English default");
    cpu.gpr[4] = 6u;
    runtime.invoke_import("sceUtility", 0xA5DA2406u, cpu);
    require(cpu.gpr[2] == 0u && static_cast<std::int32_t>(runtime.memory().load32(cpu.gpr[5])) == -360,
            "signed timezone value");
    cpu.gpr[4] = 0u;
    runtime.invoke_import("sceUtility", 0xA5DA2406u, cpu);
    require(cpu.gpr[2] == 0x80110103u, "unknown system parameter");
    for (unsigned language = 1; language <= 5; ++language) {
        psprecomp::Runtime regional;
        register_system_parameters(regional, 0, language);
        auto &context = regional.cpu();
        context.gpr[4] = 8; context.gpr[5] = 0x08805000u;
        regional.invoke_import("sceUtility", 0xA5DA2406u, context);
        require(context.gpr[2] == 0 && regional.memory().load32(0x08805000u) == language,
                "regional PSP language selection");
    }
    return 0;
}
