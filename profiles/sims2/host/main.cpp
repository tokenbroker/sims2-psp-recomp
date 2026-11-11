#include "psprecomp/common.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"
#include "psprecomp/sha256.hpp"
#include "startup_hle.hpp"
#include "kernel/early_kernel.hpp"
#include "kernel/fixed_pool.hpp"
#include "kernel/sync.hpp"
#include "kernel/volatile_memory.hpp"
#include "modules/utility_modules.hpp"
#include "modules/module_manager.hpp"
#include "utility/savedata_dialog.hpp"
#include "umd/umd_drive.hpp"
#include "io/io_manager.hpp"
#include "rtc/rtc.hpp"
#include "ge/ge.hpp"
#include "display/display.hpp"
#include "game_timing.hpp"
#if defined(_WIN32) || defined(SIMS2_VULKAN)
#include "display/native_window.hpp"
#include "audio/native_audio.hpp"
#include "gpu/native_renderer.hpp"
#endif
#include "audio/audio.hpp"
#include "audio/atrac.hpp"
#include "sas/sas.hpp"
#include "ctrl/controller.hpp"
#include "font/native_font.hpp"
#include "movie/mpeg.hpp"
#include "kernel/interrupts.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <limits>
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>
#include <map>

namespace psprecomp { void register_generated_functions(Runtime &); }

int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) {
        std::cerr << "Usage: sims2_probe <BOOT.BIN> <USA.iso> [--max-dispatches N|unlimited]\n"
                     "Development build; native graphics/audio and incomplete gameplay validation.\n";
        return 2;
    }
    try {
        std::uint64_t max_dispatches = std::getenv("SIMS2_WINDOW") ? std::numeric_limits<std::uint64_t>::max() : 10000000u;
        if (argc == 5) {
            const std::string_view option = argv[3], value = argv[4];
            const auto parsed = value == "unlimited" ? std::from_chars_result{value.data() + value.size(), {}} :
                std::from_chars(value.data(), value.data() + value.size(), max_dispatches);
            if (value == "unlimited") max_dispatches = std::numeric_limits<std::uint64_t>::max();
            if (option != "--max-dispatches" || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                max_dispatches == 0 || (value != "unlimited" && max_dispatches > 1000000000u)) {
                std::cerr << "Invalid dispatch budget\n"; return 2;
            }
        }
        // /Od generated units have large native frames. The diagnostic uses
        // outer dispatch to avoid overflowing Windows' default native stack.
#if defined(_WIN32)
        _putenv_s("PSPRECOMP_NO_CHAIN", "1");
#else
        setenv("PSPRECOMP_NO_CHAIN", "1", 1);
#endif
        sims2::umd::DiscImage disc(argv[2]);
        const auto disc_id = disc.find("UMD_DATA.BIN");
        const auto disc_boot = disc.find("PSP_GAME/SYSDIR/BOOT.BIN");
        if (!disc_id || !disc_boot) throw psprecomp::Error("Missing PSP disc identity or BOOT.BIN");
        const auto id_bytes = disc.read_file(*disc_id, 256u);
        const std::string id(id_bytes.begin(), id_bytes.end());
        #if defined(SIMS2_EUROPE)
        constexpr auto expected_disc = "ULES-00211|";
        constexpr auto expected_boot = "0823aadf863790c82383e579f37147e3eb1c758ccf49492910091df29b4a9d25";
#else
        constexpr auto expected_disc = "ULUS-10031|";
        constexpr auto expected_boot = "03ace15b9457016d48ebe2aa18fc757912641024f1e3a1f0abb92f8280a386b1";
#endif
        if (!id.starts_with(expected_disc)) throw psprecomp::Error("This runtime does not match the selected disc region");
        auto boot_bytes = disc.read_file(*disc_boot, 4u * 1024u * 1024u);
        if (psprecomp::sha256_bytes(boot_bytes) != expected_boot)
            throw psprecomp::Error("Unsupported executable revision for this disc region");
        // Updates may retain the launcher's prepared flag while changing the
        // install layout. The verified ISO is authoritative, so a missing old
        // extraction must not prevent a prebuilt runtime from starting.
        if (std::filesystem::exists(argv[1])) {
            const auto cached_boot = psprecomp::sha256_file(argv[1]);
            if (cached_boot != expected_boot) {
                if (cached_boot != "03ace15b9457016d48ebe2aa18fc757912641024f1e3a1f0abb92f8280a386b1" &&
                    cached_boot != "0823aadf863790c82383e579f37147e3eb1c758ccf49492910091df29b4a9d25")
                    throw psprecomp::Error("Prepared BOOT.BIN differs from the supported disc executable");
                std::cerr << "[startup] Prepared copy belongs to the other supported region; using verified ISO\n";
            }
        } else {
            std::cerr << "[startup] Prepared BOOT.BIN is absent; loading verified executable directly from ISO\n";
        }
        const auto elf = psprecomp::Elf32Image::from_bytes(std::move(boot_bytes), "verified disc BOOT.BIN");
        auto runtime = std::make_unique<psprecomp::Runtime>();
        const auto relocations = elf.load_and_relocate(runtime->memory());
        if (relocations.invalid || relocations.unsupported)
            throw psprecomp::Error("Relocation failure");
        psprecomp::register_generated_functions(*runtime);
#if defined(_WIN32) || defined(SIMS2_VULKAN)
        const auto startup_settings = sims2::display::load_host_settings(sims2::display::host_settings_path());
        if (!sims2::kernel::configure_guest_display(startup_settings.engine_fps))
            throw psprecomp::Error("Unsupported engine frame rate");
        sims2::install_game_timing(*runtime);
        if (startup_settings.engine_fps)
            std::cerr << "[timing] Experimental engine rate " << startup_settings.engine_fps << " Hz\n";
#endif
        unsigned system_language = 1;
#if defined(_WIN32) || defined(SIMS2_VULKAN)
        system_language = startup_settings.system_language;
#endif
        sims2::StartupHle startup;
        startup.install(*runtime);
        sims2::kernel::EarlyKernel kernel;
        std::uint32_t image_end = psprecomp::kDefaultPspUserLoadBase;
        for (std::size_t i = 0; i < elf.segments().size(); ++i) {
            const auto &segment = elf.segments()[i];
            if (segment.type != 1u) continue;
            const auto end = static_cast<std::uint64_t>(elf.segment_runtime_address(i)) + segment.memory_size;
            if (end > 0x09FF6000u) throw psprecomp::Error("Image overlaps startup stack");
            image_end = std::max(image_end, static_cast<std::uint32_t>(end));
        }
        kernel.install(*runtime, image_end, 0x09FF6000u);
        sims2::kernel::register_early_threadman(kernel, *runtime);
        sims2::kernel::InterruptManager interrupts(kernel);
        sims2::kernel::register_interrupts(interrupts, *runtime);
        sims2::kernel::FixedPools pools(kernel);
        pools.install(*runtime);
        sims2::kernel::SyncObjects sync(kernel);
        sims2::kernel::register_sync(sync, *runtime);
        sims2::kernel::VolatileMemory volatile_memory(kernel);
        volatile_memory.install(*runtime);
        sims2::modules::UtilityModules utility_modules;
        sims2::modules::register_utility_modules(utility_modules, *runtime);
        sims2::modules::register_system_parameters(*runtime, sims2::rtc::Rtc::host_utc_offset_seconds() / 60, system_language);
        sims2::utility::SavedataUtility savedata(sims2::utility::default_memory_stick(),
            [&startup] { return startup.compiled_sdk_version(); });
        sims2::utility::register_savedata(savedata, *runtime);
        sims2::umd::UmdDrive drive(disc);
        sims2::umd::register_umd(drive, *runtime, &kernel);
        sims2::io::IoManager io(disc, *runtime, &kernel);
        sims2::io::register_io(io, *runtime);
        sims2::modules::ModuleManager modules(kernel, io);
        sims2::modules::register_module_manager(modules, *runtime);
        sims2::rtc::Rtc rtc(kernel, sims2::rtc::Rtc::host_boot_utc_unix_us(),
                           sims2::rtc::Rtc::host_utc_offset_seconds());
        sims2::rtc::register_rtc(rtc, *runtime);
        sims2::ge::Ge ge(kernel);
        sims2::ge::register_ge(ge, kernel, *runtime);
#if defined(_WIN32) || defined(SIMS2_VULKAN)
        sims2::gpu::NativeRenderer renderer(std::getenv("SIMS2_WARP") != nullptr);
        std::ofstream ui_trace;
        if (const auto *path = std::getenv("SIMS2_TRACE_UI")) ui_trace.open(path);
        const auto *trace_time = std::getenv("SIMS2_TRACE_UI_US");
        const std::string trace_time_file = trace_time && trace_time[0] == '@' ? trace_time + 1 : "";
        std::uint64_t ui_trace_us = trace_time_file.empty() ? (trace_time ? std::strtoull(trace_time, nullptr, 10) : 23000000ull)
                                                         : std::numeric_limits<std::uint64_t>::max();
        auto trace_poll_us = std::numeric_limits<std::uint64_t>::max();
        ge.state().set_draw_sink([&](const auto &call) {
            if (ui_trace.is_open() && !trace_time_file.empty() && trace_poll_us != kernel.now_us()) {
                trace_poll_us = kernel.now_us();
                std::ifstream request(trace_time_file);
                if (!(request >> ui_trace_us)) ui_trace_us = std::numeric_limits<std::uint64_t>::max();
            }
            if (ui_trace.is_open() && !call.vertices.empty() && kernel.now_us() >= ui_trace_us && kernel.now_us() - ui_trace_us < 20000) {
                const auto &a=call.vertices.front(), &b=call.vertices.back();
                ui_trace << kernel.now_us() << " through " << call.through << " lit " << call.lighting_enabled << " prim " << unsigned(call.primitive) << " n " << call.vertices.size()
                    << " xy " << a.position[0] << ',' << a.position[1] << ',' << b.position[0] << ',' << b.position[1]
                    << " z " << a.position[2] << ',' << b.position[2] << " color " << a.color << ',' << b.color
                    << " vc " << call.has_vertex_color << " mat " << call.material_color
                    << " light " << call.vertex_type << ',' << call.lighting.material_update << ',' << call.lighting.material_emissive << ',' << call.lighting.ambient_color << ',' << call.lighting.ambient_alpha << ',' << call.lighting.material_diffuse
                    << " tex " << call.texture.enabled << ',' << call.texture.address << ',' << call.texture.width << ',' << call.texture.height << ',' << unsigned(call.texture.format) << ',' << call.texture.function
                    << " uv " << a.texcoord[0] << ',' << a.texcoord[1] << ',' << b.texcoord[0] << ',' << b.texcoord[1]
                    << " map " << ge.state(0xC0) << ',' << call.texture_matrix[12] << ',' << call.texture_matrix[13] << ',' << call.texture.scale_u << ',' << call.texture.scale_v << ',' << call.texture.offset_u << ',' << call.texture.offset_v
                    << " blend " << call.blend.enabled << ',' << call.blend.source_factor << ',' << call.blend.destination_factor
                    << " depth " << call.depth.test_enabled << ',' << call.depth.function
                    << " regs " << ge.state(0x27) << ',' << ge.state(0x28) << ',' << ge.state(0x50) << ',' << ge.state(0xE6)
                    << " vertex_address " << call.vertex_address;
                const auto matrix = [&](const char *name, const auto &values) {
                    ui_trace << ' ' << name << ' ';
                    for (unsigned i = 0; i < values.size(); ++i) { if (i) ui_trace << ','; ui_trace << values[i]; }
                };
                matrix("world", call.world); matrix("view", call.view); matrix("projection", call.projection);
                ui_trace << " viewport " << call.viewport.x_scale << ',' << call.viewport.y_scale << ',' << call.viewport.z_scale
                    << ',' << call.viewport.x_offset << ',' << call.viewport.y_offset << ',' << call.viewport.z_offset
                    << ',' << call.viewport.offset_x << ',' << call.viewport.offset_y << '\n';
                ui_trace.flush();
            }
            renderer.draw(runtime->memory(), call);
        });
        ge.state().set_transfer_sink([&](const auto &transfer) {
            renderer.prepare_transfer(runtime->memory(), transfer);
            if (!sims2::ge::Ge::copy_transfer(runtime->memory(), transfer)) runtime->stop("GE transfer outside guest memory");
        });
#endif
        sims2::display::Display display;
        std::map<std::pair<std::string,std::uint32_t>,std::uint64_t> timing_calls;
        const auto *timing_path=std::getenv("SIMS2_TIMING_TRACE");
        sims2::display::VblankWaitHook research_wait;
        if(const auto *value=std::getenv("SIMS2_EXPERIMENTAL_RENDER_HZ")){
            if(sims2::kernel::guest_display_hz)
                throw psprecomp::Error("Choose the engine rate or the single-wait research experiment, not both");
#if defined(SIMS2_TIMING_RESEARCH)
            unsigned hz{};const std::string_view text=value;auto parsed=std::from_chars(text.data(),text.data()+text.size(),hz);
            if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||hz<60||hz>240||!timing_path||!std::getenv("SIMS2_MEMSTICK")||!std::getenv("SIMS2_STOP_AFTER_FRAMES"))
                throw psprecomp::Error("Timing research requires 60..240 Hz, explicit isolated memstick, timing trace and bounded frames");
            research_wait=[hz](psprecomp::Runtime &,const auto &cpu,std::uint64_t now)->std::optional<std::uint64_t>{
                // Only the measured framebuffer-loop caller. Kernel vcount,
                // audio, callbacks and the secondary startup wait stay native.
                if(cpu.gpr[31]==0x08939000u)return now+1000000u/hz;return std::nullopt;
            };
            std::cerr<<"[research] Unverified render wait experiment: "<<hz<<" Hz; never use production saves\n";
#else
            (void)value;throw psprecomp::Error("This production build does not enable timing research");
#endif
        }
        sims2::display::register_display(display,kernel,*runtime,{},std::move(research_wait),timing_path?sims2::display::TimingObserver([&](const char *kind,const auto &cpu,std::uint64_t){
            ++timing_calls[{kind,cpu.gpr[31]}];
        }):sims2::display::TimingObserver{});
        sims2::audio::Audio audio(kernel);
        sims2::audio::register_audio(audio, *runtime);
        sims2::audio::AtracLibrary atrac(sims2::audio::make_ffmpeg_decoder);
        sims2::audio::register_atrac(atrac, *runtime);
        sims2::sas::SasCore sas;
        sims2::sas::register_sas(sas, *runtime);
        sims2::ctrl::Controller controller;
        sims2::ctrl::register_ctrl(controller, kernel, *runtime);
        if (const auto *script = std::getenv("SIMS2_INPUT_SCRIPT")) {
            auto input = sims2::ctrl::parse_input_script(script);
            if (!input) throw psprecomp::Error("Invalid SIMS2_INPUT_SCRIPT");
            controller.set_script(std::move(*input));
        }
#if defined(_WIN32) || defined(SIMS2_VULKAN)
        std::unique_ptr<sims2::display::NativeWindow> window;
        std::unique_ptr<sims2::audio::NativeAudio> playback;
        if (std::getenv("SIMS2_WINDOW") || std::getenv("SIMS2_PACE") || std::getenv("SIMS2_CAPTURE_BMP") || std::getenv("SIMS2_CAPTURE_LAST_BMP")) {
            window = std::make_unique<sims2::display::NativeWindow>(std::getenv("SIMS2_WINDOW") != nullptr, &renderer, std::getenv("SIMS2_WINDOW_HIDDEN") != nullptr);
            if (std::getenv("SIMS2_WINDOW")) savedata.set_selector([&](auto mode, const auto &slots) {
                return window->select_savedata(mode, slots);
            });
        }
        if ((std::getenv("SIMS2_WINDOW") || std::getenv("SIMS2_AUDIO")) && !std::getenv("SIMS2_MUTE")) {
            playback = std::make_unique<sims2::audio::NativeAudio>();
            if(window)window->set_audio_control([&](float gain){playback->set_gain(gain);});
            audio.set_sink([&](auto channel, auto samples, auto mono, auto left, auto right) {
                if (window) window->synchronize(kernel.now_us());
                playback->play(channel, samples, mono, left, right);
            });
        }
        std::ofstream frame_csv;
        if (const auto *path = std::getenv("SIMS2_FRAME_CSV")) {
            frame_csv.open(path);
            if (!frame_csv) throw psprecomp::Error("Cannot open SIMS2_FRAME_CSV");
            frame_csv << "frame,guest_us,guest_delta_us,work_us,renderer_draw_us,renderer_flush_us,present_us,draws\n";
        }
        auto previous_frame_end = std::chrono::steady_clock::now();
        std::uint64_t previous_guest_us = kernel.now_us(), previous_draws = renderer.draws();
        auto previous_profile = renderer.profile_stats();
        std::string step_command;
        std::string last_live_command = "-";
        std::uint64_t step_end_frame{};
        display.set_present_hook([&](const auto &frame) {
            const auto arrived = frame_csv.is_open() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            renderer.flush(runtime->memory());
            if (window && !window->present(runtime->memory(), frame, kernel.now_us(), controller)) runtime->stop("Window closed");
            if (frame_csv.is_open()) {
                const auto presented = std::chrono::steady_clock::now();
                const auto profile = renderer.profile_stats();
                frame_csv << display.framebuffer_requests() << ',' << kernel.now_us() << ',' << kernel.now_us() - previous_guest_us
                    << ',' << std::chrono::duration_cast<std::chrono::microseconds>(arrived - previous_frame_end).count()
                    << ',' << (profile.draw_ns - previous_profile.draw_ns) / 1000
                    << ',' << (profile.flush_ns - previous_profile.flush_ns) / 1000
                    << ',' << std::chrono::duration_cast<std::chrono::microseconds>(presented - arrived).count()
                    << ',' << renderer.draws() - previous_draws << '\n';
                previous_profile = profile; previous_draws = renderer.draws(); previous_guest_us = kernel.now_us();
            }
            // Optional local diagnostics: gamepad input and state observation
            // for long scenario checks without restarting the game each time.
            bool paused = false;
            if (const auto *path = std::getenv("SIMS2_LIVE_INPUT")) {
                if (std::getenv("SIMS2_INPUT_SCRIPT")) throw psprecomp::Error("Choose live input or a timed script");
                const auto read_buttons = [&] {
                    std::ifstream input(path); std::string buttons;
                    if (!input || !std::getline(input, buttons)) return last_live_command;
                    if (!buttons.empty() && buttons.back() == '\r') buttons.pop_back();
                    if (buttons.empty()) return last_live_command;
                    last_live_command = buttons;
                    return buttons;
                };
                auto buttons = read_buttons();
                const auto apply_step = [&] {
                    if (!buttons.starts_with("STEP ")) { step_command.clear(); return; }
                    if (std::getenv("SIMS2_WINDOW")) throw psprecomp::Error("Live-input STEP is for headless diagnostics only");
                    std::istringstream command(buttons);
                    std::string verb, pad, extra; std::uint32_t count{};
                    if (!(command >> verb >> count >> pad) || count == 0 || count > 3600 || (command >> extra))
                        throw psprecomp::Error("Expected STEP 1..3600 BUTTONS");
                    if (step_command != buttons) {
                        step_command = buttons;
                        step_end_frame = display.framebuffer_requests() + count;
                    }
                    if (display.framebuffer_requests() >= step_end_frame) {
                        std::ofstream control(path);
                        if (!(control << "PAUSE\n")) throw psprecomp::Error("Cannot pause live-input step");
                        buttons = "PAUSE";
                        step_command.clear();
                    } else buttons = pad;
                };
                apply_step();
                if (buttons == "PAUSE") {
                    last_live_command = "PAUSE";
                    if (std::getenv("SIMS2_WINDOW")) throw psprecomp::Error("Live-input PAUSE is for headless diagnostics only");
                    paused = true;
                    // Capture the exact frame held here, rather than an older
                    // periodic capture. Guest time and input stop together.
                    if (window) if (const auto *capture = std::getenv("SIMS2_CAPTURE_BMP")) window->capture(capture);
                    if (const auto *state_path = std::getenv("SIMS2_STATE_JSON")) {
                        std::ofstream state(state_path);
                        state << "{\"frames\":" << display.framebuffer_requests() << ",\"guest_us\":" << kernel.now_us()
                            << ",\"gpu_draws\":" << renderer.draws() << ",\"presentations\":" << (window?window->presented_frames():0) << "}\n";
                    }
                    while (buttons == "PAUSE") {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        buttons = read_buttons();
                    }
                    apply_step();
                }
                auto parsed = sims2::ctrl::parse_input_script("0:" + (buttons.empty() ? std::string("-") : buttons));
                if (!parsed) throw psprecomp::Error("Invalid SIMS2_LIVE_INPUT buttons");
                controller.set_pad(parsed->front().pad, kernel.now_us());
            }
            if (const auto *path = std::getenv("SIMS2_STATE_JSON"); path && (paused || display.framebuffer_requests() % 30u == 0u)) {
                std::ofstream state(path);
                state << "{\"frames\":" << display.framebuffer_requests() << ",\"guest_us\":" << kernel.now_us()
                      << ",\"gpu_draws\":" << renderer.draws() << ",\"presentations\":" << (window?window->presented_frames():0) << "}\n";
            }
            if (const auto *limit = std::getenv("SIMS2_STOP_AFTER_FRAMES")) {
                if (display.framebuffer_requests() >= std::strtoull(limit, nullptr, 10)) runtime->stop("Framebuffer diagnostic limit reached");
            }
            // Inspection pauses and diagnostic file writes are not guest work.
            if (frame_csv.is_open()) previous_frame_end = std::chrono::steady_clock::now();
        });
#endif
        // An explicit slot choice permits reproducible headless load/save
        // checks without treating the first list entry as a user decision.
        if (const auto *slot = std::getenv("SIMS2_SAVE_SLOT")) {
            const std::string selected = std::string_view(slot) == "@default" ? "" : slot;
            savedata.set_selector([selected](std::uint32_t mode, const std::vector<std::string> &slots) -> std::optional<std::string> {
                if (mode != 2u && mode != 3u && mode != 4u && mode != 5u) return std::nullopt;
                if (std::find(slots.begin(), slots.end(), selected) == slots.end())
                    throw psprecomp::Error("SIMS2_SAVE_SLOT does not match any requested slot");
                return selected;
            });
        }
        sims2::font::NativeFont font(kernel);
        font.install(*runtime);
        sims2::movie::Mpeg mpeg(&kernel);
        mpeg.install(*runtime);
        constexpr std::uint32_t arguments = 0x08100000u;
        constexpr std::string_view launch_path = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
        for (std::size_t i = 0; i <= launch_path.size(); ++i)
            runtime->memory().store8(arguments + static_cast<std::uint32_t>(i),
                i == launch_path.size() ? 0u : static_cast<std::uint8_t>(launch_path[i]));
        runtime->cpu().gpr[4] = static_cast<std::uint32_t>(launch_path.size() + 1u);
        runtime->cpu().gpr[5] = arguments;
        runtime->cpu().gpr[29] = 0x09FFE000u;
        runtime->cpu().gpr[31] = sims2::kernel::kThreadExitStub;
        runtime->cpu().pc = elf.runtime_entry();
        if (const auto module = elf.find_module_info(runtime->memory()))
            runtime->cpu().gpr[28] = module->gp;
        kernel.begin_main_thread(runtime->cpu(), "module_start", 0x20u);
        std::string execution_error;
        try { runtime->run(elf.runtime_entry(), max_dispatches); }
        catch (const std::exception &error) { execution_error = error.what(); }
#if defined(_WIN32) || defined(SIMS2_VULKAN)
        renderer.flush(runtime->memory());
        if (window) if (const auto *path = std::getenv("SIMS2_CAPTURE_LAST_BMP")) window->capture(path);
        if(timing_path){std::ofstream trace(timing_path);trace<<"{\"native_vblank_hz\":59.94005994,\"active_display_hz\":"<<sims2::kernel::guest_display_frequency()<<",\"calls\":[";bool first=true;
            for(const auto &[key,count]:timing_calls){if(!first)trace<<',';first=false;trace<<"{\"kind\":\""<<key.first<<"\",\"return_pc\":\""<<psprecomp::hex32(key.second)<<"\",\"count\":"<<count<<'}';}trace<<"]}\n";}
        std::cout << "GPU draws rendered: " << renderer.draws() << '\n';
#endif
        std::cout << "PC: " << psprecomp::hex32(runtime->cpu().pc)
                  << "\nGuest time (us): " << kernel.now_us()
                  << "\nSDK: " << psprecomp::hex32(startup.compiled_sdk_version())
                  << " compiler: " << psprecomp::hex32(startup.compiler_version())
                  << "\nStopped: " << runtime->stop_reason() << '\n';
        std::cout << "GE commands: " << ge.stats().commands
                  << " lists completed: " << ge.stats().lists_completed
                  << " draws decoded: " << ge.state().draw_count()
                  << " framebuffer requests: " << display.framebuffer_requests() << '\n';
#if defined(_WIN32)
        std::cout << "Font backend: Windows Arial substitution (PSP typography differs)\n";
#else
        std::cout << "Font backend: FreeType/fontconfig substitution (PSP typography differs)\n";
#endif
        std::cout << "Movie packets: " << mpeg.packets_read() << " video frames decoded: " << mpeg.video_frames() << '\n';
#if defined(_WIN32) || defined(SIMS2_VULKAN)
        if (playback) std::cout << "SDL audio frames submitted: " << playback->frames_submitted() << '\n';
#endif
        std::cout << "Thread: " << psprecomp::runtime_thread_name()
                  << " return: " << psprecomp::hex32(runtime->cpu().gpr[31]) << "\nArgs:";
        for (unsigned i = 4; i < 12; ++i) std::cout << ' ' << psprecomp::hex32(runtime->cpu().gpr[i]);
        std::cout << '\n';
        if (runtime->memory().contains(runtime->cpu().gpr[4], 1u)) {
            try {
                const auto text = runtime->memory().read_c_string(runtime->cpu().gpr[4], 256u);
                if (!text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
                    std::cout << "Argument string: " << text << '\n';
            }
            catch (const psprecomp::Error &) {}
        }
        std::cout.flush(); // Preserve the stop report even if driver teardown stalls.
        if (!execution_error.empty()) { std::cerr << execution_error << '\n'; return 1; }
        if(runtime->stop_reason()=="Window closed")return 0;
        return 3; // Diagnostic/unsupported stops remain distinct from a normal user exit.
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
