#include "kernel/volatile_memory.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    try {
        const auto require = [](bool value) { if (!value) throw std::runtime_error("Volatile memory regression"); };
        psprecomp::Runtime runtime; sims2::kernel::EarlyKernel kernel;
        kernel.install(runtime, 0x08804000u, 0x09FF6000u);
        auto &cpu = runtime.cpu(); cpu.pc = 0x08804444u; kernel.begin_main_thread(cpu, "main", 0x20u);
        const auto main = kernel.current_uid();
        sims2::kernel::VolatileMemory memory(kernel); memory.install(runtime);
        const auto call = [&](std::uint32_t nid, std::uint32_t mode, std::uint32_t address, std::uint32_t size) {
            cpu.gpr[4] = mode; cpu.gpr[5] = address; cpu.gpr[6] = size; cpu.gpr[31] = 0x08804500u;
            runtime.invoke_import("sceSuspendForUser", nid, cpu); return cpu.gpr[2];
        };
        constexpr auto address = 0x08806000u, size = address + 4u;
        require(call(0xA14F40B2u, 1, address, size) == 0x80000107u);
        require(call(0xA14F40B2u, 0, address, size) == 0u);
        require(runtime.memory().load32(address) == 0x08400000u && runtime.memory().load32(size) == 0x00400000u);
        runtime.memory().store32(address, 0xDEADBEEFu);
        require(call(0xA14F40B2u, 0, address, size) == 0x802B0200u && runtime.memory().load32(address) == 0xDEADBEEFu);
        const auto worker = kernel.create_thread("worker", 0x08805000u, 0x30u, 0x400u, 0, 0);
        require(kernel.start_thread(cpu, worker, 0, 0) == 0);
        call(0x3E0271D3u, 0, address, size);
        require(kernel.current_uid() == worker);
        require(call(0xA569E425u, 0, 0, 0) == 0u && kernel.current_uid() == main);
        require(cpu.pc == 0x08804500u && runtime.memory().load32(address) == 0x08400000u);
        require(call(0xA14F40B2u, 0, 0, 0) == 0x802B0200u);
        require(call(0xA569E425u, 0, 0, 0) == 0u);
        require(call(0xA569E425u, 0, 0, 0) == 0x800201AEu);
        require(call(0xA14F40B2u, 0, 0, 0) == 0u);
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
