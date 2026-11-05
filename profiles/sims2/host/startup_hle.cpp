// Adapted from bigmission-recomp (MIT). See ../THIRD_PARTY.md in the profile root.
#include "startup_hle.hpp"

namespace sims2 {

void StartupHle::install(psprecomp::Runtime &runtime) {
    // The diagnostic host has no suspend transition. Retain balanced lock
    // ownership; unknown modes stop instead of claiming unsupported behavior.
    runtime.register_hle("sceSuspendForUser", 0xEADB1BD7u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            if (ctx.gpr[4] != 0u || power_locks_ == 0xFFFFFFFFu) {
                rt.stop("Unmodeled sceKernelPowerLock mode or lock overflow"); return;
            }
            ++power_locks_;
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceSuspendForUser", 0x3AEE7261u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            if (ctx.gpr[4] != 0u || power_locks_ == 0u) {
                rt.stop("Unmodeled sceKernelPowerUnlock mode or unbalanced unlock"); return;
            }
            --power_locks_;
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("SysMemUserForUser", 0x7591C7DBu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            compiled_sdk_version_ = ctx.gpr[4];
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("SysMemUserForUser", 0xF77D77CBu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            compiler_version_ = ctx.gpr[4];
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("UtilsForUser", 0x6AD345D7u,  // sceKernelSetGPO
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            gpo_ = ctx.gpr[4] & 0xFFu;
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceSuspendForUser", 0x090CCB3Fu,  // sceKernelPowerTick(type)
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ++power_ticks_;
            ctx.set_gpr(2u, 0u);
        });
    const auto cache_writeback = [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
        rt.memory().memory_barrier(); // Guest RAM and GE views share coherent host storage.
        ctx.set_gpr(2u, 0u);
    };
    runtime.register_hle("UtilsForUser", 0x79D1C3FAu, cache_writeback);  // sceKernelDcacheWritebackAll
    runtime.register_hle("UtilsForUser", 0xB435DEC5u, cache_writeback);  // sceKernelDcacheWritebackInvalidateAll
    runtime.register_hle("UtilsForUser", 0x3EE30821u, cache_writeback);  // sceKernelDcacheWritebackRange
    runtime.register_hle("UtilsForUser", 0x34B9FA9Eu, cache_writeback);  // sceKernelDcacheWritebackInvalidateRange
}

} // namespace sims2
