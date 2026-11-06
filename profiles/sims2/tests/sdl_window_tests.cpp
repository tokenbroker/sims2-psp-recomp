#include "display/native_window.hpp"
#include "gpu/native_renderer.hpp"
#include "ctrl/native_gamepad.hpp"
#include "display/save_slot_label.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <iostream>
#include <stdexcept>
#include <imgui_internal.h>
#include <cmath>
#include <vector>
namespace {
SDL_WindowID window_id{};
void record_save_geometry(ImGuiContext *,ImGuiContextHook *hook) {
    auto *window=ImGui::FindWindowByName("Save data");
    if(window && window->Active && !window->Hidden)
        static_cast<std::vector<ImVec2>*>(hook->UserData)->push_back(window->Size);
}
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
Uint32 SDLCALL choose_default(void *, SDL_TimerID, Uint32) {
    key(SDL_SCANCODE_RETURN,SDLK_RETURN);
    SDL_Event released{};released.type=SDL_EVENT_KEY_UP;released.key.windowID=window_id;
    released.key.scancode=SDL_SCANCODE_RETURN;released.key.key=SDLK_RETURN;SDL_PushEvent(&released);
    return 100; // Retry after navigation initializes, including a slow first GPU frame.
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
        // Exercise the real GPU-image presentation path, including a framebuffer
        // whose alpha is shadow data rather than display transparency.
        sims2::gpu::DrawCall scene{};scene.through=true;scene.has_vertex_color=true;
        scene.primitive=sims2::gpu::PrimitiveType::Sprites;
        scene.target={frame.address,512,3,0x04180000u,512};scene.vertices.resize(2);
        scene.vertices[0].position={0,0,0,1};scene.vertices[1].position={480,272,0,1};
        scene.vertices[1].color=0xffffffffu;renderer.draw(memory,scene);
        scene.vertices[1].color=0x40000000u;scene.blend.enabled=true;
        scene.blend.source_factor=2;scene.blend.destination_factor=3;
        renderer.draw(memory,scene);renderer.flush(memory);
        require(renderer.gpu_presentation(memory,frame.address,512,3)!=nullptr,
                "Window test did not exercise GPU presentation");
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
        require(sims2::display::save_slot_label("")=="Default save","Default slot has no visible label");
        const auto choose_timer=SDL_AddTimer(100,choose_default,nullptr);
        auto selected=window.select_savedata(2,{""});
        SDL_RemoveTimer(choose_timer);
        require(selected.has_value() && selected->empty(),"Default save label changed the actual slot name");
        SDL_Event released{};released.type=SDL_EVENT_KEY_UP;released.key.windowID=window_id;
        released.key.scancode=SDL_SCANCODE_RETURN;released.key.key=SDLK_RETURN;SDL_PushEvent(&released);
        window.present(memory,frame,55001,controller);
        std::vector<ImVec2> sizes;
        ImGuiContextHook hook{};hook.Type=ImGuiContextHookType_EndFramePost;
        hook.Callback=record_save_geometry;hook.UserData=&sizes;
        const auto hook_id=ImGui::AddContextHook(ImGui::GetCurrentContext(),&hook);
        SDL_AddTimer(250, close_dialog, nullptr);
        require(!window.select_savedata(2, {"TEST_SLOT"}), "Save selector cancellation failed");
        ImGui::RemoveContextHook(ImGui::GetCurrentContext(),hook_id);
        require(sizes.size()>=3,"Save geometry check did not observe enough frames");
        for(const auto &size:sizes)
            require(std::abs(size.x-sizes.front().x)<0.5f && std::abs(size.y-sizes.front().y)<0.5f,
                    "Save dialog size changed after appearing");
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
