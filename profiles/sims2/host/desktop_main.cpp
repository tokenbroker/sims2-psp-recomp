#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include "display/backend_selection.hpp"

namespace {
std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
int fail(const std::wstring &message) {
    MessageBoxW(nullptr, message.c_str(), L"The Sims 2 PSP", MB_OK | MB_ICONERROR);
    return 1;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t module[32768]{};
    if (!GetModuleFileNameW(nullptr, module, 32768)) return fail(L"Cannot locate the game folder.");
    const auto folder = std::filesystem::path(module).parent_path();
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return fail(L"Cannot read the launch arguments.");
    using sims2::display::GraphicsBackend;
#if defined(SIMS2_PRIMARY_VULKAN)
    constexpr auto primary=GraphicsBackend::Vulkan;
#else
    constexpr auto primary=GraphicsBackend::D3D11;
#endif
    const auto settings=sims2::display::load_host_settings(sims2::display::host_settings_path());
    auto selected=settings.graphics_backend;
    // Explicit shortcuts can recover a saved choice without opening the game.
    if(const auto *choice=std::getenv("SIMS2_GRAPHICS_BACKEND")) {
        const std::string value(choice);
        if(value=="vulkan")selected=GraphicsBackend::Vulkan;
        else if(value=="d3d11")selected=GraphicsBackend::D3D11;
        else if(value=="auto")selected=GraphicsBackend::Default;
        else {LocalFree(argv);return fail(L"Unknown graphics backend; use auto, d3d11 or vulkan.");}
    }
#if defined(SIMS2_RUNTIME_SUBDIR)
    const auto runtime_folder=folder/SIMS2_RUNTIME_SUBDIR;
#else
    const auto runtime_folder=folder;
#endif
    const auto backend=sims2::display::selected_runtime(runtime_folder,selected,primary);
    std::error_code backend_error;
    const auto prepared = std::filesystem::is_regular_file(backend, backend_error);
    if (argc == 2 && std::wstring(argv[1]) == L"--check") {
        LocalFree(argv);
        if (!prepared) return 1;
        for (const auto name : {L"SDL3.dll", L"avcodec-61.dll", L"avutil-59.dll"})
            if (!std::filesystem::is_regular_file(backend.parent_path() / name, backend_error)) return 1;
        return 0;
    }
    if (argc != 3) {
        LocalFree(argv);
        return fail(L"Start The Sims 2 PSP from RecompLauncher and select your USA ULUS-10031 ISO during setup.");
    }
    if (!prepared) {
        LocalFree(argv);
        return fail(L"The selected graphics runtime is not installed.\n\nInstall both backends with setup --graphics BOTH, or use the Direct3D 11/Vulkan shortcut for an installed runtime.");
    }
    std::wstring command = quote(backend.wstring()) + L" " + quote(argv[1]) + L" " + quote(argv[2])
        + L" --max-dispatches unlimited";
    LocalFree(argv);
    // Desktop play always uses a live window and keyboard, with no automated
    // input, screenshot pauses, headless slot selection or diagnostic budget.
    SetEnvironmentVariableW(L"SIMS2_WINDOW", L"1");
    for (const auto name : {L"SIMS2_STOP_AFTER_FRAMES", L"SIMS2_INPUT_SCRIPT", L"SIMS2_LIVE_INPUT",
            L"SIMS2_STATE_JSON", L"SIMS2_CAPTURE_BMP", L"SIMS2_CAPTURE_LAST_BMP", L"SIMS2_SAVE_SLOT",
            L"SIMS2_PACE", L"SIMS2_AUDIO", L"SIMS2_AUDIO_SILENT", L"SIMS2_FRAME_CSV",
            L"SIMS2_TRACE_UI", L"SIMS2_TRACE_UI_US", L"SIMS2_TIMING_TRACE", L"SIMS2_EXPERIMENTAL_RENDER_HZ", L"PSPRECOMP_HLE_REGISTRY"})
        SetEnvironmentVariableW(name, nullptr);
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) return fail(L"Cannot locate the local application data folder.");
    const auto logs = std::filesystem::path(local) / L"Sims2PSPRecomp" / L"logs";
    std::error_code error;
    std::filesystem::create_directories(logs, error);
    if (error) return fail(L"Cannot create the game log folder.");
    const auto log_path = logs / L"latest.log";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        return fail(L"Cannot open the game log.");
    }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log; startup.hStdInput = input;
    PROCESS_INFORMATION process{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (job) CloseHandle(job);
        CloseHandle(input); CloseHandle(log);
        return fail(L"Cannot create the game process group.");
    }
    const auto started = CreateProcessW(backend.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, backend.parent_path().c_str(), &startup, &process);
    CloseHandle(input); CloseHandle(log);
    if (!started) {
        CloseHandle(job);
        return fail(L"Cannot start the prepared game runtime. Run setup again from RecompLauncher.");
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
        return fail(L"Cannot attach the game runtime to its process group.");
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(process.hProcess, &code); CloseHandle(process.hProcess);
    CloseHandle(job);
    std::ifstream output(log_path);
    const std::string contents((std::istreambuf_iterator<char>(output)), {});
    if (code == 0 || (code == 3 && contents.find("Stopped: Window closed") != std::string::npos)) return 0;
    return fail(L"The development build stopped while running the game.\n\nDetails are saved in:\n" + log_path.wstring());
}
