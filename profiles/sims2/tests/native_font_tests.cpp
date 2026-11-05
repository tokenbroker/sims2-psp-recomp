#include "font/native_font.hpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned allocations{}, frees{};
void allocate(psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
    ++allocations; cpu.gpr[2] = 0x09000000u; cpu.pc = cpu.gpr[31];
}
void release(psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
    ++frees; cpu.gpr[2] = 0u; cpu.pc = cpu.gpr[31];
}
}
int main() {
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("Native font regression"); };
        psprecomp::Runtime runtime;
        sims2::kernel::EarlyKernel kernel;
        kernel.install(runtime, 0x09100000u, 0x09F00000u);
        auto &cpu = runtime.cpu(); auto &mem = runtime.memory();
        cpu.gpr[29] = 0x09FFF000u;
        kernel.begin_main_thread(cpu, "font test", 0x20u);
        runtime.register_function(0x08801000u, allocate, "test_allocate");
        runtime.register_function(0x08801004u, release, "test_free");
        sims2::font::NativeFont font(kernel); font.install(runtime);
        constexpr auto params = 0x08805000u, error = params + 64u, out = params + 128u;
        mem.zero(params, 44u); mem.store32(params + 4u, 1u);
        mem.store32(params + 12u, 0x08801000u); mem.store32(params + 16u, 0x08801004u);
        cpu.gpr[4] = params; cpu.gpr[5] = error;
        runtime.invoke_import("sceLibFont", 0x67F17ED7u, cpu);
        const auto lib = cpu.gpr[2];
        require(lib == 0x09000000u && mem.load32(error) == 0u && allocations == 1u && !runtime.stopped());
        cpu.gpr[4] = lib; cpu.gpr[5] = error;
        runtime.invoke_import("sceLibFont", 0x27F6E642u, cpu);
        require(cpu.gpr[2] == 1u && mem.load32(error) == 0u);
        cpu.gpr[5] = out; cpu.gpr[6] = 1u;
        runtime.invoke_import("sceLibFont", 0xBC75D85Bu, cpu);
        require(cpu.gpr[2] == 1u && mem.load16(out + 26u) == 2u && mem.read_c_string(out + 32u) != "");
        cpu.gpr[4] = lib; cpu.gpr[5] = 0u; cpu.gpr[6] = 0u; cpu.gpr[7] = error;
        runtime.invoke_import("sceLibFont", 0xA834319Du, cpu);
        const auto handle = cpu.gpr[2]; require(handle > 0u && mem.load32(error) == 0u);
        cpu.gpr[4] = handle; cpu.gpr[5] = 'A'; cpu.gpr[6] = out;
        runtime.invoke_import("sceLibFont", 0xDCC80C2Fu, cpu);
        require(cpu.gpr[2] == 0u && mem.load32(out) > 0u && mem.load32(out + 4u) > 0u);
        constexpr auto image = 0x08806000u, pixels = 0x08807000u;
        mem.zero(image, 24u); mem.store32(image, 2u);
        mem.store16(image + 12u, 32u); mem.store16(image + 14u, 32u);
        mem.store16(image + 16u, 32u); mem.store32(image + 20u, pixels);
        mem.zero(pixels, 1024u); mem.store8(pixels - 1u, 0xA5u); mem.store8(pixels + 1024u, 0xA5u);
        cpu.gpr[6] = image;
        runtime.invoke_import("sceLibFont", 0x980F4895u, cpu);
        require(cpu.gpr[2] == 0u && mem.load8(pixels - 1u) == 0xA5u && mem.load8(pixels + 1024u) == 0xA5u);
        unsigned nonzero = 0; for (unsigned i = 0; i < 1024u; ++i) nonzero += mem.load8(pixels + i) != 0u;
        require(nonzero > 0u && nonzero < 1024u);
        mem.store16(image + 16u, 1u);
        runtime.invoke_import("sceLibFont", 0x980F4895u, cpu);
        require(cpu.gpr[2] == 0x80460003u);
        cpu.gpr[4] = handle;
        runtime.invoke_import("sceLibFont", 0x3AEA8CB6u, cpu);
        require(cpu.gpr[2] == 0u);
        cpu.gpr[4] = lib;
        runtime.invoke_import("sceLibFont", 0x574B6FBCu, cpu);
        require(cpu.gpr[2] == 0u && frees == 1u);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
