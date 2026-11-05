#pragma once
#include "display/settings_overlay.hpp"
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
    auto &style=ImGui::GetStyle();
    style.WindowRounding=9;style.FrameRounding=5;style.WindowPadding={18,16};
    style.ItemSpacing={10,8};style.FramePadding={10,6};
    style.Colors[ImGuiCol_WindowBg]={.06f,.07f,.09f,.98f};
    style.Colors[ImGuiCol_Header]={.29f,.56f,.78f,.4f};
    return style;
}
constexpr std::array<unsigned, 2> resolutions[]{{960, 544},   {1280, 800},  {1280, 720},  {1600, 900},
                                                {1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440},
                                                {3840, 2160}, {5120, 1440}};
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
struct SettingsMenu {
    unsigned width{}, height{};
    float scale{1};
    HostSettings draft;
    bool opened{}, focus{}, hdr_active{}, hdr_supported{true};
    std::string status, output_status, backend = "Direct3D 11";
    std::array<float, 120> times{};
    unsigned sample{}, count{};
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
                        note("Game speed and audio stay at the native rate (~60 Hz). Higher caps do not "
                             "unlock gameplay FPS.");
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
                    float sum{}, lo = count ? times[0] : 0, hi{};
                    for (unsigned i = 0; i < count; ++i) {
                        sum += times[i];
                        lo = std::min(lo, times[i]);
                        hi = std::max(hi, times[i]);
                    }
                    const auto average = count ? sum / count : 0;
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
                         "measurements.");
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
                    ImGui::TextUnformatted("SDL controllers / Steam Deck");
                    ImGui::Separator();
                    ImGui::TextUnformatted(
                        "A / B / X / Y  ->  Cross / Circle / Square / Triangle\nLB / RB  ->  L / R\nMenu / "
                        "View  ->  Start / Select\nLeft stick and d-pad are supported.");
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Keyboard");
                    ImGui::Separator();
                    ImGui::TextUnformatted(
                        "X / Z / A / S  ->  Cross / Circle / Square / Triangle\nQ / E  ->  L / R\nEnter / "
                        "Backspace  ->  Start / Select\nI J K L  ->  Analog stick\nArrow keys  ->  D-pad\nF1 "
                        "(SDL: L3 + R3)  ->  Settings     F2  ->  Performance");
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
