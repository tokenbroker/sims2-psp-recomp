#include "platform/native_crash.hpp"
int main() {
    sims2::platform::install_native_crash_logging();
    // Returning calls must restore their enclosing operation, not leave stale
    // breadcrumbs after the GPU call has completed.
    auto value = sims2::platform::native_call("returned-call", [] { return 7; });
    if (value != 7 || std::string_view(sims2::platform::native_call_on_thread) != "outside-instrumented-call") return 2;
    const unsigned detail[]{7, 9};
    sims2::platform::set_native_call_detail(detail, 2);
    sims2::platform::NativeCallScope phase("synthetic-phase");
    if (std::string_view(sims2::platform::native_call_on_thread) != "synthetic-phase") return 3;
    sims2::platform::native_call("synthetic-fault", [] {
        *reinterpret_cast<volatile int *>(static_cast<std::uintptr_t>(0x10)) = 42;
    });
}
