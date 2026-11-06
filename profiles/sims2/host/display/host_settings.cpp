#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "display/host_settings.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>

namespace sims2::display {
namespace {
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
bool number(std::string_view text, unsigned &value) {
    unsigned parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    value = parsed;
    return true;
}
void boolean(std::string_view text, bool &value) {
    if (text == "1" || text == "true") value = true;
    if (text == "0" || text == "false") value = false;
}
}
std::string_view aspect_name(AspectMode mode) {
    constexpr std::string_view names[]{"native","16:9","stretch","21:9","4:3","16:10","32:9"};
    const auto index=unsigned(mode);return index<std::size(names)?names[index]:names[0];
}
float effective_render_scale(const HostSettings &settings,unsigned output_height) {
    return settings.render_percent?std::clamp(float(output_height)*settings.render_percent/(100*272),0.5f,16.0f):float(settings.render_scale);
}
HostSettings validated_settings(HostSettings settings) {
    settings.input=ctrl::validated_input(settings.input);
    if(!settings.width || !settings.height)settings.width=settings.height=0;
    else {settings.width=std::clamp(settings.width,480u,7680u);settings.height=std::clamp(settings.height,272u,4320u);}
    settings.fps_cap=std::min(settings.fps_cap,1000u);
    if(settings.engine_fps!=120u && settings.engine_fps!=240u)settings.engine_fps=0u;
    if(settings.render_percent)settings.render_percent=std::clamp(settings.render_percent,50u,200u);
    if(settings.anisotropy!=0 && settings.anisotropy!=2 && settings.anisotropy!=4 && settings.anisotropy!=8 && settings.anisotropy!=16)settings.anisotropy=0;
    if(unsigned(settings.hdr)>unsigned(HdrMode::On))settings.hdr=HdrMode::Off;
    settings.paper_white=std::clamp(settings.paper_white,80u,480u);
    if(settings.peak_nits)settings.peak_nits=std::clamp(settings.peak_nits,80u,10000u);
    settings.render_scale = std::clamp(settings.render_scale,1u,8u);
    settings.stick_deadzone=std::clamp(settings.stick_deadzone,5u,40u);
    settings.volume = std::min(settings.volume, 100u);
    if (settings.mode != WindowMode::Windowed && settings.mode != WindowMode::Borderless && settings.mode != WindowMode::Fullscreen) settings.mode = WindowMode::Windowed;
    if(unsigned(settings.aspect)>unsigned(AspectMode::SuperUltrawide))settings.aspect=AspectMode::Native;
    if (settings.filter != OutputFilter::Nearest && settings.filter != OutputFilter::Linear) settings.filter = OutputFilter::Nearest;
    return settings;
}
PictureRect picture_rect(unsigned width, unsigned height, AspectMode mode) {
    if (!width || !height) return {};
    if (mode == AspectMode::Stretch) return {0, 0, width, height};
    const double ratio = mode == AspectMode::Widescreen ? 16.0 / 9.0 : mode == AspectMode::Ultrawide ? 21.0 / 9.0 : mode==AspectMode::FourThree?4.0/3.0:mode==AspectMode::SixteenTen?16.0/10.0:mode==AspectMode::SuperUltrawide?32.0/9.0:480.0/272.0;
    unsigned w = width, h = height;
    if (double(width) / height > ratio) w = std::clamp(unsigned(std::lround(height * ratio)), 1u, width);
    else h = std::clamp(unsigned(std::lround(width / ratio)), 1u, height);
    return {(width - w) / 2, (height - h) / 2, w, h};
}
std::filesystem::path host_settings_path() {
    if (const auto *override_path = std::getenv("SIMS2_SETTINGS_PATH")) return override_path;
#if defined(_WIN32)
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (count) {
        std::wstring folder(count, L'\0');
        const auto read = GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), count);
        if (read && read < count) {
            folder.resize(read);
            return std::filesystem::path(folder) / "Sims2PSPRecomp/settings.ini";
        }
    }
#else
    if (const auto *config = std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute()) return std::filesystem::path(config) / "Sims2PSPRecomp/settings.ini";
    if (const auto *home = std::getenv("HOME")) return std::filesystem::path(home) / ".config/Sims2PSPRecomp/settings.ini";
#endif
    return std::filesystem::temp_directory_path() / "Sims2PSPRecomp/settings.ini";
}
HostSettings load_host_settings(const std::filesystem::path &path) {
    HostSettings result;
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);) {
        const auto text = trim(line);
        if (text.empty() || text.front() == '#' || text.front() == ';' || text.front() == '[') continue;
        const auto equals = text.find('=');
        if (equals == text.npos) continue;
        const auto key = trim(text.substr(0, equals)), value = trim(text.substr(equals + 1));
        if(key.starts_with("key_")||key.starts_with("pad_")){
            unsigned index{};if(number(key.substr(4),index)){
                if(key.starts_with("key_")&&index<result.input.keys.size())number(value,result.input.keys[index]);
                if(key.starts_with("pad_")&&index<result.input.gamepad.size())number(value,result.input.gamepad[index]);
            }
        }
        else if(key=="controller_labels")number(value,result.input.controller_labels);
        else if(key=="camera_threshold")number(value,result.input.camera_threshold);
        else if(key=="mouse_sensitivity")number(value,result.input.mouse_sensitivity);
        else if(key=="mouse_pulse_ms")number(value,result.input.mouse_pulse_ms);
        else if(key=="menu_repeat_ms")number(value,result.input.menu_repeat_ms);
        else if(key=="wheel_up")number(value,result.input.wheel_up);
        else if(key=="wheel_down")number(value,result.input.wheel_down);
        else if(key=="mouse_camera")boolean(value,result.input.mouse_camera);
        else if(key=="invert_camera")boolean(value,result.input.invert_camera);
        else if(key=="input_hints")boolean(value,result.input.input_hints);
        else if (key == "width") number(value, result.width);
        else if (key == "height") number(value, result.height);
        else if(key=="fps_cap")number(value,result.fps_cap);
        else if(key=="engine_fps")number(value,result.engine_fps);
        else if(key=="render_percent")number(value,result.render_percent);
        else if(key=="anisotropy")number(value,result.anisotropy);
        else if(key=="paper_white")number(value,result.paper_white);
        else if(key=="peak_nits")number(value,result.peak_nits);
        else if(key=="hdr") {if(value=="off")result.hdr=HdrMode::Off;if(value=="auto")result.hdr=HdrMode::Auto;if(value=="on")result.hdr=HdrMode::On;}
        else if (key == "render_scale") number(value, result.render_scale);
        else if (key == "window_mode") {
            if (value == "windowed") result.mode = WindowMode::Windowed;
            if(value=="fullscreen")result.mode=WindowMode::Fullscreen;
            if (value == "borderless") result.mode = WindowMode::Borderless;
        } else if (key == "aspect") {
            for(unsigned i=0;i<=unsigned(AspectMode::SuperUltrawide);++i)
                if(value==aspect_name(AspectMode(i)))result.aspect=AspectMode(i);
        } else if (key == "filter") {
            if (value == "nearest") result.filter = OutputFilter::Nearest;
            if (value == "linear") result.filter = OutputFilter::Linear;
        } else if (key == "vsync") boolean(value, result.vsync);
        else if(key=="stick_deadzone")number(value,result.stick_deadzone);
        else if(key=="right_stick_camera")boolean(value,result.right_stick_camera);
        else if (key == "volume") number(value, result.volume);
        else if (key == "match_camera_aspect") boolean(value, result.match_camera_aspect);
        else if (key == "muted") boolean(value, result.muted);
        else if (key == "show_performance") boolean(value, result.show_performance);
    }
    return validated_settings(result);
}
bool save_host_settings(const std::filesystem::path &path, HostSettings settings, std::string &error) {
    settings = validated_settings(settings);
    auto temporary = path;
#if defined(_WIN32)
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
#else
    temporary += ".tmp";
#endif
    try {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "[host]\nwindow_mode=" << (settings.mode==WindowMode::Windowed?"windowed":settings.mode==WindowMode::Fullscreen?"fullscreen":"borderless")
               << "\nrender_scale=" << settings.render_scale
               << "\nwidth=" << settings.width << "\nheight=" << settings.height
               << "\naspect=" << aspect_name(settings.aspect)
               << "\nrender_percent=" << settings.render_percent << "\nfps_cap=" << settings.fps_cap
               << "\nengine_fps=" << settings.engine_fps
               << "\nanisotropy=" << settings.anisotropy << "\nhdr=" << (settings.hdr==HdrMode::Auto?"auto":settings.hdr==HdrMode::On?"on":"off")
               << "\npaper_white=" << settings.paper_white << "\npeak_nits=" << settings.peak_nits
               << "\nfilter=" << (settings.filter == OutputFilter::Nearest ? "nearest" : "linear")
               << "\nmatch_camera_aspect=" << settings.match_camera_aspect
               << "\nstick_deadzone=" << settings.stick_deadzone << "\nright_stick_camera=" << settings.right_stick_camera
               << "\nvsync=" << settings.vsync << "\nvolume=" << settings.volume
               << "\nmuted=" << settings.muted << "\nshow_performance=" << settings.show_performance << '\n';
        for(unsigned i=0;i<settings.input.keys.size();++i)output<<"key_"<<i<<'='<<settings.input.keys[i]<<'\n';
        for(unsigned i=0;i<settings.input.gamepad.size();++i)output<<"pad_"<<i<<'='<<settings.input.gamepad[i]<<'\n';
        output<<"controller_labels="<<settings.input.controller_labels<<"\ncamera_threshold="<<settings.input.camera_threshold<<"\nmouse_sensitivity="<<settings.input.mouse_sensitivity
            <<"\nmouse_pulse_ms="<<settings.input.mouse_pulse_ms<<"\nmenu_repeat_ms="<<settings.input.menu_repeat_ms
            <<"\nwheel_up="<<settings.input.wheel_up<<"\nwheel_down="<<settings.input.wheel_down
            <<"\nmouse_camera="<<settings.input.mouse_camera<<"\ninvert_camera="<<settings.input.invert_camera
            <<"\ninput_hints="<<settings.input.input_hints<<'\n';
        output.close();
        if (!output) throw std::runtime_error("Cannot write settings file");
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category(), "Cannot replace settings file");
#else
        std::filesystem::rename(temporary, path);
#endif
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
}
}
