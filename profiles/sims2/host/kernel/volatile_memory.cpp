#include "kernel/volatile_memory.hpp"
namespace sims2::kernel {
namespace { constexpr std::uint32_t invalid_mode = 0x80000107u, invalid_pointer = 0x80000103u;
constexpr std::uint32_t busy = 0x802B0200u, not_locked = 0x800201AEu; }
void VolatileMemory::outputs(std::uint32_t address, std::uint32_t size) {
    auto &memory = kernel_.runtime().memory();
    if (address) memory.store32(address, 0x08400000u);
    if (size) memory.store32(size, 0x00400000u);
}
void VolatileMemory::lock(psprecomp::AllegrexContext &cpu, bool try_only) {
    if (cpu.gpr[4]) { cpu.gpr[2] = invalid_mode; return; }
    const auto address = cpu.gpr[5], size = cpu.gpr[6]; auto &memory = kernel_.runtime().memory();
    if ((address && !memory.contains(address, 4u)) || (size && !memory.contains(size, 4u))) {
        cpu.gpr[2] = invalid_pointer; return;
    }
    if (!try_only && (kernel_.in_interrupt() || !kernel_.dispatch_enabled() || !kernel_.interrupts_enabled())) {
        outputs(address, size); cpu.gpr[2] = kernel_.in_interrupt() ? 0x80020064u : kCanNotWait; return;
    }
    if (!locked_) { outputs(address, size); locked_ = true; cpu.gpr[2] = 0; return; }
    if (try_only) { cpu.gpr[2] = busy; return; }
    waiters_.push_back({kernel_.current_uid(), address, size});
    kernel_.block_current(cpu, {"volatile memory", kNoDeadline, 0u, {}, 0, false,
        {EarlyKernel::WaitType::Custom, object_}});
}
void VolatileMemory::unlock(psprecomp::AllegrexContext &cpu) {
    if (cpu.gpr[4]) { cpu.gpr[2] = invalid_mode; return; }
    if (!locked_) { cpu.gpr[2] = not_locked; return; }
    locked_ = false;
    while (!waiters_.empty()) {
        const auto waiter = waiters_.front(); waiters_.pop_front();
        const auto *thread = kernel_.thread(waiter.uid);
        if (!thread || thread->state != EarlyKernel::ThreadState::Waiting ||
            thread->wait_identity != EarlyKernel::WaitIdentity{EarlyKernel::WaitType::Custom, object_}) continue;
        outputs(waiter.address_out, waiter.size_out);
        if (kernel_.release_wait(waiter.uid, 0u)) { locked_ = true; break; }
    }
    kernel_.return_with_preemption(cpu, 0u);
}
void VolatileMemory::install(psprecomp::Runtime &runtime) {
    runtime.register_hle("sceSuspendForUser", 0xA14F40B2u, [this](auto &, auto &cpu) { lock(cpu, true); });
    runtime.register_hle("sceSuspendForUser", 0x3E0271D3u, [this](auto &, auto &cpu) { lock(cpu, false); });
    runtime.register_hle("sceSuspendForUser", 0xA569E425u, [this](auto &, auto &cpu) { unlock(cpu); });
}
}
