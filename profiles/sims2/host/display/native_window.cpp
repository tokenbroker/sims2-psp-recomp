#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "display/native_window.hpp"
#include "display/framebuffer_pixels.hpp"
#include "display/save_slot_label.hpp"
#include "display/settings_overlay.hpp"
#include "display/presentation_limiter.hpp"
#include "gpu/native_renderer.hpp"
#include "ctrl/native_gamepad.hpp"
#include "ctrl/host_input.hpp"
#include "psprecomp/common.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <exception>
#include <array>

namespace sims2::display {
struct NativeWindow::Impl {
    ctrl::HostInput host_input;
    float wheel{};
    HWND window{};
    std::unique_ptr<ctrl::NativeGamepad> gamepad;
    std::unique_ptr<SettingsOverlay> overlay;
    HostSettings settings;
    PresentationLimiter limiter;
    std::filesystem::path settings_file;
    std::function<void(float)> audio_control;
    std::array<bool,256> suppressed_keys{};
    bool visible{}, display_configured{};
    gpu::NativeRenderer *renderer{};
    RECT windowed_rect{};
    void update_camera_aspect(){
        if(!renderer || !window)return;
        RECT client{};GetClientRect(window,&client);
        const auto fit=picture_rect(unsigned(std::max(0L,client.right)),unsigned(std::max(0L,client.bottom)),settings.aspect);
        if(settings.render_percent && fit.height)renderer->set_render_scale(effective_render_scale(settings,unsigned(client.bottom)));
        if(fit.height)renderer->set_display_aspect(settings.match_camera_aspect && settings.aspect!=AspectMode::Native?float(fit.width)/fit.height:480.0f/272.0f);
    }
    HANDLE frame_timer{};
    std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(480u * 272u);
    BITMAPINFO bitmap{};
    bool closed{};
    bool reset_pacing{};
    bool paced{};
    bool dialog_released{};
    std::uint32_t dialog_buttons{};
    std::uint32_t dialog_consumed{};
    std::exception_ptr dialog_error;
    std::uint64_t paced_frames{}, late_2ms{}, late_frame{}, max_late_us{}, pacing_resets{};
    std::uint64_t wake_2ms{}, wake_frame{}, max_wake_us{};
    std::uint64_t frames{}, first_us{};
    bool profile = std::getenv("SIMS2_PROFILE") != nullptr;
    std::uint64_t conversion_ns{}, scaled_presentations{};
    std::chrono::steady_clock::time_point start{};
    ~Impl() { if (frame_timer) CloseHandle(frame_timer); }
    void suppress_keyboard() {
        for (const int code : std::array<int,16>{VK_RETURN,VK_BACK,VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'X','Z','S','A','Q','E','I','J','K','L'})
            suppressed_keys[code]=(GetAsyncKeyState(code)&0x8000)!=0;
    }
    static void CALLBACK savedata_gamepad(HWND owner, UINT, UINT_PTR, DWORD) noexcept {
        auto *self = reinterpret_cast<Impl *>(GetWindowLongPtrW(owner, GWLP_USERDATA));
        const auto dialog = GetActiveWindow();
        if (!self || !self->gamepad || !dialog || GetWindow(dialog, GW_OWNER) != owner ||
            GetForegroundWindow() != dialog) return;
        try {
            const auto buttons = self->gamepad->sample(true).buttons;
            self->gamepad->settings_toggle(false);
            if (!self->dialog_released) {
                self->dialog_released = buttons == 0;
                self->dialog_buttons = buttons;
                return;
            }
            const auto pressed = buttons & ~self->dialog_buttons;
            self->dialog_buttons = buttons;
            const auto choice = pressed & ctrl::kButtonCircle ? IDCANCEL :
                pressed & ctrl::kButtonCross ? IDYES : pressed & ctrl::kButtonSquare ? IDNO : 0;
            if (choice) PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(choice, BN_CLICKED), 0);
        } catch (...) {
            self->dialog_error = std::current_exception();
            PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto *self = reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_GETMINMAXINFO) {
            auto *limits=reinterpret_cast<MINMAXINFO *>(l);
            limits->ptMaxTrackSize={8000,4600};
            limits->ptMinTrackSize={480,272};
            return 0;
        }
        if(self && message==WM_MOUSEWHEEL)self->wheel+=float(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA;
        if (self && message == WM_CLOSE) { self->closed = true; return 0; }
        if (self && self->overlay) {
            if (message == WM_KEYDOWN && !(l & (1ll << 30))) {
                if (w == VK_F1) { self->overlay->toggle(self->settings); return 0; }
                if (w == VK_F2) {
                    self->settings.show_performance=!self->settings.show_performance;
                    self->overlay->performance(self->settings.show_performance);return 0;
                }
            }
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_DPICHANGED) {
                const auto *area=reinterpret_cast<const RECT *>(l);
                SetWindowPos(window,nullptr,area->left,area->top,area->right-area->left,area->bottom-area->top,SWP_NOZORDER|SWP_NOACTIVATE);
                return 0;
            }
            if (self->overlay->handle_message(window,message,w,l)) return 1;
        }
        if (self && message == WM_PAINT) {
            if (self->overlay) { ValidateRect(window,nullptr); return 0; }
            PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); RECT rect{}; GetClientRect(window, &rect);
            StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, 480, 272,
                self->pixels.data(), &self->bitmap, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(window, &paint); return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
};
NativeWindow::NativeWindow(bool visible, gpu::NativeRenderer *renderer, bool hidden) : impl_(std::make_unique<Impl>()) {
    auto &p = *impl_;
    p.paced = visible || std::getenv("SIMS2_PACE") != nullptr;
    if (p.paced) {
        p.frame_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        // Older Windows versions retain the standard sleep fallback.
    }
    p.bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    p.bitmap.bmiHeader.biWidth = 480; p.bitmap.bmiHeader.biHeight = -272;
    p.bitmap.bmiHeader.biPlanes = 1; p.bitmap.bmiHeader.biBitCount = 32; p.bitmap.bmiHeader.biCompression = BI_RGB;
    if (!visible) return;
    if (!renderer) throw psprecomp::Error("A visible window requires the native renderer");
    p.renderer=renderer;
    p.visible=!hidden;p.settings_file=host_settings_path();p.settings=load_host_settings(p.settings_file);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    p.gamepad = std::make_unique<ctrl::NativeGamepad>();
    WNDCLASSW cls{}; cls.lpfnWndProc = Impl::procedure; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"Sims2PSPRecompDiagnostic"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    RECT area{0, 0, LONG(p.settings.width?p.settings.width:960), LONG(p.settings.height?p.settings.height:544)}; AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    p.window = CreateWindowW(cls.lpszClassName, L"The Sims 2 PSP \u2014 development build",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left, area.bottom - area.top,
        nullptr, nullptr, cls.hInstance, &p);
    if (!p.window) throw psprecomp::Error("Cannot create native display window");
    p.overlay=std::make_unique<SettingsOverlay>(p.window,renderer->device(),renderer->context());
    GetWindowRect(p.window,&p.windowed_rect);
    apply_settings(p.settings);
    if(p.visible)ShowWindow(p.window, SW_SHOW);
}
NativeWindow::~NativeWindow() {
    if (impl_->profile) std::cerr << "[profile] framebuffer_conversion_ms=" << impl_->conversion_ns / 1000000.0 << " scaled_presentations=" << impl_->scaled_presentations << " output_presentations=" << presented_frames() << '\n';
    if (impl_->paced) std::cerr << "[pacing] frames=" << impl_->frames << " sync_calls=" << impl_->paced_frames << " late_over_2ms=" << impl_->late_2ms
        << " late_over_frame=" << impl_->late_frame << " max_late_us=" << impl_->max_late_us
        << " clock_resets=" << impl_->pacing_resets << " present_late_over_2ms=" << impl_->wake_2ms
        << " present_late_over_frame=" << impl_->wake_frame << " max_present_late_us=" << impl_->max_wake_us << '\n';
    impl_->overlay.reset();
    if (impl_->window) DestroyWindow(impl_->window);
}
void *NativeWindow::native_handle() const noexcept { return impl_->window; }
std::uint64_t NativeWindow::presented_frames() const noexcept{return impl_->overlay?impl_->overlay->presented_frames():0;}
HostSettings NativeWindow::settings() const { return impl_->settings; }
void NativeWindow::set_audio_control(std::function<void(float)> control) {
    impl_->audio_control=std::move(control);
    if(impl_->audio_control)impl_->audio_control(impl_->settings.muted?0.0f:float(impl_->settings.volume)/100);
}
void NativeWindow::apply_settings(HostSettings settings) {
    auto &p=*impl_;settings=validated_settings(settings);const auto previous=p.settings;
    if(p.gamepad){p.gamepad->set_options(settings.stick_deadzone,settings.right_stick_camera);p.gamepad->set_input_options(settings.input);}
    p.host_input.release();
    if(p.renderer){p.renderer->set_anisotropy(settings.anisotropy);if(!settings.render_percent)p.renderer->set_render_scale(float(settings.render_scale));}
    if(settings.fps_cap!=previous.fps_cap)p.limiter.reset();
    if(p.overlay && (settings.mode!=previous.mode || settings.width!=previous.width || settings.height!=previous.height))p.overlay->leave_fullscreen();
    p.settings=settings;
    if(p.window && (settings.mode!=previous.mode || settings.width!=previous.width || settings.height!=previous.height || !p.display_configured)){
        if(settings.mode!=WindowMode::Windowed){
            if(previous.mode==WindowMode::Windowed)GetWindowRect(p.window,&p.windowed_rect);
            MONITORINFO monitor{sizeof(monitor)};
            if(GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor)){
                SetWindowLongPtrW(p.window,GWL_STYLE,WS_POPUP|(p.visible?WS_VISIBLE:0));
                SetWindowPos(p.window,nullptr,monitor.rcMonitor.left,monitor.rcMonitor.top,
                    monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,
                    SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
            }
        } else {
            const auto style=WS_OVERLAPPEDWINDOW|(p.visible?WS_VISIBLE:0);
            SetWindowLongPtrW(p.window,GWL_STYLE,style);
            MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor);
            RECT rect{0,0,LONG(settings.width?settings.width:monitor.rcMonitor.right-monitor.rcMonitor.left),LONG(settings.height?settings.height:monitor.rcMonitor.bottom-monitor.rcMonitor.top)};AdjustWindowRectExForDpi(&rect,style,FALSE,0,GetDpiForWindow(p.window));
            SetWindowPos(p.window,nullptr,p.windowed_rect.left,p.windowed_rect.top,rect.right-rect.left,rect.bottom-rect.top,
                SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
        }
    }
    if(p.renderer && settings.render_percent){RECT client{};GetClientRect(p.window,&client);p.renderer->set_render_scale(effective_render_scale(settings,unsigned(client.bottom)));}
    p.update_camera_aspect();
    p.display_configured=true;
    if(p.audio_control)p.audio_control(settings.muted?0.0f:float(settings.volume)/100);
    p.reset_pacing=true;
}
void NativeWindow::capture(const std::string &path) const {
    const auto &p = *impl_;
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(p.pixels.size() * 4u);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    out.write(reinterpret_cast<const char *>(&p.bitmap.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(reinterpret_cast<const char *>(p.pixels.data()), p.pixels.size() * 4u);
    if (!out) throw psprecomp::Error("Cannot write framebuffer capture");
}
std::optional<std::string> NativeWindow::select_savedata(std::uint32_t mode, const std::vector<std::string> &slots) {
    impl_->host_input.release();impl_->wheel=0;
    if (!impl_->window) throw psprecomp::Error("Savedata selection requires a visible window");
    const auto verb = mode == 2u || mode == 4u ? "Load" : mode == 6u ? "Delete" : "Save to";
    for (const auto &slot : slots) {
        const auto message = std::string(verb) + " save slot \"" + std::string(save_slot_label(slot)) + "\"?\n\n" +
            (mode == 6u ? "This removes the saved game.\n" : mode == 3u || mode == 5u ? "Existing data in this slot will be replaced.\n" : "") +
            "Yes selects this slot. No checks the next slot. Cancel returns to the game.\n\n"
            "Controller: bound Cross = Yes, Square = No, Circle = Cancel.";
        impl_->dialog_buttons = impl_->gamepad->sample(true).buttons;
        impl_->dialog_released = impl_->dialog_buttons == 0;
        impl_->dialog_error = {};
        const auto input_timer = SetTimer(impl_->window, 1, 16, &Impl::savedata_gamepad);
        const auto choice = MessageBoxA(impl_->window, message.c_str(), "The Sims 2 - Save data",
            MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONQUESTION);
        if (input_timer) KillTimer(impl_->window, input_timer);
        impl_->dialog_consumed |= impl_->gamepad->sample(true).buttons;
        impl_->suppress_keyboard();
        impl_->reset_pacing = true;
        if (impl_->dialog_error) std::rethrow_exception(impl_->dialog_error);
        if (choice == IDYES) return slot;
        if (choice == IDCANCEL) return std::nullopt;
    }
    return std::nullopt;
}
bool NativeWindow::present(const psprecomp::GuestMemory &memory, const Display::Framebuffer &frame,
    std::uint64_t now_us, ctrl::Controller &controller) {
    auto &p = *impl_;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (p.closed) return false;
    bool settings_open=p.overlay && p.overlay->open();
    ctrl::Controller::Pad pad;
    if (p.gamepad) pad = p.gamepad->sample(!settings_open && p.window && GetForegroundWindow() == p.window);
    if (p.gamepad && p.gamepad->settings_toggle(p.window && GetForegroundWindow() == p.window) && p.overlay) {
        p.overlay->toggle(p.settings);
        settings_open = p.overlay->open();
        p.host_input.release();
        p.suppress_keyboard();
        p.reset_pacing = true;
    }
    p.dialog_consumed &= pad.buttons;
    pad.buttons &= ~p.dialog_consumed;
    auto key=[&](unsigned code){
        int vk=int(code);if(code>=ctrl::Enter&&code<=ctrl::ArrowRight){constexpr int special[]{VK_RETURN,VK_BACK,VK_ESCAPE,VK_TAB,VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};vk=special[code-ctrl::Enter];}
        if(!ctrl::valid_key(code)||!code)return false;
        const bool held=(GetAsyncKeyState(vk)&0x8000)!=0;if(!held)p.suppressed_keys[vk]=false;return held&&!p.suppressed_keys[vk];
    };
    POINT position{};GetCursorPos(&position);ScreenToClient(p.window,&position);
    ctrl::MouseSnapshot mouse{float(position.x),float(position.y),p.wheel,(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0};p.wheel=0;
    const auto host_us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    pad=p.host_input.sample(p.settings.input,key,mouse,pad,!settings_open&&p.window&&GetForegroundWindow()==p.window,host_us);
    controller.set_pad(pad, now_us);
    const auto bpp = frame.pixel_format == 3u ? 4u : 2u;
    if (!frame.address || frame.buffer_width < 480u || frame.pixel_format > 3u ||
        !memory.contains(frame.address, static_cast<std::size_t>(frame.buffer_width) * 272u * bpp)) return true;
    const auto conversion_start = p.profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto byte_size = static_cast<std::size_t>(frame.buffer_width) * 272u * bpp;
    if (const auto *source = memory.raw_pointer(frame.address, byte_size)) {
        if (!convert_framebuffer_pixels({source, byte_size}, frame.buffer_width, 480, 272, frame.pixel_format, p.pixels))
            throw psprecomp::Error("Invalid native framebuffer conversion");
    } else for (unsigned y = 0; y < 272; ++y) for (unsigned x = 0; x < 480; ++x) {
        const auto address = frame.address + (y * frame.buffer_width + x) * bpp;
        const auto raw = bpp == 4 ? memory.load32(address) : memory.load16(address);
        std::uint32_t r{}, g{}, b{};
        if (frame.pixel_format == 3u) { r = raw & 255u; g = (raw >> 8) & 255u; b = (raw >> 16) & 255u; }
        else if (frame.pixel_format == 0u) { r = (raw & 31u) * 255u / 31u; g = ((raw >> 5) & 63u) * 255u / 63u; b = ((raw >> 11) & 31u) * 255u / 31u; }
        else if (frame.pixel_format == 1u) { r = (raw & 31u) * 255u / 31u; g = ((raw >> 5) & 31u) * 255u / 31u; b = ((raw >> 10) & 31u) * 255u / 31u; }
        else { r = (raw & 15u) * 17u; g = ((raw >> 4) & 15u) * 17u; b = ((raw >> 8) & 15u) * 17u; }
        p.pixels[y * 480 + x] = (r << 16) | (g << 8) | b;
    }
    if (p.profile) p.conversion_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - conversion_start).count();
    const auto *capture_frame = std::getenv("SIMS2_CAPTURE_FRAME");
    const auto capture_at = capture_frame ? std::strtoull(capture_frame, nullptr, 10) : 60u;
    const auto *interval_text = std::getenv("SIMS2_CAPTURE_EVERY");
    const auto interval = interval_text ? std::strtoull(interval_text, nullptr, 10) : 0u;
    if (++p.frames == capture_at || (interval && p.frames % interval == 0u)) if (const auto *path = std::getenv("SIMS2_CAPTURE_BMP")) {
        capture(path);
    }
    p.update_camera_aspect();
    synchronize(now_us);
    if(p.overlay){
        if(p.frames==capture_at)if(const auto *path=std::getenv("SIMS2_CAPTURE_OUTPUT_BMP"))p.overlay->capture_next_frame(path);
        const auto render=[&](bool game_frame){
            auto *view=p.renderer->presentation_view(memory,frame.address,frame.buffer_width,frame.pixel_format);
            if(game_frame && view)++p.scaled_presentations;
            const auto action=p.overlay->present(p.pixels,p.settings,game_frame,view);
            if(action.apply){apply_settings(action.settings);p.overlay->status("Settings applied");}
            if(action.save){std::string error;const bool saved=save_host_settings(p.settings_file,p.settings,error);p.overlay->status(saved?"Settings saved":error);}
        };
        if(p.overlay->open() || p.limiter.due(now_us,p.settings.fps_cap))render(true);
        while(p.overlay->open() && !p.closed){
            const auto began=std::chrono::steady_clock::now();
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            if(p.closed)break;
            if(p.gamepad) {
                p.gamepad->sample(false);
                if(p.gamepad->settings_toggle(p.window && GetForegroundWindow() == p.window))
                    p.overlay->toggle(p.settings);
            }
            controller.set_pad(ctrl::Controller::Pad{},now_us);
            render(false);
            std::this_thread::sleep_until(began+std::chrono::microseconds(16667));
        }
        if(settings_open){p.wheel=0;p.host_input.release();p.suppress_keyboard();if(p.gamepad)p.dialog_consumed|=p.gamepad->sample(true).buttons;p.reset_pacing=true;}
    } else if (p.paced && p.window) { InvalidateRect(p.window, nullptr, FALSE); UpdateWindow(p.window); }
    return !p.closed;
}
void NativeWindow::synchronize(std::uint64_t now_us) {
    auto &p = *impl_;
    if (p.paced) {
        const auto current = std::chrono::steady_clock::now();
        if (p.paced_frames != 0u && !p.reset_pacing) {
            const auto deadline = p.start + std::chrono::microseconds(now_us - p.first_us);
            const auto late = current > deadline ? std::chrono::duration_cast<std::chrono::microseconds>(current - deadline).count() : 0;
            p.max_late_us = std::max(p.max_late_us, static_cast<std::uint64_t>(late));
            p.late_2ms += late > 2000; p.late_frame += late > 16683;
        }
        if (p.paced_frames == 0u || p.reset_pacing ||
            current > p.start + std::chrono::microseconds(now_us - p.first_us) + std::chrono::milliseconds(250)) {
            if (p.paced_frames != 0u) ++p.pacing_resets;
            p.start = current; p.first_us = now_us; p.reset_pacing = false;
        }
        ++p.paced_frames;
        const auto deadline = p.start + std::chrono::microseconds(now_us - p.first_us);
        const auto remaining = deadline - std::chrono::steady_clock::now();
        bool waited = false;
        if (p.frame_timer && remaining > std::chrono::steady_clock::duration::zero()) {
            LARGE_INTEGER due{};
            due.QuadPart = -std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
            if (SetWaitableTimer(p.frame_timer, &due, 0, nullptr, nullptr, FALSE))
                waited = WaitForSingleObject(p.frame_timer, INFINITE) == WAIT_OBJECT_0;
        }
        if (!waited) std::this_thread::sleep_until(deadline);
        const auto presented = std::chrono::steady_clock::now();
        const auto wake_late = presented > deadline ? std::chrono::duration_cast<std::chrono::microseconds>(presented - deadline).count() : 0;
        p.max_wake_us = std::max(p.max_wake_us, static_cast<std::uint64_t>(wake_late));
        p.wake_2ms += wake_late > 2000; p.wake_frame += wake_late > 16683;
    }
}
}
