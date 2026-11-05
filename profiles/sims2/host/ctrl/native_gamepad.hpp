#pragma once
#include "ctrl/controller.hpp"
#include <memory>
namespace sims2::ctrl {
class NativeGamepad {
public:
    NativeGamepad();
    ~NativeGamepad();
    void set_options(unsigned deadzone,bool right_stick_camera);
    Controller::Pad sample(bool focused);
    static Controller::Pad map(std::uint32_t buttons, std::int16_t x, std::int16_t y, unsigned deadzone=18);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
