// Native SDL gamepad polling; mapping follows Bigmission's physical-button convention.
#include "ctrl/native_gamepad.hpp"
#include "psprecomp/common.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
namespace sims2::ctrl {
struct NativeGamepad::Impl {
    unsigned deadzone{18};bool right_stick_camera{};
    std::map<SDL_JoystickID, SDL_Gamepad *> pads;
    ~Impl() {
        for (auto &[id, pad] : pads) SDL_CloseGamepad(pad);
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
};
NativeGamepad::NativeGamepad() : impl_(std::make_unique<Impl>()) {
    // The window is Win32-owned; focus is gated by our caller, not SDL video.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) throw psprecomp::Error(std::string("Controller initialization: ") + SDL_GetError());
}
NativeGamepad::~NativeGamepad() = default;
Controller::Pad NativeGamepad::map(std::uint32_t buttons, std::int16_t x, std::int16_t y,unsigned deadzone) {
    Controller::Pad result;
    constexpr std::pair<SDL_GamepadButton, std::uint32_t> mappings[]{
        {SDL_GAMEPAD_BUTTON_SOUTH,kButtonCross},{SDL_GAMEPAD_BUTTON_EAST,kButtonCircle},
        {SDL_GAMEPAD_BUTTON_WEST,kButtonSquare},{SDL_GAMEPAD_BUTTON_NORTH,kButtonTriangle},
        {SDL_GAMEPAD_BUTTON_START,kButtonStart},{SDL_GAMEPAD_BUTTON_BACK,kButtonSelect},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,kButtonL},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,kButtonR},
        {SDL_GAMEPAD_BUTTON_DPAD_UP,kButtonUp},{SDL_GAMEPAD_BUTTON_DPAD_DOWN,kButtonDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,kButtonLeft},{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,kButtonRight}};
    for (auto [source, target] : mappings) if (buttons & (1u << source)) result.buttons |= target;
    const float fx = float(x) / (x < 0 ? 32768.0f : 32767.0f), fy = float(y) / (y < 0 ? 32768.0f : 32767.0f);
    const auto length = std::sqrt(fx*fx + fy*fy);
    const float threshold=std::clamp(deadzone,5u,40u)/100.0f;
    if (length > threshold) {
        const auto gain = std::min(1.0f, (length - threshold) / (1-threshold)) / length;
        const auto axis = [](float value) { return static_cast<std::uint8_t>(std::clamp(std::lround(128.0f + value * 127.5f), 0l, 255l)); };
        result.lx = axis(fx * gain); result.ly = axis(fy * gain);
    }
    return result;
}
void NativeGamepad::set_options(unsigned deadzone,bool camera){impl_->deadzone=std::clamp(deadzone,5u,40u);impl_->right_stick_camera=camera;}
Controller::Pad NativeGamepad::sample(bool focused) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {} // Drain device events in our Win32-owned shell.
    SDL_UpdateGamepads();
    for (auto it = impl_->pads.begin(); it != impl_->pads.end();) {
        if (SDL_GamepadConnected(it->second)) { ++it; continue; }
        SDL_CloseGamepad(it->second); it = impl_->pads.erase(it);
    }
    int count = 0; auto *ids = SDL_GetGamepads(&count);
    for (int i=0; i<count; ++i) if (!impl_->pads.contains(ids[i])) {
        if (auto *pad = SDL_OpenGamepad(ids[i])) {
            impl_->pads.emplace(ids[i], pad);
            std::cerr << "[controller] connected: " << SDL_GetGamepadName(pad) << '\n';
        }
    }
    SDL_free(ids);
    Controller::Pad result;
    if (!focused) return result;
    for (auto &[id, pad] : impl_->pads) {
        std::uint32_t buttons = 0;
        for (unsigned button=0; button<SDL_GAMEPAD_BUTTON_COUNT; ++button)
            if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button))) buttons |= 1u << button;
        const auto state = map(buttons, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY),impl_->deadzone);
        result.buttons |= state.buttons;
        if(impl_->right_stick_camera){
            const auto x=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTX);
            if(x<-14745)result.buttons|=kButtonL;
            if(x>14745)result.buttons|=kButtonR;
        }
        if (std::abs(int(state.lx)-128)+std::abs(int(state.ly)-128) > std::abs(int(result.lx)-128)+std::abs(int(result.ly)-128)) {
            result.lx = state.lx; result.ly = state.ly;
        }
    }
    return result;
}
}
