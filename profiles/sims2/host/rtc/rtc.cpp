// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#include "rtc/rtc.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>

namespace sims2::rtc {

DateTime civil_from_unix_us(std::int64_t unix_us) noexcept {
    constexpr std::int64_t kMicrosPerDay = 86'400'000'000;
    std::int64_t days = unix_us / kMicrosPerDay;
    std::int64_t remainder = unix_us % kMicrosPerDay;
    if (remainder < 0) { remainder += kMicrosPerDay; --days; }
    // Howard Hinnant's civil_from_days.
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    const std::int64_t seconds = remainder / 1'000'000;
    return {static_cast<std::uint16_t>(year), static_cast<std::uint16_t>(month), static_cast<std::uint16_t>(day),
            static_cast<std::uint16_t>(seconds / 3600), static_cast<std::uint16_t>(seconds / 60 % 60),
            static_cast<std::uint16_t>(seconds % 60), static_cast<std::uint32_t>(remainder % 1'000'000)};
}

std::int64_t Rtc::host_boot_local_unix_us() {
    return host_boot_utc_unix_us() + static_cast<std::int64_t>(host_utc_offset_seconds()) * 1000000;
}

std::int64_t Rtc::host_boot_utc_unix_us() {
    if (const char *epoch = std::getenv("SIMS2_RTC_EPOCH"); epoch != nullptr && *epoch != '\0')
        return std::strtoll(epoch, nullptr, 10) * 1'000'000;
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::int32_t Rtc::host_utc_offset_seconds() {
    const auto now = std::chrono::system_clock::from_time_t(
        static_cast<std::time_t>(host_boot_utc_unix_us() / 1000000));
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const auto date = std::chrono::year(local.tm_year + 1900) / (local.tm_mon + 1) / local.tm_mday;
    const auto local_seconds = std::chrono::sys_days(date).time_since_epoch() +
        std::chrono::hours(local.tm_hour) + std::chrono::minutes(local.tm_min) + std::chrono::seconds(local.tm_sec);
    return static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::seconds>(local_seconds).count() - seconds);
}

void register_rtc(Rtc &rtc, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceRtc", 0x3F7AD767u,
        [&rtc](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto address = ctx.gpr[4];
            if (!rt.memory().contains(address, 8u)) { ctx.set_gpr(2u, kInvalidPointer); return; }
            const auto tick = rtc.current_tick();
            rt.memory().store32(address, static_cast<std::uint32_t>(tick));
            rt.memory().store32(address + 4u, static_cast<std::uint32_t>(tick >> 32u));
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceRtc", 0xE7C27D1Bu,  // sceRtcGetCurrentClockLocalTime
        [&rtc](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const std::uint32_t address = ctx.gpr[4];
            if (!rt.memory().contains(address, 16u)) { ctx.set_gpr(2u, kInvalidPointer); return; }
            const DateTime time = rtc.local_time();
            const std::uint16_t fields[6] = {time.year, time.month, time.day, time.hour, time.minute, time.second};
            for (std::uint32_t i = 0; i < 6u; ++i) rt.memory().store16(address + i * 2u, fields[i]);
            rt.memory().store32(address + 12u, time.microsecond);
            ctx.set_gpr(2u, 0u);
        });
}

} // namespace sims2::rtc
