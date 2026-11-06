#include "ctrl/host_input.hpp"
#include "ctrl/native_gamepad.hpp"
#include "display/native_window.hpp"
#include "display/presentation_limiter.hpp"
#include "display/settings_menu.hpp"
#include "gpu/native_renderer.hpp"
#include "psprecomp/common.hpp"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
namespace sims2::display {
namespace {
void check(bool value, const char *action) {
    if (!value)
        throw psprecomp::Error(std::string(action) + ": " + SDL_GetError());
}
} // namespace
struct NativeWindow::Impl : SettingsMenu {
    SDL_Window *window{};
    SDL_GPUDevice *device{};
    gpu::NativeRenderer *renderer{};
    SDL_GPUTexture *upload{};
    SDL_GPUSampler *nearest{}, *linear{};
    ImGuiContext *gui{};
    ImGuiStyle base;
    std::unique_ptr<ctrl::NativeGamepad> gamepad;
    ctrl::HostInput host_input;
    float wheel{};
    HostSettings current;
    std::filesystem::path settings_file = host_settings_path();
    std::function<void(float)> audio_control;
    bool video{}, claimed{}, platform{}, graphics{}, closed{}, paced{}, reset_pacing = true,
                                                                        suppress{};
    bool vsync = true;
    std::uint64_t frames{}, presentations{}, first_us{};
    PresentationLimiter limiter;
    std::chrono::steady_clock::time_point start{}, previous{};
    std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(480 * 272);
    SDL_GPUTextureSamplerBinding binding{};
    std::optional<std::string> slot_choice;
    bool slot_open{}, slot_done{};
    std::uint32_t slot_mode{};
    std::vector<std::string> slots;
    ~Impl() {
        if (device)
            SDL_WaitForGPUIdle(device);
        if (gui) {
            ImGui::SetCurrentContext(gui);
            if (graphics)
                ImGui_ImplSDLGPU3_Shutdown();
            if (platform)
                ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(gui);
        }
        gamepad.reset();
        if (device) {
            if (upload)
                SDL_ReleaseGPUTexture(device, upload);
            if (nearest)
                SDL_ReleaseGPUSampler(device, nearest);
            if (linear)
                SDL_ReleaseGPUSampler(device, linear);
            if (claimed)
                SDL_ReleaseWindowFromGPUDevice(device, window);
        }
        if (window)
            SDL_DestroyWindow(window);
        if (video)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    bool focused() const { return (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0; }
    void toggle() {
        opened = !opened;
        if (opened) {
            draft = current;
            focus = true;
            status.clear();
        }
        suppress = true;
        reset_pacing = true;
    }
    void events() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_MOUSE_WHEEL)
                wheel += event.wheel.y;
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                closed = true;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.scancode == SDL_SCANCODE_F1)
                    toggle();
                if (event.key.scancode == SDL_SCANCODE_F2)
                    current.show_performance = !current.show_performance;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                suppress = true;
                reset_pacing = true;
            }
        }
        gamepad->sample(false);
        if (gamepad->settings_toggle(focused() && !slot_open))
            toggle();
    }
    void camera() {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w <= 0 || h <= 0)
            return;
        renderer->set_render_scale(effective_render_scale(current, unsigned(h)));
        auto rect = picture_rect(unsigned(w), unsigned(h), current.aspect);
        renderer->set_display_aspect(
            current.match_camera_aspect && rect.height ? float(rect.width) / rect.height : 480.f / 272);
    }
    void save_menu() {
        if (!slot_open)
            return;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f, .5f});
        ImGui::SetNextWindowSize({std::min(580.f, float(width) - 24), 0}, ImGuiCond_Appearing);
        if (ImGui::Begin("Save data", nullptr,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextUnformatted(slot_mode == 2 || slot_mode == 4 ? "Choose a save to load"
                                   : slot_mode == 6                 ? "Choose a save to delete"
                                                                    : "Choose a save slot");
            if (slot_mode == 6)
                ImGui::TextWrapped("Selecting a slot deletes its saved game.");
            else if (slot_mode == 3 || slot_mode == 5)
                ImGui::TextWrapped("Selecting an existing slot replaces its saved game.");
            for (auto &slot : slots)
                if (ImGui::Button(slot.c_str(), {-1, 0})) {
                    slot_choice = slot;
                    slot_done = true;
                }
            if (ImGui::Button("Cancel", {-1, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
                slot_choice.reset();
                slot_done = true;
            }
        }
        ImGui::End();
    }
    SettingsAction render(bool game_frame, const psprecomp::GuestMemory *memory = nullptr,
                          const Display::Framebuffer *frame = nullptr) {
        int w{}, h{};
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w <= 0 || h <= 0) {
            SDL_Delay(16);
            return {};
        }
        width = unsigned(w);
        height = unsigned(h);
        if (vsync != current.vsync) {
            auto mode = current.vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
            if (!SDL_WindowSupportsGPUPresentMode(device, window, mode))
                mode = SDL_GPU_PRESENTMODE_VSYNC;
            check(SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode),
                  "Set Vulkan present mode");
            vsync = current.vsync;
        }
        ImGui::SetCurrentContext(gui);
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui::NewFrame();
        auto *commands = SDL_AcquireGPUCommandBuffer(device);
        check(commands, "Acquire Vulkan output commands");
        SDL_GPUTexture *swap{};
        Uint32 sw{}, sh{};
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swap, &sw, &sh)) {
            SDL_CancelGPUCommandBuffer(commands);
            check(false, "Acquire Vulkan swapchain");
        }
        if (!swap) {
            SDL_CancelGPUCommandBuffer(commands);
            ImGui::EndFrame();
            SDL_Delay(16);
            return {};
        }
        SDL_GPUTexture *image = nullptr;
        float u = 1;
        if (memory && frame) {
            image = static_cast<SDL_GPUTexture *>(renderer->gpu_presentation(
                *memory, frame->address, frame->buffer_width, frame->pixel_format));
            if (image)
                u = 480.f / 512;
        }
        if (!image) {
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = 480 * 272 * 4;
            auto *transfer = SDL_CreateGPUTransferBuffer(device, &info);
            check(transfer, "Create output upload");
            auto *mapped = static_cast<std::uint32_t *>(SDL_MapGPUTransferBuffer(device, transfer, false));
            check(mapped, "Map output upload");
            for (unsigned i = 0; i < pixels.size(); ++i) {
                auto p = pixels[i];
                mapped[i] = (p & 0xff00ff00u) | ((p & 255) << 16) | ((p >> 16) & 255) | 0xff000000u;
            }
            SDL_UnmapGPUTransferBuffer(device, transfer);
            auto *copy = SDL_BeginGPUCopyPass(commands);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer;
            src.pixels_per_row = 480;
            src.rows_per_layer = 272;
            SDL_GPUTextureRegion dst{};
            dst.texture = upload;
            dst.w = 480;
            dst.h = 272;
            dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, true);
            SDL_EndGPUCopyPass(copy);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            image = upload;
        }
        binding = {image, current.filter == OutputFilter::Linear ? linear : nearest};
        auto rect = picture_rect(sw, sh, current.aspect);
        // ImGui uses logical coordinates; swapchain and aspect calculations use drawable pixels.
        auto size = ImGui::GetIO().DisplaySize;
        const float sx = size.x / sw, sy = size.y / sh;
        ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)&binding, {rect.x * sx, rect.y * sy},
                                                 {(rect.x + rect.width) * sx, (rect.y + rect.height) * sy},
                                                 {0, 0}, {u, 1});
        backend = "Vulkan / SDL3";
        hdr_supported = false;
        output_status = "SDR output. HDR is not enabled in this Vulkan backend.";
        hdr_active = false;
        if (game_frame && !opened) {
            auto now = std::chrono::steady_clock::now();
            if (previous.time_since_epoch().count()) {
                times[sample++ % times.size()] =
                    std::chrono::duration<float, std::milli>(now - previous).count();
                count = std::min(unsigned(times.size()), count + 1);
            }
            previous = now;
        }
        if (opened)
            previous = {};
        auto logical = ImGui::GetIO().DisplaySize;
        const float desired = std::clamp(std::round(std::max(1.f, logical.y / 1080.f) * 4) / 4, 1.f, 3.f);
        if (scale != desired) {
            scale = desired;
            ImGui::GetStyle() = base;
            ImGui::GetStyle().ScaleAllSizes(scale);
            ImGui::GetIO().FontGlobalScale = scale;
        }
        width = unsigned(logical.x);
        height = unsigned(logical.y);
        SettingsAction action = menu(current);
        save_menu();
        performance_panel(current, sw, sh);
        input_panel(current);
        ImGui::Render();
        Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commands);
        SDL_GPUColorTargetInfo target{};
        target.texture = swap;
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        target.clear_color = {0, 0, 0, 1};
        auto *pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        check(pass, "Begin Vulkan output pass");
        ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commands, pass);
        SDL_EndGPURenderPass(pass);
        check(SDL_SubmitGPUCommandBuffer(commands), "Present Vulkan output");
        ++presentations;
        return action;
    }
};
NativeWindow::NativeWindow(bool visible, gpu::NativeRenderer *renderer, bool hidden)
    : impl_(std::make_unique<Impl>()) {
    auto &p = *impl_;
    p.renderer = renderer;
    check(renderer, "Vulkan renderer required");
    p.device = static_cast<SDL_GPUDevice *>(renderer->gpu_device());
    p.paced = visible || std::getenv("SIMS2_PACE");
    check(SDL_InitSubSystem(SDL_INIT_VIDEO), "Initialize SDL video");
    p.video = true;
    auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!visible || hidden)
        flags |= SDL_WINDOW_HIDDEN;
    p.window = SDL_CreateWindow("The Sims 2 PSP", 960, 544, flags);
    check(p.window, "Create SDL window");
    SDL_SetWindowMinimumSize(p.window, 480, 272);
    check(SDL_ClaimWindowForGPUDevice(p.device, p.window), "Create Vulkan surface");
    p.claimed = true;
    check(SDL_SetGPUSwapchainParameters(p.device, p.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                        SDL_GPU_PRESENTMODE_VSYNC),
          "Configure Vulkan swapchain");
    p.gamepad = std::make_unique<ctrl::NativeGamepad>();
    IMGUI_CHECKVERSION();
    p.gui = ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFontConfig font;
    font.SizePixels = 18;
    io.Fonts->AddFontDefault(&font);
    p.base = settings_ui::initialize_style();
    check(ImGui_ImplSDL3_InitForSDLGPU(p.window), "Initialize SDL settings input");
    p.platform = true;
    ImGui_ImplSDLGPU3_InitInfo init{};
    init.Device = p.device;
    init.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(p.device, p.window);
    check(ImGui_ImplSDLGPU3_Init(&init), "Initialize Vulkan settings renderer");
    p.graphics = true;
    SDL_GPUTextureCreateInfo texture{};
    texture.type = SDL_GPU_TEXTURETYPE_2D;
    texture.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture.width = 480;
    texture.height = 272;
    texture.layer_count_or_depth = 1;
    texture.num_levels = 1;
    texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
    p.upload = SDL_CreateGPUTexture(p.device, &texture);
    check(p.upload, "Create SDL framebuffer upload");
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    p.nearest = SDL_CreateGPUSampler(p.device, &sampler);
    check(p.nearest, "Create nearest output sampler");
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    p.linear = SDL_CreateGPUSampler(p.device, &sampler);
    check(p.linear, "Create linear output sampler");
    apply_settings(load_host_settings(p.settings_file));
}
NativeWindow::~NativeWindow() = default;
void *NativeWindow::native_handle() const noexcept { return impl_->window; }
std::uint64_t NativeWindow::presented_frames() const noexcept { return impl_->presentations; }
HostSettings NativeWindow::settings() const { return impl_->current; }
void NativeWindow::set_audio_control(std::function<void(float)> control) {
    impl_->audio_control = std::move(control);
    if (impl_->audio_control)
        impl_->audio_control(impl_->current.muted ? 0 : impl_->current.volume / 100.f);
}
void NativeWindow::apply_settings(HostSettings settings) {
    auto &p = *impl_;
    settings = validated_settings(settings);
    const auto old = p.current;
    p.current = settings;
    if (old.mode != settings.mode || old.width != settings.width || old.height != settings.height ||
        !p.frames) {
        check(SDL_SetWindowFullscreen(p.window, false), "Leave fullscreen");
        const auto *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(p.window));
        int w = int(settings.width ? settings.width
                    : desktop      ? desktop->w
                                   : 960),
            h = int(settings.height ? settings.height
                    : desktop       ? desktop->h
                                    : 544);
        if (settings.mode == WindowMode::Windowed) {
            SDL_SetWindowSize(p.window, w, h);
        } else {
            SDL_DisplayMode mode{};
            const bool exclusive = settings.mode == WindowMode::Fullscreen &&
                                   SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(p.window), w,
                                                                       h, 0, false, &mode) &&
                                   SDL_SetWindowFullscreenMode(p.window, &mode);
            if (!exclusive)
                SDL_SetWindowFullscreenMode(p.window, nullptr);
            check(SDL_SetWindowFullscreen(p.window, true), "Set fullscreen");
        }
    }
    p.gamepad->set_options(settings.stick_deadzone, settings.right_stick_camera);
    p.gamepad->set_input_options(settings.input);
    p.host_input.release();
    p.renderer->set_anisotropy(settings.anisotropy);
    p.camera();
    p.limiter.reset();
    p.reset_pacing = true;
    if (p.audio_control)
        p.audio_control(settings.muted ? 0 : settings.volume / 100.f);
}
void NativeWindow::synchronize(std::uint64_t now_us) {
    auto &p = *impl_;
    if (!p.paced)
        return;
    auto now = std::chrono::steady_clock::now();
    if (p.reset_pacing ||
        now > p.start + std::chrono::microseconds(now_us - p.first_us) + std::chrono::milliseconds(250)) {
        p.start = now;
        p.first_us = now_us;
        p.reset_pacing = false;
    }
    std::this_thread::sleep_until(p.start + std::chrono::microseconds(now_us - p.first_us));
}
bool NativeWindow::present(const psprecomp::GuestMemory &memory, const Display::Framebuffer &frame,
                           std::uint64_t now_us, ctrl::Controller &controller) {
    auto &p = *impl_;
    p.events();
    if (p.closed)
        return false;
    auto pad = p.gamepad->sample(p.focused() && !p.opened);
    const bool *keys = SDL_GetKeyboardState(nullptr);
    auto key = [&](unsigned code) {
        SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
        if (code >= 'A' && code <= 'Z')
            sc = SDL_Scancode(SDL_SCANCODE_A + code - 'A');
        else if (code >= '1' && code <= '9')
            sc = SDL_Scancode(SDL_SCANCODE_1 + code - '1');
        else if (code == '0')
            sc = SDL_SCANCODE_0;
        else if (code == ' ')
            sc = SDL_SCANCODE_SPACE;
        else if (code >= ctrl::Enter && code <= ctrl::ArrowRight) {
            constexpr SDL_Scancode special[]{SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_ESCAPE,
                                             SDL_SCANCODE_TAB,    SDL_SCANCODE_UP,        SDL_SCANCODE_DOWN,
                                             SDL_SCANCODE_LEFT,   SDL_SCANCODE_RIGHT};
            sc = special[code - ctrl::Enter];
        }
        return sc != SDL_SCANCODE_UNKNOWN && keys[sc];
    };
    ctrl::MouseSnapshot mouse;
    auto buttons = SDL_GetMouseState(&mouse.x, &mouse.y);
    mouse.right = (buttons & SDL_BUTTON_RMASK) != 0;
    mouse.wheel = p.wheel;
    p.wheel = 0;
    const auto host_us = std::uint64_t(SDL_GetTicksNS() / 1000);
    pad = p.host_input.sample(p.current.input, key, mouse, pad, p.focused() && !p.opened && !p.slot_open,
                              host_us);
    controller.set_pad(pad, now_us);
    const auto bpp = frame.pixel_format == 3 ? 4u : 2u;
    for (unsigned y = 0; y < 272; ++y)
        for (unsigned x = 0; x < 480; ++x) {
            auto address = frame.address + (y * frame.buffer_width + x) * bpp;
            if (!memory.contains(address, bpp))
                continue;
            auto raw = bpp == 4 ? memory.load32(address) : memory.load16(address);
            unsigned r{}, g{}, b{};
            if (frame.pixel_format == 3) {
                r = raw & 255;
                g = (raw >> 8) & 255;
                b = (raw >> 16) & 255;
            } else if (frame.pixel_format == 0) {
                r = (raw & 31) * 255 / 31;
                g = ((raw >> 5) & 63) * 255 / 63;
                b = ((raw >> 11) & 31) * 255 / 31;
            } else if (frame.pixel_format == 1) {
                r = (raw & 31) * 255 / 31;
                g = ((raw >> 5) & 31) * 255 / 31;
                b = ((raw >> 10) & 31) * 255 / 31;
            } else {
                r = (raw & 15) * 17;
                g = ((raw >> 4) & 15) * 17;
                b = ((raw >> 8) & 15) * 17;
            }
            p.pixels[y * 480 + x] = (r << 16) | (g << 8) | b;
        }
    ++p.frames;
    const auto *at = std::getenv("SIMS2_CAPTURE_FRAME");
    if (p.frames == (at ? std::strtoull(at, nullptr, 10) : 60))
        if (auto *path = std::getenv("SIMS2_CAPTURE_BMP"))
            capture(path);
    p.camera();
    synchronize(now_us);
    auto render = [&](bool game) {
        auto action = p.render(game, &memory, &frame);
        if (action.apply)
            apply_settings(action.settings);
        if (action.save) {
            std::string error;
            p.status = save_host_settings(p.settings_file, p.current, error) ? "Settings saved" : error;
        }
    };
    if (p.opened || p.limiter.due(now_us, p.current.fps_cap))
        render(true);
    bool paused = p.opened;
    while (p.opened && !p.closed) {
        p.events();
        p.gamepad->sample(false);
        controller.set_pad({}, now_us);
        render(false);
        SDL_Delay(8);
    }
    if (paused) {
        p.wheel = 0;
        p.host_input.release();
        p.suppress = true;
        p.host_input.release();
        p.reset_pacing = true;
    }
    return !p.closed;
}
std::optional<std::string> NativeWindow::select_savedata(std::uint32_t mode,
                                                         const std::vector<std::string> &slots) {
    auto &p = *impl_;
    p.host_input.release();
    p.wheel = 0;
    p.slot_mode = mode;
    p.slots = slots;
    p.slot_open = true;
    p.slot_done = false;
    p.slot_choice.reset();
    while (!p.slot_done && !p.closed) {
        p.events();
        p.gamepad->sample(false);
        p.render(false);
        SDL_Delay(8);
    }
    p.slot_open = false;
    p.host_input.release();
    p.wheel = 0;
    p.suppress = true;
    p.reset_pacing = true;
    return p.slot_choice;
}
void NativeWindow::capture(const std::string &path) const {
    const auto &pixels = impl_->pixels;
    std::ofstream out(path, std::ios::binary);
    const unsigned size = 54 + 480 * 272 * 4;
    unsigned char header[54]{};
    header[0] = 'B';
    header[1] = 'M';
    auto put = [&](unsigned offset, std::uint32_t n) {
        for (unsigned i = 0; i < 4; ++i)
            header[offset + i] = static_cast<unsigned char>(n >> (8 * i));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, 480);
    put(22, std::uint32_t(-272));
    header[26] = 1;
    header[28] = 32;
    out.write(reinterpret_cast<char *>(header), 54);
    out.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * 4);
    if (!out)
        throw psprecomp::Error("Cannot write framebuffer capture");
}
} // namespace sims2::display
