#include "kernel/fixed_pool.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("FPL regression"); };
        psprecomp::Runtime runtime;
        sims2::kernel::EarlyKernel kernel;
        kernel.install(runtime, 0x09000000u, 0x09100000u);
        sims2::kernel::FixedPools pools(kernel);
        pools.install(runtime);
        auto &cpu = runtime.cpu();
        constexpr auto text = 0x08805000u, out = text + 64u;
        runtime.memory().store32(text, 'f');
        const auto initial = kernel.max_free_memory();
        const auto create = [&](std::uint32_t size, std::uint32_t blocks) {
            cpu.gpr[4] = text; cpu.gpr[5] = 2u; cpu.gpr[6] = 0u;
            cpu.gpr[7] = size; cpu.gpr[8] = blocks; cpu.gpr[9] = 0u;
            runtime.invoke_import("ThreadManForUser", 0xC07BB470u, cpu);
            return cpu.gpr[2];
        };
        const auto uid = create(25u, 2u);
        require(static_cast<std::int32_t>(uid) > 0 && kernel.max_free_memory() < initial);
        cpu.gpr[4] = uid; cpu.gpr[5] = out;
        runtime.invoke_import("ThreadManForUser", 0x623AE665u, cpu);
        require(cpu.gpr[2] == 0u);
        const auto first = runtime.memory().load32(out);
        runtime.invoke_import("ThreadManForUser", 0x623AE665u, cpu);
        require(cpu.gpr[2] == 0u && runtime.memory().load32(out) == first + 32u);
        runtime.invoke_import("ThreadManForUser", 0x623AE665u, cpu);
        require(cpu.gpr[2] != 0u && runtime.memory().load32(out) == first + 32u);
        runtime.invoke_import("ThreadManForUser", 0xED1410E0u, cpu);
        require(cpu.gpr[2] == 0u && kernel.max_free_memory() == initial);
        runtime.invoke_import("ThreadManForUser", 0x623AE665u, cpu);
        require(cpu.gpr[2] == 0x8002019Du);
        require(create(0xFFFFFFFFu, 2u) == 0x800201B7u && kernel.max_free_memory() == initial);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
