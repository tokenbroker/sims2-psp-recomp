#include "display/native_window.hpp"
#include "gpu/native_renderer.hpp"
#include "ctrl/native_gamepad.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <iostream>
#include <stdexcept>
namespace {
SDL_WindowID window_id{};
void key(SDL_Scancode code, SDL_Keycode symbol) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.windowID = window_id;
    e.key.scancode = code;
    e.key.key = symbol;
    e.key.down = true;
    SDL_PushEvent(&e);
}
Uint32 SDLCALL close_menu(void *, SDL_TimerID, Uint32) {
    key(SDL_SCANCODE_F1, SDLK_F1);
    return 0;
}
Uint32 SDLCALL close_dialog(void *, SDL_TimerID, Uint32) {
    key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    return 0;
}
} // namespace
int main() {
    try {
        const auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        psprecomp::GuestMemory memory;
        sims2::gpu::NativeRenderer renderer;
        sims2::display::NativeWindow window(true, &renderer);
        {
            sims2::ctrl::NativeGamepad polling;
            auto event_type=SDL_RegisterEvents(1);SDL_Event event{};event.type=event_type;SDL_PushEvent(&event);
            polling.sample(false);SDL_Event kept{};
            require(SDL_PeepEvents(&kept,1,SDL_GETEVENT,event_type,event_type)==1,"Gamepad polling swallowed an SDL window event");
        }
        const auto *driver = SDL_GetCurrentVideoDriver();
        require(driver != nullptr, "SDL did not select a video driver");
        if (const auto *requested = std::getenv("SDL_VIDEO_DRIVER"))
            require(std::string_view(driver) == requested, "SDL used an unexpected video driver");
        std::cout << "SDL video driver: " << driver << '\n';
        window_id = SDL_GetWindowID(static_cast<SDL_Window *>(window.native_handle()));
        sims2::display::Display::Framebuffer frame{0x04000000u, 512u, 3u};
        sims2::ctrl::Controller controller;
        auto settings = window.settings();
        settings.width = 1280;
        settings.height = 800;
        settings.render_scale = 2;
        settings.vsync = false;
        settings.input.input_hints=true;
        window.apply_settings(settings);
        for (unsigned i = 0; i < 3; ++i)
            require(window.present(memory, frame, i * 16667, controller), "Window closed unexpectedly");
        key(SDL_SCANCODE_F2,SDLK_F2);window.present(memory,frame,45000,controller);
        require(window.settings().show_performance,"F2 overlay shortcut failed");
        require(window.presented_frames() > 0, "Vulkan swapchain did not present");
        key(SDL_SCANCODE_F1, SDLK_F1);
        SDL_AddTimer(250, close_menu, nullptr);
        const auto start = std::chrono::steady_clock::now();
        require(window.present(memory, frame, 50001, controller), "Menu closed window");
        require(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(150),
                "F1 did not pause the game");
        require(window.presented_frames() > 3, "F1 did not render");
        SDL_AddTimer(250, close_dialog, nullptr);
        require(!window.select_savedata(2, {"TEST_SLOT"}), "Save selector cancellation failed");
        SDL_Event close{};
        close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID = window_id;
        SDL_PushEvent(&close);
        require(!window.present(memory, frame, 66668, controller), "Window close request was lost");
        std::cout << "Vulkan SDL shell: swapchain, 1280x800, 2x, F1/F2, input event ownership, save cancellation, close passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
