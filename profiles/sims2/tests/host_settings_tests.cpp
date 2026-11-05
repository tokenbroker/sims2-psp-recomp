#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "display/host_settings.hpp"
#include "display/native_window.hpp"
#include "display/settings_overlay.hpp"
#include "gpu/native_renderer.hpp"
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>
int main() {
    using namespace sims2::display;
    const auto require=[](bool ok,const char *why){if(!ok)throw std::runtime_error(why);};
    const auto path=std::filesystem::temp_directory_path()/("sims2-settings-test-"+std::to_string(GetCurrentProcessId())+".ini");
    std::cout.setf(std::ios::unitbuf);
    try {
        HostSettings settings; settings.width=1920;settings.height=1080;settings.volume=37;settings.render_scale=4;
        settings.aspect=AspectMode::Ultrawide;settings.match_camera_aspect=true;settings.filter=OutputFilter::Linear;
        settings.fps_cap=144;settings.anisotropy=16;settings.render_percent=125;settings.hdr=HdrMode::Auto;settings.paper_white=240;settings.peak_nits=900;settings.stick_deadzone=25;settings.right_stick_camera=true;
        settings.muted=true;settings.vsync=false;settings.show_performance=true;
        std::string error;require(save_host_settings(path,settings,error),"Save settings failed");
        require(load_host_settings(path)==settings,"Settings did not round trip");
        {std::ofstream out(path);out<<"width=bad\nheight=999999\nvolume=-1\naspect=999\nrender_scale=999\n";}
        auto malformed=load_host_settings(path);
        require(malformed.width==1920 && malformed.height==4320 && malformed.volume==100 && malformed.render_scale==8,"Malformed preferences unsafe");
        auto rect=picture_rect(1920,1080,AspectMode::Widescreen);
        require(rect.width==1920 && rect.height==1080,"16:9 sizing incorrect");
        rect=picture_rect(3440,1440,AspectMode::Ultrawide);
        require(rect.width==3360 && rect.height==1440 && rect.x==40,"21:9 letterbox incorrect");
        rect=picture_rect(3440,1440,AspectMode::Widescreen);
        require(rect.width==2560 && rect.x==440 && rect.height==1440,"Ultrawide letterbox incorrect");
        rect=picture_rect(1920,1080,AspectMode::Native);
        require(rect.width<=1920 && rect.height==1080 && rect.x>0,"Native aspect incorrect");
        SetEnvironmentVariableW(L"SIMS2_SETTINGS_PATH",path.c_str());
        std::filesystem::remove(path);
        sims2::gpu::NativeRenderer renderer(true);
        NativeWindow window(true,&renderer,true);
        settings.muted=false;settings.render_percent=0;window.apply_settings(settings);
        RECT client{};GetClientRect(static_cast<HWND>(window.native_handle()),&client);
        std::cout<<"Client "<<client.right<<" x "<<client.bottom<<" DPI "<<GetDpiForWindow(static_cast<HWND>(window.native_handle()))<<"\n";
        require(client.right==1920 && client.bottom==1080,"1080p client sizing incorrect");
        require(!IsWindowVisible(static_cast<HWND>(window.native_handle())),"Test window became visible");
        float gain=-1;window.set_audio_control([&](float value){gain=value;});
        require(gain>.36f && gain<.38f,"Volume callback incorrect");
        settings.mode=WindowMode::Borderless;window.apply_settings(settings);
        require((GetWindowLongPtrW(static_cast<HWND>(window.native_handle()),GWL_STYLE)&WS_POPUP)!=0,"Borderless failed");
        settings.mode=WindowMode::Windowed;window.apply_settings(settings);
        PostMessageW(static_cast<HWND>(window.native_handle()),WM_KEYDOWN,VK_F1,0);
        std::thread closer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));PostMessageW(static_cast<HWND>(window.native_handle()),WM_KEYDOWN,VK_F1,0);});
        psprecomp::GuestMemory memory;sims2::ctrl::Controller controller;
        const auto began=std::chrono::steady_clock::now();
        const bool running=window.present(memory,{0x04000000u,512,3},16667,controller);
        closer.join();
        require(running && std::chrono::steady_clock::now()-began>=std::chrono::milliseconds(80),"F1 did not pause and resume");
        auto test_window=CreateWindowExW(0,L"STATIC",L"Overlay test",WS_OVERLAPPEDWINDOW,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(test_window!=nullptr,"Hidden overlay window failed");
        {
        std::cout<<"Creating second overlay\n";
        SettingsOverlay overlay(test_window,renderer.device(),renderer.context());
        std::vector<std::uint32_t> pixels(480*272,0x004488CC);
        std::cout<<"Presenting\n";
        overlay.present(pixels,settings,true);
        std::cout<<"Opening menu\n";
        overlay.toggle(settings);require(overlay.open(),"F1 open failed");
        for(int i=0;i<2;++i)overlay.present(pixels,settings,false);
        if(const auto *capture=std::getenv("SIMS2_TEST_MENU_CAPTURE"))overlay.capture_next_frame(capture);
        for(int i=0;i<3;++i)overlay.present(pixels,settings,false);
        // Select each real ImGui tab and capture its settled layout, without
        // showing or automating a desktop window.
        auto &bars=ImGui::GetCurrentContext()->TabBars;
        for(const char *name:{"Quality","Performance","Audio","Controls","Display"}){
            for(int i=0;i<bars.GetMapSize();++i)if(auto *bar=bars.TryGetMapData(i))for(auto &tab:bar->Tabs)
                if(std::string(ImGui::TabBarGetTabName(bar,&tab))==name)bar->NextSelectedTabId=tab.ID;
            for(int i=0;i<3;++i)overlay.present(pixels,settings,false);
            if(const auto *base=std::getenv("SIMS2_TEST_MENU_CAPTURE"))overlay.capture_next_frame(std::string(base)+"-"+name+".bmp");
            overlay.present(pixels,settings,false);
        }
        overlay.toggle(settings);require(!overlay.open(),"F1 close failed");
        }
        DestroyWindow(test_window);
        settings.fps_cap=30;settings.mode=WindowMode::Windowed;window.apply_settings(settings);
        const auto before=window.presented_frames();
        for(unsigned frame=0;frame<60;++frame)window.present(memory,{0x04000000,512,3},1000000+frame*16683,controller);
        const auto capped=window.presented_frames()-before;
        require(capped>=29 && capped<=31,"30 FPS cap was not applied to the real presenter");
        settings.width=settings.height=0;settings.mode=WindowMode::Fullscreen;window.apply_settings(settings);
        window.present(memory,{0x04000000,512,3},11000000,controller);
        require(!IsWindowVisible(static_cast<HWND>(window.native_handle())),"Hidden fullscreen test showed a window");
        std::filesystem::remove(path);
        std::cout<<"Settings persistence, aspect, hidden 1080p/fullscreen and ImGui presentation passed\n";
        return 0;
    }catch(const std::exception &e){std::filesystem::remove(path);std::cerr<<e.what()<<'\n';return 1;}
}
