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
            // not presented, but the picture never stalls for long.
            const auto fail = [](const char *why) { throw std::runtime_error(why); };
            FramePacing on_time, late;
            unsigned shown{};
            std::uint64_t longest_gap{}, last_shown{};
            for (std::uint64_t frame = 0; frame < 600; ++frame) {
                const auto now_us = frame * 16683;
                on_time.begin_frame(now_us);
                if (on_time.skip_presentation(now_us, 1500)) fail("An on-time frame was not presented");
                on_time.begin_presentation(); on_time.end_presentation(now_us); on_time.end_frame();
                late.begin_frame(now_us);
                if (!late.skip_presentation(now_us, 20000)) {
                    late.begin_presentation(); late.end_presentation(now_us);
                    longest_gap = std::max(longest_gap, now_us - last_shown); last_shown = now_us; ++shown;
                }
                late.end_frame();
            }
            if (on_time.skipped()) fail("On-time frames were skipped");
            if (!late.skipped() || shown < 600 * 16683 / FramePacing::longest_gap_us - 1) fail("Late frames were not skipped while keeping the picture alive");
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
