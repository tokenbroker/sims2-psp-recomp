#include "kernel/fixed_pool.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <limits>

namespace sims2::kernel {
namespace {
constexpr std::uint32_t unknown_pool = 0x8002019Du;
constexpr std::uint32_t invalid_size = 0x800201B7u;
constexpr std::uint32_t invalid_pointer = 0x800200D3u;
}
void FixedPools::install(psprecomp::Runtime &runtime) {
    runtime.register_hle("ThreadManForUser", 0xC07BB470u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            if (cpu.gpr[5] != 2u || cpu.gpr[6] != 0u || cpu.gpr[9] != 0u) {
                rt.stop("Unmodeled FPL partition, attributes or options"); return;
            }
            const auto size = cpu.gpr[7], count = cpu.gpr[8];
            if (!size || size > 0x7FFFFFF8u || !count || count > 0x7FFFFFFFu) {
                cpu.set_gpr(2u, invalid_size); return;
            }
            const auto stride = (size + 7u) & ~7u;
            if (count > 0x7FFFFFFFu / stride) { cpu.set_gpr(2u, invalid_size); return; }
            std::string name;
            try { name = rt.memory().read_c_string(cpu.gpr[4], 32u); }
            catch (const psprecomp::Error &) { cpu.set_gpr(2u, kNoMemory); return; }
            const auto memory_uid = kernel_.allocate_partition_memory("fpl:" + name, 0u, stride * count, 0u);
            if (memory_uid < 0) { cpu.set_gpr(2u, static_cast<std::uint32_t>(memory_uid)); return; }
            const auto uid = kernel_.allocate_uid();
            pools_.emplace(uid, Pool{memory_uid, kernel_.block_head_address(memory_uid), stride,
                                    std::vector<bool>(count, false)});
            cpu.set_gpr(2u, static_cast<std::uint32_t>(uid));
        });
    runtime.register_hle("ThreadManForUser", 0x623AE665u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = pools_.find(static_cast<std::int32_t>(cpu.gpr[4]));
            if (found == pools_.end()) { cpu.set_gpr(2u, unknown_pool); return; }
            if (!rt.memory().contains(cpu.gpr[5], 4u)) { cpu.set_gpr(2u, invalid_pointer); return; }
            auto &pool = found->second;
            const auto free = std::find(pool.used.begin(), pool.used.end(), false);
            if (free == pool.used.end()) { cpu.set_gpr(2u, kNoMemory); return; }
            const auto index = static_cast<std::uint32_t>(free - pool.used.begin());
            rt.memory().store32(cpu.gpr[5], pool.base + index * pool.stride);
            *free = true;
            cpu.set_gpr(2u, 0u);
        });
    runtime.register_hle("ThreadManForUser", 0xED1410E0u,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto found = pools_.find(static_cast<std::int32_t>(cpu.gpr[4]));
            if (found == pools_.end()) { cpu.set_gpr(2u, unknown_pool); return; }
            const auto result = kernel_.free_partition_memory(found->second.memory_uid);
            pools_.erase(found);
            cpu.set_gpr(2u, static_cast<std::uint32_t>(result));
        });
}
}
