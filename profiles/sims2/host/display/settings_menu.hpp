#pragma once
#include "display/settings_overlay.hpp"
#include "display/frame_history.hpp"
#include "kernel/guest_clock.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <string>
namespace sims2::display {
namespace settings_ui {
inline ImGuiStyle initialize_style() {
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 9;
    style.FrameRounding = 5;
    style.WindowPadding = {18, 16};
    style.ItemSpacing = {10, 8};
    style.FramePadding = {10, 6};
    style.Colors[ImGuiCol_WindowBg] = {.06f, .07f, .09f, .98f};
    style.Colors[ImGuiCol_Header] = {.29f, .56f, .78f, .4f};
    return style;
}
constexpr std::array<unsigned, 2> resolutions[]{{960, 544},   {1280, 800},  {1280, 720},  {1600, 900},
                                                {1680, 1050}, {1920, 1200}, {2560, 1600}, {3840, 1080},
                                                {3840, 1600}, {1920, 1080}, {2560, 1080}, {2560, 1440},
                                                {3440, 1440}, {3840, 2160}, {5120, 1440}};
inline void row(const char *label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
}
inline unsigned render_dimension(float value) { return unsigned(std::max(1l, std::lround(value))); }
inline void note(const char *text) {
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}
} // namespace settings_ui
struct SettingsMenu : FrameHistory {
    unsigned width{}, height{};
    float scale{1};
    HostSettings draft;
    int capture_action = -1;
    bool opened{}, focus{}, hdr_active{}, hdr_supported{true};
    std::string status, output_status, backend = "Direct3D 11";
    void performance_panel(const HostSettings &current, unsigned output_width, unsigned output_height) const {
        if (!current.show_performance)
            return;
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 12, 12}, ImGuiCond_Always, {1, 0});
        ImGui::SetNextWindowBgAlpha(.65f);
        if (ImGui::Begin("Performance (F2)", nullptr,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoSavedSettings)) {
            const auto timing = summary();
            const auto average = timing.average, lo = timing.minimum, hi = timing.maximum;
            ImGui::Text("%s | Present %.1f FPS | %.2f ms", opened ? "Paused" : "Running",
                        average > 0 ? 1000 / average : 0, average);
            ImGui::Text("%s | output %u x %u", backend.c_str(), output_width, output_height);
            auto factor = effective_render_scale(current, output_height);
            ImGui::Text("Render %u x %u | aspect %s", settings_ui::render_dimension(480 * factor),
                        settings_ui::render_dimension(272 * factor), aspect_name(current.aspect).data());
            ImGui::Text("VSync %s | cap %u | %s", current.vsync ? "On" : "Off", current.fps_cap,
                        hdr_active ? "HDR" : "SDR");
            ImGui::Text("Engine %.2f Hz%s", kernel::guest_display_frequency(),
                        kernel::guest_display_hz ? " (experimental)" : "");
            ImGui::Text("%.2f - %.2f ms", lo, hi);
            ImGui::PlotLines("##times", times.data(), int(count),
                             count == times.size() ? int(sample % times.size()) : 0, nullptr, 0,
                             std::max(40.f, hi * 1.2f), {280, 60});
        }
        ImGui::End();
    }
    void input_panel(const HostSettings &current) const {
        if (!current.input.input_hints || opened)
            return;
        ImGui::SetNextWindowPos({12, ImGui::GetIO().DisplaySize.y - 12}, ImGuiCond_Always, {0, 1});
        ImGui::SetNextWindowBgAlpha(.65f);
        if (ImGui::Begin("Input hints", nullptr,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("Keyboard: %s interact | %s cancel",
                        ctrl::key_name(current.input.keys[ctrl::Cross]).c_str(),
                        ctrl::key_name(current.input.keys[ctrl::Circle]).c_str());
            ImGui::Text("Move %s%s%s%s | camera %s/%s or RMB drag",
                        ctrl::key_name(current.input.keys[ctrl::AnalogUp]).c_str(),
                        ctrl::key_name(current.input.keys[ctrl::AnalogLeft]).c_str(),
                        ctrl::key_name(current.input.keys[ctrl::AnalogDown]).c_str(),
                        ctrl::key_name(current.input.keys[ctrl::AnalogRight]).c_str(),
                        ctrl::key_name(current.input.keys[ctrl::LeftShoulder]).c_str(),
                        ctrl::key_name(current.input.keys[ctrl::RightShoulder]).c_str());
            auto physical = ctrl::controller_names(current.input.controller_labels);
            ImGui::Text("Controller: %s interact | %s cancel", physical[current.input.gamepad[ctrl::Cross]],
                        physical[current.input.gamepad[ctrl::Circle]]);
            ImGui::TextUnformatted(
                "PSP prompts: Cross = interact; Circle = cancel. F1 settings / F2 performance.");
        }
        ImGui::End();
    }
    SettingsAction menu(const HostSettings &current) {
        using namespace settings_ui;
        SettingsAction result;
        if (!opened)
            return result;
        ImGui::SetNextWindowSize(
            {std::min(840.0f * scale, float(width) - 32), std::min(740.0f * scale, float(height) - 32)},
            ImGuiCond_Appearing);
        ImGui::SetNextWindowPos({float(width) * .5f, float(height) * .5f}, ImGuiCond_Appearing, {.5f, .5f});
        if (focus) {
            ImGui::SetNextWindowFocus();
            focus = false;
        }
        if (ImGui::Begin("The Sims 2 PSP - Settings (F1)", &opened, ImGuiWindowFlags_NoCollapse)) {
            note("The game is paused while this menu is open.");
            ImGui::BeginChild("options", {0, -70.0f * scale});
            if (ImGui::BeginTabBar("tabs")) {
                if (ImGui::BeginTabItem("Display")) {
                    if (ImGui::BeginTable("display", 2, ImGuiTableFlags_SizingFixedFit)) {
                        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 210 * scale);
                        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
                        row("Window mode");
                        int mode = int(draft.mode);
                        if (ImGui::Combo("##mode", &mode,
                                         "Windowed\0Borderless fullscreen\0Fullscreen (exclusive)\0"))
                            draft.mode = WindowMode(mode);
                        row("Output resolution");
                        char label[64];
                        std::snprintf(label, sizeof(label), draft.width ? "%u x %u" : "Desktop (automatic)",
                                      draft.width, draft.height);
                        if (ImGui::BeginCombo("##resolution", label)) {
                            if (ImGui::Selectable("Desktop (automatic)", !draft.width)) {
                                draft.width = draft.height = 0;
                            }
                            for (const auto &size : resolutions) {
                                std::snprintf(label, sizeof(label), "%u x %u", size[0], size[1]);
                                if (ImGui::Selectable(label,
                                                      draft.width == size[0] && draft.height == size[1])) {
                                    draft.width = size[0];
                                    draft.height = size[1];
                                }
                            }
                            ImGui::EndCombo();
                        }
                        row("Custom width");
                        int custom_width = int(draft.width);
                        if (ImGui::InputInt("##width", &custom_width, 0, 0))
                            draft.width = unsigned(std::max(custom_width, 0));
                        row("Custom height");
                        int custom_height = int(draft.height);
                        if (ImGui::InputInt("##height", &custom_height, 0, 0))
                            draft.height = unsigned(std::max(custom_height, 0));
                        note("0 uses the desktop size. Borderless fills the monitor; exclusive requests a "
                             "display mode.");
                        row("Aspect ratio");
                        int aspect = int(draft.aspect);
                        if (ImGui::Combo("##aspect", &aspect,
                                         "PSP (480:272)\0"
                                         "16:9 widescreen\0Auto (window shape)\0"
                                         "21:9 ultrawide\0"
                                         "4:3\0"
                                         "16:10\0"
                                         "32:9 super ultrawide\0"))
                            draft.aspect = AspectMode(aspect);
                        row("Match camera aspect");
                        ImGui::Checkbox("##camera", &draft.match_camera_aspect);
                        note("Fits 3D framing to the selected aspect and keeps the HUD centered. Camera "
                             "changes appear after resuming.");

                        row("Vertical sync");
                        ImGui::Checkbox("##vsync", &draft.vsync);
                        row("Frame rate cap");
                        char fps_text[48];
                        std::snprintf(fps_text, sizeof(fps_text), draft.fps_cap ? "%u fps" : "Unlimited",
                                      draft.fps_cap);
                        if (ImGui::BeginCombo("##fps_cap", fps_text)) {
                            for (unsigned cap : {0u, 30u, 60u, 72u, 120u, 144u, 165u, 240u}) {
                                std::snprintf(fps_text, sizeof(fps_text), cap ? "%u fps" : "Unlimited", cap);
                                if (ImGui::Selectable(fps_text, draft.fps_cap == cap))
                                    draft.fps_cap = cap;
                            }
                            ImGui::EndCombo();
                        }
                        note("Limits presentation. Select an engine rate below to render additional game frames.");
                        row("Engine frame rate");
                        int engine = draft.engine_fps == 240u ? 2 : draft.engine_fps == 120u ? 1 : 0;
                        if (ImGui::Combo("##engine_fps", &engine,
                                         "Native (~60)\0" "120 (experimental)\0" "240 (experimental)\0"))
                            draft.engine_fps = engine == 2 ? 240u : engine == 1 ? 120u : 0u;
                        note("Save and restart to apply. High rates need more GPU/CPU power. "
                             "Experimental: use Native if gameplay or audio behaves incorrectly. "
                             "VSync and the presentation cap can limit visible FPS.");
                        if(draft.engine_fps!=kernel::guest_display_hz)
                            note("Engine rate change pending: Save, close the game, then launch again.");
                        ImGui::BeginDisabled(!hdr_supported);
                        row("HDR");
                        int hdr = int(draft.hdr);
                        if (ImGui::Combo("##hdr", &hdr, "Off\0Auto\0On\0"))
                            draft.hdr = HdrMode(hdr);
                        ImGui::EndDisabled();
                        note(output_status.c_str());
                        ImGui::BeginDisabled(!hdr_active);
                        row("Paper white");
                        int white = int(draft.paper_white);
                        if (ImGui::SliderInt("##white", &white, 80, 480, "%d nits"))
                            draft.paper_white = unsigned(white);
                        row("Peak brightness");
                        int peak = int(draft.peak_nits);
                        if (ImGui::SliderInt("##peak", &peak, 0, 10000, peak ? "%d nits" : "Display maximum"))
                            draft.peak_nits = peak < 80 ? 0 : unsigned(peak);
                        ImGui::EndDisabled();
                        note("HDR maps the original SDR artwork into HDR output. HDR availability depends on "
                             "the active backend and monitor.");
                        ImGui::EndTable();
                    }
                    ImGui::Separator();
                    ImGui::Text("Active: %s | %u x %u output", backend.c_str(), width, height);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Quality")) {
                    if (ImGui::BeginTable("quality", 2, ImGuiTableFlags_SizingFixedFit)) {
                        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 210 * scale);
                        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
                        bool percentage = draft.render_percent != 0;
                        row("Output-relative scale");
                        if (ImGui::Checkbox("##relative", &percentage))
                            draft.render_percent = percentage ? 100 : 0;
                        ImGui::BeginDisabled(percentage);
                        row("Internal resolution");
                        const char *scales[]{"1x - 480 x 272 (native)", "2x - 960 x 544",
                                             "3x - 1440 x 816",         "4x - 1920 x 1088",
                                             "5x - 2400 x 1360",        "6x - 2880 x 1632",
                                             "7x - 3360 x 1904",        "8x - 3840 x 2176"};
                        int render_scale = int(draft.render_scale) - 1;
                        if (ImGui::Combo("##render_scale", &render_scale, scales, 8))
                            draft.render_scale = unsigned(render_scale + 1);
                        note("Higher values render geometry in greater detail and use more GPU resources.");
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(!percentage);
                        row("Render scale");
                        int percent = int(draft.render_percent ? draft.render_percent : 100);
                        if (ImGui::SliderInt("##percent", &percent, 50, 200, "%d %%"))
                            draft.render_percent = unsigned(percent);
                        ImGui::EndDisabled();
                        const auto output_height =
                            draft.mode == WindowMode::Windowed && draft.height ? draft.height : height;
                        const auto factor = effective_render_scale(draft, output_height);
                        ImGui::Text("Render target: %u x %u (%.2fx PSP)", render_dimension(480 * factor),
                                    render_dimension(272 * factor), factor);
                        row("Anisotropic filtering");
                        const unsigned anisotropy[]{0, 2, 4, 8, 16};
                        int aniso = 0;
                        for (int i = 0; i < 5; ++i)
                            if (draft.anisotropy == anisotropy[i])
                                aniso = i;
                        if (ImGui::Combo("##aniso", &aniso,
                                         "As the game asks\0"
                                         "2x\0"
                                         "4x\0"
                                         "8x\0"
                                         "16x\0"))
                            draft.anisotropy = anisotropy[aniso];
                        note("Applies to filtered 3D textures; preserves the game's unfiltered UI artwork.");
                        row("Output filtering");
                        int filtering = int(draft.filter);
                        if (ImGui::Combo("##filter", &filtering, "Nearest\0Bilinear\0"))
                            draft.filter = OutputFilter(filtering);
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Performance")) {
                    const auto timing = summary();
                    const auto average = timing.average, lo = timing.minimum, hi = timing.maximum;
                    ImGui::Text("Presented: %.1f FPS | average %.2f ms", average > 0 ? 1000 / average : 0,
                                average);
                    ImGui::Text("Frame times: %.2f - %.2f ms", lo, hi);
                    ImGui::Text("Output: %u x %u | render: %u x %u", width, height,
                                render_dimension(480 * effective_render_scale(current, height)),
                                render_dimension(272 * effective_render_scale(current, height)));
                    ImGui::Text("VSync: %s | Cap: %u (0 = unlimited)", current.vsync ? "On" : "Off",
                                current.fps_cap);
                    note(output_status.c_str());
                    ImGui::Checkbox("Show performance overlay (F2)", &draft.show_performance);
                    ImGui::PlotLines("##f1times", times.data(), int(count),
                                     count == times.size() ? int(sample % times.size()) : 0, nullptr, 0,
                                     std::max(40.0f, hi * 1.2f), {0, 100 * scale});
                    note("The game pauses while this menu is open. These are the most recent presented-frame "
                         "measurements, including stalls. This is presentation cadence, not engine FPS.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Audio")) {
                    int volume = int(draft.volume);
                    if (ImGui::SliderInt("Master volume", &volume, 0, 100, "%d%%"))
                        draft.volume = unsigned(volume);
                    ImGui::Checkbox("Mute", &draft.muted);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Controls")) {
                    int deadzone = int(draft.stick_deadzone);
                    if (ImGui::SliderInt("Stick deadzone", &deadzone, 5, 40, "%d%%"))
                        draft.stick_deadzone = unsigned(deadzone);
                    ImGui::Checkbox("Right stick camera (left/right)", &draft.right_stick_camera);
                    note("Maps right-stick horizontal movement to the PSP L/R camera buttons.");
                    int threshold = int(draft.input.camera_threshold);
                    if (ImGui::SliderInt("Right stick activation", &threshold, 5, 95, "%d %%"))
                        draft.input.camera_threshold = unsigned(threshold);
                    ImGui::Checkbox("Invert camera left/right", &draft.input.invert_camera);
                    ImGui::Checkbox("Mouse camera (hold right button)", &draft.input.mouse_camera);
                    int sensitivity = int(draft.input.mouse_sensitivity),
                        pulse = int(draft.input.mouse_pulse_ms);
                    if (ImGui::SliderInt("Mouse sensitivity", &sensitivity, 10, 300, "%d %%"))
                        draft.input.mouse_sensitivity = unsigned(sensitivity);
                    if (ImGui::SliderInt("Camera pulse length", &pulse, 20, 250, "%d ms"))
                        draft.input.mouse_pulse_ms = unsigned(pulse);
                    note("PSP camera rotation uses L/R buttons. Mouse drag and right stick emulate those "
                         "buttons; this is not an analog zoom patch.");
                    bool repeat = draft.input.menu_repeat_ms != 0;
                    if (ImGui::Checkbox("Limit held D-pad repeat", &repeat))
                        draft.input.menu_repeat_ms = repeat ? 160 : 0;
                    if (repeat) {
                        int ms = int(draft.input.menu_repeat_ms);
                        if (ImGui::SliderInt("Repeat interval", &ms, 80, 500, "%d ms"))
                            draft.input.menu_repeat_ms = unsigned(ms);
                    }
                    note("Repeat filtering affects every D-pad action; analog movement is unchanged. Off "
                         "preserves native holds.");
                    ImGui::Checkbox("Show input hints", &draft.input.input_hints);
                    if (ImGui::Button("Classic keyboard")) {
                        draft.input.keys = ctrl::classic_keys;
                        capture_action = -1;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("WASD keyboard")) {
                        draft.input.keys = ctrl::wasd_keys;
                        capture_action = -1;
                    }
                    if (ImGui::BeginTable("keybindings", 2, ImGuiTableFlags_SizingStretchProp)) {
                        for (unsigned i = 0; i < ctrl::ActionCount; ++i) {
                            ImGui::PushID(int(i));
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(ctrl::action_names[i]);
                            ImGui::TableNextColumn();
                            const auto label = capture_action == int(i) ? std::string("Press a key...")
                                                                        : ctrl::key_name(draft.input.keys[i]);
                            if (ImGui::Button(label.c_str(), {-1, 0}))
                                capture_action = int(i);
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    if (capture_action >= 0) {
                        note("Press a letter, number, Space, Enter, Backspace, Tab or arrow key. Delete "
                             "unbinds; Escape cancels. F1/F2 stay reserved.");
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                            capture_action = -1;
                        else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                            draft.input.keys[unsigned(capture_action)] = 0;
                            capture_action = -1;
                        } else {
                            unsigned code = 0;
                            for (int k = int(ImGuiKey_A); k <= int(ImGuiKey_Z); ++k)
                                if (ImGui::IsKeyPressed(ImGuiKey(k)))
                                    code = unsigned('A' + k - int(ImGuiKey_A));
                            for (int k = int(ImGuiKey_0); k <= int(ImGuiKey_9); ++k)
                                if (ImGui::IsKeyPressed(ImGuiKey(k)))
                                    code = unsigned('0' + k - int(ImGuiKey_0));
                            const std::pair<ImGuiKey, unsigned> special[]{
                                {ImGuiKey_Space, ' '},
                                {ImGuiKey_Enter, ctrl::Enter},
                                {ImGuiKey_Backspace, ctrl::Backspace},
                                {ImGuiKey_Tab, ctrl::Tab},
                                {ImGuiKey_UpArrow, ctrl::ArrowUp},
                                {ImGuiKey_DownArrow, ctrl::ArrowDown},
                                {ImGuiKey_LeftArrow, ctrl::ArrowLeft},
                                {ImGuiKey_RightArrow, ctrl::ArrowRight}};
                            for (auto [key, value] : special)
                                if (ImGui::IsKeyPressed(key))
                                    code = value;
                            if (code) {
                                draft.input.keys[unsigned(capture_action)] = code;
                                capture_action = -1;
                            }
                        }
                    }
                    ImGui::SeparatorText("Controller button bindings");
                    int labels = int(draft.input.controller_labels);
                    if (ImGui::Combo("Controller labels", &labels,
                                     "Xbox / Steam Deck\0PlayStation\0Nintendo\0Physical positions\0"))
                        draft.input.controller_labels = unsigned(labels);
                    auto pad_names = ctrl::controller_names(draft.input.controller_labels);
                    if (ImGui::Button("Default controller"))
                        draft.input.gamepad = ctrl::default_gamepad;
                    for (unsigned i = 0; i < 12; ++i) {
                        ImGui::PushID(100 + int(i));
                        int button = int(draft.input.gamepad[i]);
                        if (ImGui::Combo(ctrl::action_names[i], &button, pad_names, 15))
                            draft.input.gamepad[i] = unsigned(button);
                        ImGui::PopID();
                    }
                    for (unsigned i = 0; i < 2; ++i) {
                        auto &binding = i ? draft.input.wheel_down : draft.input.wheel_up;
                        int action = int(binding);
                        if (ImGui::Combo(i ? "Wheel down" : "Wheel up", &action, ctrl::action_names.data(),
                                         12))
                            binding = unsigned(action);
                    }
                    note("F1 / SDL L3+R3: settings. F2: performance. Bindings apply after Apply/Save. Guide "
                         "may be reserved by the operating system.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild();
            ImGui::Separator();
            if (ImGui::Button("Apply", {105 * scale, 0})) {
                result.apply = true;
                result.settings = validated_settings(draft);
            }
            ImGui::SameLine();
            if (ImGui::Button("Save", {105 * scale, 0})) {
                result.apply = result.save = true;
                result.settings = validated_settings(draft);
            }
            ImGui::SameLine();
            if (ImGui::Button("Defaults", {105 * scale, 0}))
                draft = HostSettings{};
            ImGui::SameLine();
            if (ImGui::Button("Close", {105 * scale, 0}))
                opened = false;
            if (!status.empty())
                note(status.c_str());
        }
        ImGui::End();
        return result;
    }
};
} // namespace sims2::display
