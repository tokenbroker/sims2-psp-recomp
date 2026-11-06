// Adapted from bigmission-recomp (MIT). See ../THIRD_PARTY.md in the profile root.
#pragma once

#include <cstdint>

namespace sims2::kernel {

// Selected once before guest execution. F1 persists a choice for the next launch;
// changing the clock origin while threads are running would invalidate deadlines.
inline unsigned guest_display_hz = 0u;
[[nodiscard]] inline bool configure_guest_display(unsigned hz) noexcept {
    if (hz != 0u && hz != 120u && hz != 240u) return false;
    guest_display_hz = hz;
    return true;
}
[[nodiscard]] inline float guest_display_frequency() noexcept {
    return guest_display_hz ? static_cast<float>(guest_display_hz) : 60000.0f / 1001.0f;
}

// Vblank n (n >= 1) starts at n * 1001000 / 60 microseconds: 60000/1001 Hz.
[[nodiscard]] inline std::uint64_t vblank_time_us(std::uint64_t n) noexcept {
    return guest_display_hz ? n * 1000000u / guest_display_hz : n * 1001000u / 60u;
}

// Number of vblanks that have started at or before time_us.
[[nodiscard]] inline std::uint64_t vblanks_by(std::uint64_t time_us) noexcept {
    return guest_display_hz ? (guest_display_hz * (time_us + 1u) - 1u) / 1000000u :
        (60u * (time_us + 1u) - 1u) / 1001000u;
}

} // namespace sims2::kernel
