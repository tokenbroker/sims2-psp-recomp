#pragma once
#include <algorithm>
#include <array>
#include <string>
namespace sims2::ctrl {
enum InputAction {
    Cross,
    Circle,
    Square,
    Triangle,
    LeftShoulder,
    RightShoulder,
    Start,
    Select,
    Up,
    Down,
    Left,
    Right,
    AnalogUp,
    AnalogDown,
    AnalogLeft,
    AnalogRight,
    ActionCount
};
inline constexpr std::array<const char *, ActionCount> action_names{
    "Cross / interact", "Circle / cancel",  "Square",      "Triangle",
    "L / camera left",  "R / camera right", "Start",       "Select",
    "D-pad up",         "D-pad down",       "D-pad left",  "D-pad right",
    "Analog up",        "Analog down",      "Analog left", "Analog right"};
enum KeyCode { Unbound = 0, Enter = 256, Backspace, Escape, Tab, ArrowUp, ArrowDown, ArrowLeft, ArrowRight };
inline constexpr std::array<unsigned, ActionCount> classic_keys{
    'X',     'Z',       'A',       'S',        'Q', 'E', Enter, Backspace,
    ArrowUp, ArrowDown, ArrowLeft, ArrowRight, 'I', 'K', 'J',   'L'};
inline constexpr std::array<unsigned, ActionCount> wasd_keys{
    ' ',     Escape,    'F',       'G',        'Q', 'E', Enter, Tab,
    ArrowUp, ArrowDown, ArrowLeft, ArrowRight, 'W', 'S', 'A',   'D'};
inline constexpr std::array<unsigned, 12> default_gamepad{0, 1, 2, 3, 9, 10, 6, 4, 11, 12, 13, 14};
struct InputConfig {
    std::array<unsigned, ActionCount> keys = classic_keys;
    std::array<unsigned, 12> gamepad = default_gamepad;
    unsigned camera_threshold = 45, mouse_sensitivity = 100, mouse_pulse_ms = 80, menu_repeat_ms = 0;
    unsigned wheel_up = Up, wheel_down = Down, controller_labels = 0;
    bool mouse_camera = true, invert_camera = false, input_hints = false;
    bool operator==(const InputConfig &) const = default;
};
inline const char *const *controller_names(unsigned style) {
    static const char *xbox[]{"South / A", "East / B",     "West / X",   "North / Y",  "View / Back",
                              "Guide",     "Menu / Start", "L3",         "R3",         "LB",
                              "RB",        "D-pad up",     "D-pad down", "D-pad left", "D-pad right"};
    static const char *ps[]{"Cross", "Circle",          "Square",     "Triangle",   "Share / Select",
                            "PS",    "Options / Start", "L3",         "R3",         "L1",
                            "R1",    "D-pad up",        "D-pad down", "D-pad left", "D-pad right"};
    static const char *nintendo[]{"South / B", "East / A", "West / Y",   "North / X",   "Minus",
                                  "Home",      "Plus",     "Left stick", "Right stick", "L",
                                  "R",         "D-pad up", "D-pad down", "D-pad left",  "D-pad right"};
    static const char *physical[]{"South",          "East",     "West",       "North",       "Back",
                                  "Guide",          "Start",    "Left stick", "Right stick", "Left shoulder",
                                  "Right shoulder", "D-pad up", "D-pad down", "D-pad left",  "D-pad right"};
    return style == 1 ? ps : style == 2 ? nintendo : style == 3 ? physical : xbox;
}
inline bool valid_key(unsigned code) {
    return code == 0 || code == ' ' || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') ||
           (code >= Enter && code <= ArrowRight);
}
inline std::string key_name(unsigned code) {
    if (code == 0)
        return "Unbound";
    if (code == ' ')
        return "Space";
    if (code < 128)
        return std::string(1, char(code));
    const char *names[]{"Enter", "Backspace", "Escape", "Tab", "Up", "Down", "Left", "Right"};
    return code >= Enter && code <= ArrowRight ? names[code - Enter] : "Invalid";
}
inline InputConfig validated_input(InputConfig c) {
    for (unsigned i = 0; i < c.keys.size(); ++i)
        if (!valid_key(c.keys[i]))
            c.keys[i] = classic_keys[i];
    for (unsigned i = 0; i < c.gamepad.size(); ++i)
        if (c.gamepad[i] > 14)
            c.gamepad[i] = default_gamepad[i];
    c.camera_threshold = std::clamp(c.camera_threshold, 5u, 95u);
    c.mouse_sensitivity = std::clamp(c.mouse_sensitivity, 10u, 300u);
    c.mouse_pulse_ms = std::clamp(c.mouse_pulse_ms, 20u, 250u);
    if (c.menu_repeat_ms)
        c.menu_repeat_ms = std::clamp(c.menu_repeat_ms, 80u, 500u);
    c.controller_labels = std::min(c.controller_labels, 3u);
    if (c.wheel_up >= 12)
        c.wheel_up = Up;
    if (c.wheel_down >= 12)
        c.wheel_down = Down;
    return c;
}
} // namespace sims2::ctrl
