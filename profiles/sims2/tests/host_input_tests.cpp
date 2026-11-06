#include "ctrl/host_input.hpp"
#include "display/host_settings.hpp"
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
int main() {
    try {
        using namespace sims2::ctrl;
        auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        InputConfig config;
        config.keys = wasd_keys;
        HostInput input;
        std::set<unsigned> held;
        auto key = [&](unsigned code) { return code && held.contains(code); };
        held = {'W', ' '};
        auto pad = input.sample(config, key, {}, {}, true, 1000);
        require(pad.ly == 0 && (pad.buttons & kButtonCross), "WASD and keyboard interact");
        input.sample(config, key, {}, pad, false, 2000);
        pad = input.sample(config, key, {}, {}, true, 3000);
        require(pad.ly == 128 && !pad.buttons, "Focus loss leaked held keys");
        held.clear();
        input.sample(config, key, {}, {}, true, 4000);
        held = {' '};
        pad = input.sample(config, key, {}, {}, true, 5000);
        require(pad.buttons & kButtonCross, "Focus recovery did not re-arm");
        held.clear();
        input.sample(config, key, {10, 0, 0, true}, {}, true, 6000);
        pad = input.sample(config, key, {30, 0, 0, true}, {}, true, 7000);
        require(pad.buttons & kButtonR, "Mouse camera right");
        pad = input.sample(config, key, {30, 0, 0, false}, {}, true, 8000);
        require(!(pad.buttons & (kButtonL | kButtonR)), "Mouse release stuck camera");
        config.invert_camera = true;
        input.sample(config, key, {10, 0, 0, true}, {}, true, 9000);
        pad = input.sample(config, key, {30, 0, 0, true}, {}, true, 10000);
        require(pad.buttons & kButtonL, "Camera inversion");
        input.sample(config, key, {}, {}, true, 11000);
        pad = input.sample(config, key, {0, 0, 1, false}, {}, true, 12000);
        require(pad.buttons & kButtonUp, "Mouse wheel navigation");
        pad = input.sample(config, key, {}, {}, true, 100000);
        require(!pad.buttons, "Mouse wheel did not expire");
        config.menu_repeat_ms = 160;
        held = {ArrowDown};
        pad = input.sample(config, key, {}, {}, true, 200000);
        require(pad.buttons & kButtonDown, "First menu press missing");
        pad = input.sample(config, key, {}, {}, true, 210000);
        require(!(pad.buttons & kButtonDown), "Held menu repeat not limited");
        pad = input.sample(config, key, {}, {}, true, 550000);
        require(pad.buttons & kButtonDown, "Menu repeat deadline missing");
        config.keys[Cross] = 'V';
        held = {'V'};
        pad = input.sample(config, key, {}, {}, true, 600000);
        require(pad.buttons & kButtonCross, "Custom keyboard binding");
        sims2::display::HostSettings settings;
        settings.input = config;
        settings.input.gamepad[Cross] = 2;
        settings.input.controller_labels=2;
        const auto path = std::filesystem::temp_directory_path() / "sims2-input-roundtrip.ini";
        std::string error;
        require(sims2::display::save_host_settings(path, settings, error), "Input settings save");
        require(sims2::display::load_host_settings(path) == settings, "Input settings roundtrip");
        std::filesystem::remove(path);
        config.keys[0] = 99999;
        config.gamepad[0] = 99999;
        config.mouse_sensitivity = 99999;
        config = validated_input(config);
        require(config.keys[0] == classic_keys[0] && config.gamepad[0] == default_gamepad[0] &&
                    config.mouse_sensitivity == 300,
                "Invalid binding validation");
        std::cout << "Shared input: rebinding, mouse, wheel, repeat, focus and persistence passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
