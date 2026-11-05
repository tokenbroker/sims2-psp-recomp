#pragma once
#include "kernel/early_kernel.hpp"
#include <memory>

namespace sims2::font {
// Windows diagnostic substitute for sceLibFont. Uses the OS Arial font,
// not Sony PGF data. Typography and metrics differ from the original PSP.
class NativeFont {
public:
    explicit NativeFont(kernel::EarlyKernel &kernel);
    ~NativeFont();
    void install(psprecomp::Runtime &runtime);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
