#include "display/frame_pacing.hpp"
#include "display/host_settings.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace sims2::display;
    try {
        {
            // A display that blocks must not slow the game: late frames are
            // not presented, but the picture never stalls for long. A frame
            // that is late because of game work and cheap to present gains
            // little from skipping, so at most every other such frame is.
            const auto fail = [](const char *why) { throw std::runtime_error(why); };
            FramePacing on_time, blocked, busy;
            unsigned shown{}, busy_shown{};
            std::uint64_t longest_gap{}, last_shown{};
            bool busy_skipped_last{};
            for (std::uint64_t frame = 0; frame < 600; ++frame) {
                const auto now_us = frame * 16683;
                on_time.begin_frame(now_us);
                if (on_time.skip_presentation(now_us, 1500)) fail("An on-time frame was not presented");
                on_time.record_presentation(now_us, 16000); on_time.end_frame();
                blocked.begin_frame(now_us);
                if (!blocked.skip_presentation(now_us, 20000)) {
                    blocked.record_presentation(now_us, 16683);
                    longest_gap = std::max(longest_gap, now_us - last_shown); last_shown = now_us; ++shown;
                }
                blocked.end_frame();
                busy.begin_frame(now_us);
                const bool busy_skip = busy.skip_presentation(now_us, 20000);
                if (busy_skip && busy_skipped_last) fail("Two late frames that are cheap to present were skipped in a row");
                busy_skipped_last = busy_skip;
                if (!busy_skip) { busy.record_presentation(now_us, 300); ++busy_shown; }
                busy.end_frame();
            }
            if (on_time.skipped()) fail("On-time frames were skipped");
            if (!blocked.skipped() || shown < 600 * 16683 / FramePacing::longest_gap_us - 1) fail("Late frames were not skipped while keeping the picture alive");
            if (!busy.skipped() || busy_shown < 300) fail("Cheap late frames were not alternated");
            if (longest_gap > FramePacing::longest_gap_us + 16683) fail("The picture stalled while the host was late");
        }
#if defined(_WIN32)
        _putenv_s("SIMS2_SETTINGS_PATH", "");
#else
        unsetenv("SIMS2_SETTINGS_PATH");
#endif
        const auto filename = host_settings_path().filename();
#if defined(SIMS2_EUROPE)
        if (filename != "settings-europe.ini") throw std::runtime_error("European settings share USA path");
#else
        if (filename != "settings.ini") throw std::runtime_error("USA settings path changed");
#endif
        const auto path = std::filesystem::temp_directory_path() /
            ("sims2-region-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
        for (unsigned language = 1; language <= 5; ++language) {
            HostSettings settings;
            settings.system_language = language;
            settings.engine_fps = 240;
            std::string error;
            if (!save_host_settings(path, settings, error)) throw std::runtime_error(error);
            auto loaded = load_host_settings(path);

#if defined(SIMS2_EUROPE)
            if (loaded.system_language != language) throw std::runtime_error("Language did not persist");
#else
            if (loaded.system_language != 1) throw std::runtime_error("USA language behavior changed");
#endif
            if (loaded.engine_fps != 240) throw std::runtime_error("Regional timing preference did not persist");
        }
        std::filesystem::remove(path);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
