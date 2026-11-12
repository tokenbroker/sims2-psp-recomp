#pragma once
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <charconv>
#include <cstdint>
#include <string_view>

namespace sims2::platform {
// Fatal native faults bypass C++ exception handling. Write a small module-relative
// report directly to the launcher's inherited log, without allocating a dump or
// copying guest memory, saves, environment variables or personal module paths.
inline LONG WINAPI report_native_crash(EXCEPTION_POINTERS *fault) noexcept {
    if (!fault || !fault->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    char text[1024]{}; char *cursor = text;
    const auto append = [&](std::string_view value) {
        for (char c : value) if (cursor < text + sizeof(text) - 1) *cursor++ = c;
    };
    const auto number = [&](std::uintptr_t value) {
        append("0x");
        const auto result = std::to_chars(cursor, text + sizeof(text) - 1, value, 16);
        if (result.ec == std::errc{}) cursor = result.ptr;
    };
    const auto &record = *fault->ExceptionRecord;
    append("[native-crash] code="); number(record.ExceptionCode);
    const auto address = reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
    MEMORY_BASIC_INFORMATION memory{}; wchar_t path[1024]{};
    if (VirtualQuery(record.ExceptionAddress, &memory, sizeof(memory)) &&
        GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), path, 1024)) {
        const wchar_t *name = path;
        for (const auto *p = path; *p; ++p) if (*p == L'\\' || *p == L'/') name = p + 1;
        char module[512]{};
        if (WideCharToMultiByte(CP_UTF8, 0, name, -1, module, sizeof(module), nullptr, nullptr)) {
            append(" module="); append(module);
            append(" offset="); number(address - reinterpret_cast<std::uintptr_t>(memory.AllocationBase));
        }
    }
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        append(record.ExceptionInformation[0] == 1 ? " access=write" :
               record.ExceptionInformation[0] == 8 ? " access=execute" : " access=read");
        append(" address="); number(record.ExceptionInformation[1]);
    }
    append("\n"); DWORD written{};
    const auto log = GetStdHandle(STD_ERROR_HANDLE);
    if (log && log != INVALID_HANDLE_VALUE) {
        WriteFile(log, text, static_cast<DWORD>(cursor - text), &written, nullptr);
        FlushFileBuffers(log);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void install_native_crash_logging() noexcept {
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(report_native_crash);
}
}
#else
namespace sims2::platform { inline void install_native_crash_logging() noexcept {} }
#endif
