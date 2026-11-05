// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#pragma once

#include "kernel/early_kernel.hpp"
#include "io/io_manager.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace sims2::modules {

inline constexpr std::uint32_t kUnknownModule = 0x8002012Eu;
inline constexpr std::uint32_t kModuleAlreadyStarted = 0x80020133u;
inline constexpr std::uint32_t kModuleNotStarted = 0x80020134u;

// Firmware modules are identified from PSP headers in the verified disc's
// sector extents. Loading creates module lifecycle state; each export still
// requires HLE. Unknown or game-owned modules stop explicitly.
class ModuleManager {
public:
    ModuleManager(kernel::EarlyKernel &kernel, io::IoManager &io);

    // A module ID, or nullopt-equivalent negative error; stops for unknown modules.
    [[nodiscard]] std::int32_t load(psprecomp::Runtime &runtime, const std::string &path);
    [[nodiscard]] std::int32_t start(std::int32_t uid);
    [[nodiscard]] std::int32_t stop(std::int32_t uid);
    // Returns the module ID on success, as the PSP does.
    [[nodiscard]] std::int32_t unload(std::int32_t uid);
    [[nodiscard]] std::int32_t main_module() const noexcept { return main_uid_; }

private:
    struct Module { std::string path; bool started{}; };
    kernel::EarlyKernel *kernel_;
    io::IoManager *io_;
    std::int32_t main_uid_;
    std::map<std::int32_t, Module> modules_;
};

void register_module_manager(ModuleManager &modules, psprecomp::Runtime &runtime);

} // namespace sims2::modules
