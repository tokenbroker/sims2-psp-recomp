#include "platform/native_crash.hpp"
int main() {
    sims2::platform::install_native_crash_logging();
    *reinterpret_cast<volatile int *>(static_cast<std::uintptr_t>(0x10)) = 42;
}
