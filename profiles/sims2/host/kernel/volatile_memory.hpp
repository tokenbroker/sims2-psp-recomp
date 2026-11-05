#pragma once
#include "kernel/early_kernel.hpp"
#include <deque>
namespace sims2::kernel {
class VolatileMemory {
public:
    explicit VolatileMemory(EarlyKernel &kernel) : kernel_(kernel), object_(kernel.allocate_uid()) {}
    void install(psprecomp::Runtime &runtime);
private:
    struct Waiter { ThreadUid uid; std::uint32_t address_out, size_out; };
    EarlyKernel &kernel_;
    std::uint32_t object_;
    bool locked_{};
    std::deque<Waiter> waiters_;
    void lock(psprecomp::AllegrexContext &cpu, bool try_only);
    void unlock(psprecomp::AllegrexContext &cpu);
    void outputs(std::uint32_t address, std::uint32_t size);
};
}
