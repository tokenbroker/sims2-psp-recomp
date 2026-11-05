// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#pragma once

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sims2::rtc {

inline constexpr std::uint32_t kInvalidPointer = 0x80000103u;

// ScePspDateTime fields.
struct DateTime {
    std::uint16_t year{}, month{}, day{}, hour{}, minute{}, second{};
    std::uint32_t microsecond{};
};

[[nodiscard]] DateTime civil_from_unix_us(std::int64_t unix_us) noexcept;

// Guest wall clock: the boot time plus the kernel's virtual elapsed time, so
// it advances exactly with guest time.
class Rtc {
public:
    // UTC wall time at boot plus an explicit local calendar offset.
    Rtc(kernel::EarlyKernel &kernel, std::int64_t boot_utc_unix_us, std::int32_t utc_offset_seconds = 0) noexcept
        : kernel_(&kernel), boot_local_unix_us_(boot_utc_unix_us + static_cast<std::int64_t>(utc_offset_seconds) * 1000000),
          boot_utc_unix_us_(boot_utc_unix_us) {}
    // SIMS2_RTC_EPOCH overrides the UTC boot time in Unix seconds.
    [[nodiscard]] static std::int64_t host_boot_local_unix_us();
    [[nodiscard]] static std::int64_t host_boot_utc_unix_us();
    [[nodiscard]] static std::int32_t host_utc_offset_seconds();
    [[nodiscard]] std::uint64_t current_tick() const noexcept {
        // PSP RTC ticks are microseconds since 0001-01-01, in UTC.
        constexpr std::uint64_t unix_epoch_tick = 62135596800000000ull;
        return unix_epoch_tick + static_cast<std::uint64_t>(boot_utc_unix_us_) + kernel_->now_us();
    }

    [[nodiscard]] DateTime local_time() const noexcept {
        return civil_from_unix_us(boot_local_unix_us_ + static_cast<std::int64_t>(kernel_->now_us()));
    }

private:
    kernel::EarlyKernel *kernel_;
    std::int64_t boot_local_unix_us_;
    std::int64_t boot_utc_unix_us_;
};

void register_rtc(Rtc &rtc, psprecomp::Runtime &runtime);

} // namespace sims2::rtc
