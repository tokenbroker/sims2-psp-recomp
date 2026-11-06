#pragma once
#include "ctrl/controller.hpp"
#include "ctrl/input_config.hpp"
#include <chrono>
#include <cmath>
#include <functional>
namespace sims2::ctrl {
inline constexpr std::array<std::uint32_t, 12> action_buttons{
    kButtonCross, kButtonCircle, kButtonSquare, kButtonTriangle, kButtonL,    kButtonR,
    kButtonStart, kButtonSelect, kButtonUp,     kButtonDown,     kButtonLeft, kButtonRight};
struct MouseSnapshot {
    float x{}, y{}, wheel{};
    bool right{};
};
// All state belongs to the window thread; deadlines use monotonic host time.
class HostInput {
    bool dragging{}, suppressed{};
    float previous_x{}, motion{};
    std::uint32_t mouse_button{}, wheel_button{}, direction_held{};
    std::uint64_t mouse_until{}, wheel_until{}, repeat_at{};
    void clear() {
        dragging = false;
        motion = 0;
        mouse_until = wheel_until = repeat_at = 0;
        direction_held = 0;
    }

  public:
    void release() {
        clear();
        suppressed = true;
    }
    Controller::Pad sample(const InputConfig &c, const std::function<bool(unsigned)> &key,
                           MouseSnapshot mouse, Controller::Pad pad, bool enabled, std::uint64_t now) {
        bool any = mouse.right || pad.buttons || pad.lx != 128 || pad.ly != 128;
        for (auto code : c.keys)
            any |= code && key(code);
        if (!enabled) {
            release();
            return {};
        }
        if (suppressed) {
            if (any)
                return {};
            suppressed = false;
        }
        for (unsigned i = 0; i < 12; ++i)
            if (c.keys[i] && key(c.keys[i]))
                pad.buttons |= action_buttons[i];
        auto axis = [&](unsigned negative, unsigned positive, std::uint8_t existing) {
            int value = (key(c.keys[positive]) ? 1 : 0) - (key(c.keys[negative]) ? 1 : 0);
            return value ? std::uint8_t(value < 0 ? 0 : 255) : existing;
        };
        pad.lx = axis(AnalogLeft, AnalogRight, pad.lx);
        pad.ly = axis(AnalogUp, AnalogDown, pad.ly);
        if (c.mouse_camera && mouse.right) {
            if (dragging) {
                motion += (mouse.x - previous_x) * c.mouse_sensitivity / 100.f;
                if (std::abs(motion) >= 4) {
                    bool right = motion > 0;
                    if (c.invert_camera)
                        right = !right;
                    mouse_button = right ? kButtonR : kButtonL;
                    mouse_until = now + c.mouse_pulse_ms * 1000ull;
                    motion = 0;
                }
            }
            dragging = true;
            previous_x = mouse.x;
        } else {
            dragging = false;
            motion = 0;
            mouse_until = 0;
        }
        if (now < mouse_until)
            pad.buttons |= mouse_button;
        if (mouse.wheel != 0) {
            wheel_button = action_buttons[mouse.wheel > 0 ? c.wheel_up : c.wheel_down];
            wheel_until = now + 50000;
        }
        if (now < wheel_until)
            pad.buttons |= wheel_button;
        // Independent repeat rate for digital menu directions, measured in real time.
        constexpr auto directions = kButtonUp | kButtonDown | kButtonLeft | kButtonRight;
        const auto held = pad.buttons & directions;
        if (held != direction_held) {
            direction_held = held;
            repeat_at = now + 350000;
        } else if (held && c.menu_repeat_ms) {
            if (now < repeat_at)
                pad.buttons &= ~directions;
            else
                repeat_at = now + c.menu_repeat_ms * 1000ull;
        }
        return pad;
    }
};
} // namespace sims2::ctrl
