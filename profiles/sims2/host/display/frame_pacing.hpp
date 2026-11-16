#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ostream>
namespace sims2::display {
// Keeps display waits from slowing the game. A presentation can block for a
// whole refresh interval (vsync, a driver or handheld frame limiter, a display
// slower than the engine rate). When the host is already behind the guest
// clock, that frame is not presented; the simulation and audio keep real time
// and the picture catches up on the next frame. Also measures where each
// frame's host time went, for the exit report.
class FramePacing {
    using clock = std::chrono::steady_clock;
    static std::uint64_t us(clock::duration d) {
        return std::uint64_t(std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::microseconds>(d).count()));
    }
    std::uint64_t previous_us_{}, last_presented_us_{}, frames_{}, presented_{}, skipped_{}, late_frames_{};
    std::uint64_t work_us_{}, present_us_{}, max_present_us_{}, wall_us_{}, guest_us_{}, period_us_{16683};
    clock::time_point frame_began_{}, frame_ended_{}, present_began_{};
    std::uint64_t measured_{};
public:
    // At most this much guest time passes without a presentation.
    static constexpr std::uint64_t longest_gap_us = 100000;
    // Call once per guest frame, before waiting on the guest clock.
    void begin_frame(std::uint64_t now_us) {
        const auto now = clock::now();
        if (frames_ && now_us > previous_us_) {
            const auto guest = now_us - previous_us_, wall = us(now - frame_began_);
            period_us_ = std::clamp<std::uint64_t>(guest, 2000, 40000);
            // Ignore pauses (settings menu, save dialog, a dragged window).
            // Ignore frames that returned before presenting.
            if (wall < 1000000 && guest < 1000000 && frame_ended_ >= frame_began_) {
                wall_us_ += wall;
                guest_us_ += guest;
                work_us_ += us(now - frame_ended_);
                ++measured_;
            }
        }
        previous_us_ = now_us;
        frame_began_ = now;
        ++frames_;
    }
    // late_us: how far the host already is behind this frame's deadline.
    [[nodiscard]] bool skip_presentation(std::uint64_t now_us, std::uint64_t late_us) {
        const bool late = late_us > std::max<std::uint64_t>(2000, period_us_ / 2);
        late_frames_ += late;
        if (!late || !presented_ || now_us - last_presented_us_ >= longest_gap_us)
            return false;
        ++skipped_;
        return true;
    }
    void begin_presentation() { present_began_ = clock::now(); }
    void end_presentation(std::uint64_t now_us) {
        const auto spent = us(clock::now() - present_began_);
        present_us_ += spent;
        max_present_us_ = std::max(max_present_us_, spent);
        last_presented_us_ = now_us;
        ++presented_;
    }
    void end_frame() { frame_ended_ = clock::now(); }
    [[nodiscard]] std::uint64_t skipped() const noexcept { return skipped_; }
    void report(std::ostream &out) const {
        if (!measured_)
            return;
        out << "[pacing] frames=" << frames_ << " presented=" << presented_ << " skipped=" << skipped_
            << " late_frames=" << late_frames_ << " speed=" << double(guest_us_) / double(std::max<std::uint64_t>(1, wall_us_))
            << " frame_period_us=" << period_us_ << " avg_game_work_us=" << work_us_ / measured_
            << " avg_present_us=" << (presented_ ? present_us_ / presented_ : 0)
            << " max_present_us=" << max_present_us_ << '\n';
    }
};
}
