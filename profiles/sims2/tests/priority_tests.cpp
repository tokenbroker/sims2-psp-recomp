#include "kernel/early_kernel.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("Priority regression"); };
        psprecomp::Runtime runtime;
        sims2::kernel::EarlyKernel kernel;
        kernel.install(runtime, 0x09000000u, 0x09100000u);
        auto &cpu = runtime.cpu();
        cpu.pc = 0x08805000u; cpu.gpr[29] = 0x09FFF000u;
        kernel.begin_main_thread(cpu, "main", 0x20u);
        const auto main = kernel.current_uid();
        const auto worker = kernel.create_thread("worker", 0x08806000u, 0x30u, 0x400u, 0u, 0u);
        require(worker > 0 && kernel.start_thread(cpu, worker, 0u, 0u) == 0 && kernel.current_uid() == main);
        cpu.gpr[31] = 0x08805100u;
        kernel.change_priority(cpu, 0, 0x40u);
        require(kernel.current_uid() == worker && cpu.pc == 0x08806000u);
        require(kernel.thread(main)->context.pc == 0x08805100u && kernel.thread(main)->context.gpr[2] == 0u);
        kernel.change_priority(cpu, main, 0u);
        require(cpu.gpr[2] == sims2::kernel::kIllegalPriority && kernel.thread(main)->priority == 0x40u);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
