#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <filesystem>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
int main(int argc, char **argv) {
    auto folder = std::filesystem::canonical("/proc/self/exe").parent_path();
    auto backend = folder / "sims2_probe";
    if (argc == 2 && std::string_view(argv[1]) == "--check")
        return std::filesystem::is_regular_file(backend) ? 0 : 1;
    if (argc != 3) {
        std::cerr << "Usage: sims2 <BOOT.BIN> <USA.iso>\n";
        return 2;
    }
    setenv("SIMS2_WINDOW", "1", 1);
    for (auto name :
         {"SIMS2_STOP_AFTER_FRAMES", "SIMS2_INPUT_SCRIPT", "SIMS2_LIVE_INPUT", "SIMS2_STATE_JSON",
          "SIMS2_CAPTURE_BMP", "SIMS2_CAPTURE_LAST_BMP", "SIMS2_SAVE_SLOT", "SIMS2_PACE", "SIMS2_AUDIO",
          "SIMS2_AUDIO_SILENT", "SIMS2_FRAME_CSV", "SIMS2_TRACE_UI", "SIMS2_TRACE_UI_US"})
        unsetenv(name);
    // Generated guest functions may have large native frames.
    rlimit stack{};
    if (getrlimit(RLIMIT_STACK, &stack) == 0) {
        stack.rlim_cur = std::min(rlim_t(64 * 1024 * 1024), stack.rlim_max);
        setrlimit(RLIMIT_STACK, &stack);
    }
    execl(backend.c_str(), backend.c_str(), argv[1], argv[2], "--max-dispatches", "unlimited", nullptr);
    std::cerr << "Cannot start sims2_probe\n";
    return 1;
}
