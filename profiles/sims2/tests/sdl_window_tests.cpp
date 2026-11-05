#include "display/native_window.hpp"
#include "gpu/native_renderer.hpp"
#include <SDL3/SDL.h>
#include <chrono>
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
        window_id = SDL_GetWindowID(static_cast<SDL_Window *>(window.native_handle()));
        sims2::display::Display::Framebuffer frame{0x04000000u, 512u, 3u};
        sims2::ctrl::Controller controller;
        auto settings = window.settings();
        settings.width = 1280;
        settings.height = 800;
        settings.render_scale = 2;
        settings.vsync = false;
        window.apply_settings(settings);
        for (unsigned i = 0; i < 3; ++i)
            require(window.present(memory, frame, i * 16667, controller), "Window closed unexpectedly");
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
        std::cout << "Vulkan SDL shell: swapchain, 1280x800, 2x, F1 pause/resume, save cancellation passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
