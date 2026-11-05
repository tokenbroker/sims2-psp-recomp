// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#include "modules/module_manager.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <array>

namespace sims2::modules {

ModuleManager::ModuleManager(kernel::EarlyKernel &kernel, io::IoManager &io)
    : kernel_(&kernel), io_(&io), main_uid_(kernel.allocate_uid()) {
    modules_.emplace(main_uid_, Module{"main", true});
}

std::int32_t ModuleManager::load(psprecomp::Runtime &runtime, const std::string &path) {
    const auto bytes = io_->read_disc_file(path, 1024u * 1024u);
    std::string name;
    if (bytes && bytes->size() >= 38u && (*bytes)[0] == '~' && (*bytes)[1] == 'P' &&
        (*bytes)[2] == 'S' && (*bytes)[3] == 'P') {
        for (std::size_t i = 10; i < 38 && (*bytes)[i] != 0; ++i) name += static_cast<char>((*bytes)[i]);
    }
    // Firmware code is represented by HLE exports, never decrypted or executed.
    // Unknown/game-owned modules remain explicit blockers.
    static constexpr std::array<std::string_view, 7> firmware_names{
        "sceAudiocodec_Driver", "sceMpegbase_Driver", "sceSAScore", "sceVideocodec_Driver",
        "sceATRAC3plus_Library", "sceFont_Library", "sceMpeg_library"};
    if (std::find(firmware_names.begin(), firmware_names.end(), name) == firmware_names.end()) {
        runtime.stop("Unmodeled PRX '" + name + "' from " + path);
        return static_cast<std::int32_t>(kUnknownModule);
    }
    const std::int32_t uid = kernel_->allocate_uid();
    modules_.emplace(uid, Module{name, false});
    return uid;
}

std::int32_t ModuleManager::start(std::int32_t uid) {
    const auto found = modules_.find(uid);
    if (found == modules_.end()) return static_cast<std::int32_t>(kUnknownModule);
    if (found->second.started) return static_cast<std::int32_t>(kModuleAlreadyStarted);
    found->second.started = true;
    return uid;
}

std::int32_t ModuleManager::stop(std::int32_t uid) {
    const auto found = modules_.find(uid);
    if (found == modules_.end() || uid == main_uid_) return static_cast<std::int32_t>(kUnknownModule);
    if (!found->second.started) return static_cast<std::int32_t>(kModuleNotStarted);
    found->second.started = false;
    return 0;
}

std::int32_t ModuleManager::unload(std::int32_t uid) {
    const auto found = modules_.find(uid);
    if (found == modules_.end() || uid == main_uid_) return static_cast<std::int32_t>(kUnknownModule);
    modules_.erase(found);
    return uid;  // Success returns the module ID; the game treats <= 0 as failure.
}

void register_module_manager(ModuleManager &modules, psprecomp::Runtime &runtime) {
    const auto status = [](psprecomp::Runtime &rt, std::uint32_t address) {
        if (address != 0u && rt.memory().contains(address, 4u)) rt.memory().store32(address, 0u);
    };
    runtime.register_hle("ModuleMgrForUser", 0x977DE386u,  // sceKernelLoadModule(path, flags, option)
        [&modules](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::string path;
            try { path = rt.memory().read_c_string(ctx.gpr[4], 256u); }
            catch (const psprecomp::Error &) { ctx.set_gpr(2u, kUnknownModule); return; }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.load(rt, path)));
        });
    runtime.register_hle("ModuleMgrForUser", 0x50F0C1ECu,  // sceKernelStartModule(uid, argsize, argp, *status, option)
        [&modules, status](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto result = modules.start(static_cast<std::int32_t>(ctx.gpr[4]));
            if (result >= 0) status(rt, ctx.gpr[7]);
            ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
        });
    runtime.register_hle("ModuleMgrForUser", 0xD1FF982Au,  // sceKernelStopModule(uid, argsize, argp, *status, option)
        [&modules, status](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto result = modules.stop(static_cast<std::int32_t>(ctx.gpr[4]));
            if (result >= 0) status(rt, ctx.gpr[7]);
            ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
        });
    runtime.register_hle("ModuleMgrForUser", 0x2E0911AAu,  // sceKernelUnloadModule(uid)
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.unload(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("ModuleMgrForUser", 0xF0A26395u,  // sceKernelGetModuleId()
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.main_module()));
        });
    runtime.register_hle("ModuleMgrForUser", 0xD8B73127u,  // sceKernelGetModuleIdByAddress(address)
        [&modules](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(modules.main_module()));
        });
    runtime.register_hle("ModuleMgrForUser", 0x8F2DF740u,  // sceKernelStopUnloadSelfModuleWithStatus
        [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            rt.stop("The game unloaded itself (exit status " + psprecomp::hex32(ctx.gpr[5]) + ")");
        });
}

} // namespace sims2::modules
