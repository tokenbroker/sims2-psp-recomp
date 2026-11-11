#include "display/host_settings.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace sims2::display;
    try {
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
