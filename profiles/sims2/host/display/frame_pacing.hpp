#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ostream>
namespace sims2::display {
// Keeps display waits from slowing the game. A presentation can block for a
// whole refresh interval (vsync, a driver or handheld frame limiter, a display
// slower than the engine rate). When the host is already behind the guest
// clock and presenting has recently cost a material part of a frame, that
// frame is not presented; the simulation and audio keep real time and the
// picture catches up on the next frame. When presenting is cheap, the host is
// late because of game work; skipping every other late frame then recovers a
// little time while the picture keeps at least half the frame rate. Also
// measures where each frame's host time went, for the exit report.
class FramePacing {
    using clock = std::chrono::steady_clock;
    static std::uint64_t us(clock::duration d) {
        return std::uint64_t(std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::microseconds>(d).count()));
    }
    std::uint64_t previous_us_{}, last_presented_us_{}, frames_{}, presented_{}, skipped_{}, late_frames_{}, skipped_in_row_{};
    std::uint64_t work_us_{}, present_us_{}, max_present_us_{}, recent_present_us_{}, wall_us_{}, guest_us_{}, period_us_{16683};
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
        // Skipping saves only the presentation's own time. Under a quarter of
        // a frame that is little, so never skip two such frames in a row.
        if (recent_present_us_ < period_us_ / 4 && skipped_in_row_)
            return false;
        ++skipped_;
        ++skipped_in_row_;
        return true;
    }
    void begin_presentation() { present_began_ = clock::now(); }
    void end_presentation(std::uint64_t now_us) { record_presentation(now_us, us(clock::now() - present_began_)); }
    // spent_us: host time the presentation of the guest frame at now_us took.
    void record_presentation(std::uint64_t now_us, std::uint64_t spent_us) {
        present_us_ += spent_us;
        max_present_us_ = std::max(max_present_us_, spent_us);
        // Average over roughly the last eight presentations.
        recent_present_us_ = presented_ ? (recent_present_us_ * 7 + spent_us) / 8 : spent_us;
        last_presented_us_ = now_us;
        ++presented_;
        skipped_in_row_ = 0;
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
