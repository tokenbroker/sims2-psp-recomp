#pragma once
#include "kernel/early_kernel.hpp"
#include <map>
#include <vector>

namespace sims2::kernel {
// Nonblocking FPL subset used by this executable: ordinary user-partition
// pools, try-allocate and delete. No guessed blocking/waiter semantics.
class FixedPools {
public:
    explicit FixedPools(EarlyKernel &kernel) : kernel_(kernel) {}
    void install(psprecomp::Runtime &runtime);
private:
    struct Pool {
        std::int32_t memory_uid;
        std::uint32_t base, stride;
        std::vector<bool> used;
    };
    EarlyKernel &kernel_;
    std::map<std::int32_t, Pool> pools_;
};
}
