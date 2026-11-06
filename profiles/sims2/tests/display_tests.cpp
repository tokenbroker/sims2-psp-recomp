// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#include "display/display.hpp"
#include "kernel/guest_clock.hpp"
#include "game_timing.hpp"
#include <cmath>

#include <cstdlib>
#include <iostream>
#include <map>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void call(psprecomp::Runtime &runtime, std::uint32_t nid, std::uint32_t return_address) {
    auto &cpu = runtime.cpu();
    cpu.pc = 0x08A00000u;
    cpu.set_gpr(31u, return_address);
    runtime.invoke_import("sceDisplay", nid, cpu);
}
}

int main() {
    using namespace sims2::display;
    psprecomp::Runtime runtime;
    sims2::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    cpu.pc = 0x08804444u;
    kernel.begin_main_thread(cpu, "main", 0x20u);
    Display display;
    std::map<std::string,unsigned> timing;
    register_display(display,kernel,runtime,{},{},[&](const char *kind,const auto &context,std::uint64_t){
        require(context.gpr[31]!=0,"Timing probe lost caller address");++timing[kind];
    });
    sims2::kernel::register_early_threadman(kernel, runtime);

    require(static_cast<std::uint32_t>(display.set_mode(1u, 480u, 272u)) == kInvalidMode &&
                static_cast<std::uint32_t>(display.set_mode(0u, 480u, 271u)) == kInvalidSize && display.set_mode(0u, 480u, 272u) == 0,
            "display mode validation incorrect");

    // Timing: vcount counts vblank starts; vblank lasts 731us.
    require(Display::vcount(0u) == 0u && !Display::in_vblank(0u) && Display::vcount(16683u) == 1u &&
                Display::in_vblank(16683u) && Display::in_vblank(16683u + 730u) && !Display::in_vblank(16683u + 731u),
            "vblank timing incorrect");
    require(Display::accumulated_hcount(sims2::kernel::vblank_time_us(60u)) == 60u * kLinesPerFrame,
            "hcount does not accumulate 286 lines per frame");

    call(runtime, 0x4D4E10ECu, 0x08804500u);
    require(cpu.gpr[2] == 0u, "IsVblank true at boot");
    call(runtime, 0x984C27E7u, 0x08804600u);
    require(kernel.now_us() == 16683u && cpu.pc == 0x08804600u && cpu.gpr[2] == 0u, "WaitVblankStart did not wait");
    call(runtime, 0x4D4E10ECu, 0x08804700u);
    require(cpu.gpr[2] == 1u, "IsVblank false at vblank start");
    call(runtime, 0x36CDFADEu, 0x08804800u);
    require(kernel.now_us() == 16683u && cpu.gpr[2] == 0u, "WaitVblank waited inside vblank");
    call(runtime, 0x984C27E7u, 0x08804900u);
    require(kernel.now_us() == 33366u, "WaitVblankStart inside vblank did not wait for the next one");
    call(runtime, 0x9C6EAAD7u, 0x08804A00u);
    require(cpu.gpr[2] == 2u, "vcount incorrect");

    // Framebuffers: sync 0 applies at once, sync 1 at the next vblank.
    const std::uint32_t edram = 0x04000000u;
    cpu.set_gpr(4u, edram); cpu.set_gpr(5u, 512u); cpu.set_gpr(6u, 3u); cpu.set_gpr(7u, 0u);
    call(runtime, 0x289D82FEu, 0x08804B00u);
    require(cpu.gpr[2] == 0u && display.framebuffer(1u, kernel.now_us()).address == edram, "immediate framebuffer not latched");
    cpu.set_gpr(4u, edram + 0x88000u); cpu.set_gpr(5u, 512u); cpu.set_gpr(6u, 3u); cpu.set_gpr(7u, 1u);
    call(runtime, 0x289D82FEu, 0x08804C00u);
    require(cpu.gpr[2] == 0u && display.framebuffer(1u, kernel.now_us()).address == edram &&
                display.framebuffer(0u, kernel.now_us()).address == edram + 0x88000u,
            "vblank-synchronised framebuffer applied early");
    const std::uint32_t out = 0x08807000u;
    cpu.set_gpr(4u, out); cpu.set_gpr(5u, out + 4u); cpu.set_gpr(6u, out + 8u); cpu.set_gpr(7u, 1u);
    call(runtime, 0xEEDA2E54u, 0x08804D00u);
    require(cpu.gpr[2] == 0u && runtime.memory().load32(out) == edram && runtime.memory().load32(out + 4u) == 512u &&
                runtime.memory().load32(out + 8u) == 3u,
            "GetFrameBuf output incorrect");
    call(runtime, 0x984C27E7u, 0x08804E00u);
    require(display.framebuffer(1u, kernel.now_us()).address == edram + 0x88000u, "framebuffer not latched at vblank");
    require(display.framebuffer_requests() == 2u && display.framebuffer_latches(kernel.now_us()) == 2u,
            "framebuffer requests or latches miscounted");

    const auto bad = [&](std::uint32_t address, std::uint32_t width, std::uint32_t format, std::uint32_t sync) {
        return static_cast<std::uint32_t>(display.set_framebuffer({address, width, format}, sync, kernel.now_us(), runtime.memory()));
    };
    require(bad(edram, 512u, 4u, 0u) == kInvalidFormat && bad(edram, 480u, 3u, 0u) == kInvalidSize &&
                bad(edram + 4u, 512u, 3u, 0u) == kInvalidPointer && bad(0x00001000u, 512u, 3u, 0u) == kInvalidPointer &&
                bad(edram, 512u, 3u, 2u) == kInvalidMode && bad(0u, 0u, 9u, 0u) == 0u,
            "framebuffer validation incorrect");
    call(runtime,0x46F186C3u,0x08804F00u);
    require(timing["wait_start"]==3&&timing["wait_vblank"]==1&&timing["get_vcount"]==1&&timing["wait_start_cb"]==1,"Timing observer missed a wait path");
    require(!sims2::kernel::configure_guest_display(0xFFFFFFFFu) && sims2::kernel::guest_display_hz==0u,
            "Invalid engine rate changed the clock");
    for (unsigned hz : {120u, 240u}) {
        require(sims2::kernel::configure_guest_display(hz), "High-rate clock rejected");
        for (std::uint64_t n=1; n<=10000; ++n) {
            const auto time=sims2::kernel::vblank_time_us(n);
            require(sims2::kernel::vblanks_by(time)==n && sims2::kernel::vblanks_by(time-1)==n-1,
                    "High-rate vblank boundaries disagree");
        }
        require(Display::accumulated_hcount(1000000u)==hz*kLinesPerFrame,
                "High-rate horizontal count mismatch");
        psprecomp::Runtime high;
        sims2::kernel::EarlyKernel high_kernel;
        high_kernel.install(high,0x08804000u,0x0A000000u);
        high_kernel.begin_main_thread(high.cpu(),"high",0x20u);
        Display high_display;
        register_display(high_display,high_kernel,high);
        call(high,0x984C27E7u,0x08804000u);
        require(high_kernel.now_us()==1000000u/hz,"High-rate wait did not advance scheduler");
        call(high,0xDBA6C4C4u,0x08804000u);
        require(high.cpu().fpr[0]==static_cast<float>(hz),"Reported display rate mismatch");
        sims2::install_game_timing(high);
        high.cpu().set_gpr(31u,0x08805000u);
        high.register_function(0x08805000u,+[](psprecomp::Runtime &rt,psprecomp::AllegrexContext &){rt.stop("test return");},"test return");
        high.run(0x08937F00u,4u);
        require(high.stop_reason()=="test return" && high.cpu().gpr[4]==0x08A2FA14u,
                "Timestep hook did not preserve its return convention");
        const auto period=std::bit_cast<float>(high.memory().load32(0x08A2FA5Cu));
        require(std::abs(period*hz-1.0f)<0.000001f &&
                std::bit_cast<float>(high.memory().load32(0x08A2FA58u))==hz,
                "Timestep and reciprocal do not agree");
    }
    require(sims2::kernel::configure_guest_display(0u) && sims2::kernel::vblank_time_us(1u)==16683u,
            "Native timing did not restore");
    psprecomp::Runtime native;
    sims2::install_game_timing(native);
    require(!native.has_function(0x08937F00u),"Native mode installed an unnecessary override");
    return 0;
}
