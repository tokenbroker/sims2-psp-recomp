// Adapted from bigmission-recomp (MIT). See profile THIRD_PARTY.md.
#include "ctrl/controller.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sims2::ctrl;
    Controller controller;
    require(controller.set_sampling_mode(1u) == 0 && controller.set_sampling_mode(0u) == 1 &&
                controller.sampling_mode() == 0u,
            "sampling mode did not return previous mode");
    require(static_cast<std::uint32_t>(controller.set_sampling_mode(2u)) == kInvalidMode && controller.sampling_mode() == 0u,
            "invalid sampling mode accepted");
    require(controller.set_sampling_cycle(5555u) == 0 && controller.set_sampling_cycle(0u) == 5555,
            "sampling cycle did not return previous cycle");
    require(static_cast<std::uint32_t>(controller.set_sampling_cycle(5554u)) == kInvalidValue &&
                static_cast<std::uint32_t>(controller.set_sampling_cycle(20001u)) == kInvalidValue,
            "out-of-range sampling cycle accepted");
    require(static_cast<std::uint32_t>(controller.set_idle_cancel_threshold(129, 0)) == kInvalidValue &&
                controller.idle_reset() == -1,
            "out-of-range idle threshold accepted");

    psprecomp::Runtime runtime;
    sims2::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    cpu.pc = 0x08804444u;
    kernel.begin_main_thread(cpu, "main", 0x20u);
    register_ctrl(controller, kernel, runtime);
    sims2::kernel::register_early_threadman(kernel, runtime);
    cpu.set_gpr(4u, 1u);
    runtime.invoke_import("sceCtrl", 0x1F4011E6u, cpu);
    require(cpu.gpr[2] == 0u && controller.sampling_mode() == 1u, "HLE sampling mode failed");
    cpu.set_gpr(4u, 0xFFFFFFFFu); cpu.set_gpr(5u, 0xFFFFFFFFu);
    runtime.invoke_import("sceCtrl", 0xA7144800u, cpu);
    require(cpu.gpr[2] == 0u && controller.idle_reset() == -1 && controller.idle_back() == -1, "HLE idle threshold failed");
    cpu.set_gpr(4u, 16u); cpu.set_gpr(5u, 128u);
    runtime.invoke_import("sceCtrl", 0xA7144800u, cpu);
    require(cpu.gpr[2] == 0u && controller.idle_reset() == 16 && controller.idle_back() == 128, "idle threshold not stored");

    // A read with no new sample blocks until the next vblank sample.
    constexpr std::uint32_t buffer = 0x08807000u;
    controller.set_pad({0x00001000u, 0x10u, 0xF0u});
    const auto read = [&](std::uint32_t count, std::uint32_t return_address) {
        cpu.set_gpr(4u, buffer); cpu.set_gpr(5u, count);
        cpu.pc = 0x08A00000u; cpu.set_gpr(31u, return_address);
        runtime.invoke_import("sceCtrl", 0x1F803938u, cpu);
    };
    read(8u, 0x08804600u);
    auto &memory = runtime.memory();
    require(!runtime.stopped() && kernel.now_us() == 16683u && cpu.pc == 0x08804600u && cpu.gpr[2] == 1u,
            "first read did not wait for vblank");
    require(memory.load32(buffer) == 16683u && memory.load32(buffer + 4u) == 0x1000u && memory.load8(buffer + 8u) == 0x10u &&
                memory.load8(buffer + 9u) == 0xF0u && memory.load32(buffer + 12u) == 0u,
            "sample contents incorrect");
    read(8u, 0x08804700u);
    require(kernel.now_us() == 33366u && cpu.gpr[2] == 1u && memory.load32(buffer) == 33366u,
            "second read did not wait for the next vblank");

    // Samples accumulate while the thread is blocked elsewhere; a read
    // returns those since the previous read without waiting.
    cpu.set_gpr(4u, 100000u);
    cpu.pc = 0x08A00000u; cpu.set_gpr(31u, 0x08804800u);
    runtime.invoke_import("ThreadManForUser", 0xCEADEB47u, cpu);
    require(kernel.now_us() == 133366u, "delay did not advance virtual time");
    read(8u, 0x08804900u);
    require(kernel.now_us() == 133366u && cpu.gpr[2] == 5u && memory.load32(buffer) == 50050u &&
                memory.load32(buffer + 4u * kSampleSize) == 116783u,
            "buffered samples incorrect");
    require(controller.set_sampling_mode(0u) == 1, "mode reset failed");
    read(1u, 0x08804A00u);
    require(cpu.gpr[2] == 1u && memory.load8(buffer + 8u) == 0x80u, "digital mode did not centre analog stick");
    read(65u, 0x08804B00u);
    require(cpu.gpr[2] == kInvalidSize, "oversized read accepted");
    cpu.set_gpr(4u, 0u); cpu.set_gpr(5u, 1u);
    runtime.invoke_import("sceCtrl", 0x1F803938u, cpu);
    require(cpu.gpr[2] == kInvalidPointer, "invalid buffer accepted");

    // Scripts: parsing, and samples following the script at their own times.
    require(!parse_input_script("1:JUMP") && !parse_input_script("2:START;1:-") && !parse_input_script("x:START"),
            "malformed scripts rejected");
    const auto script = parse_input_script("0.05:START+CROSS;0.1:-");
    require(script && script->size() == 2u && (*script)[0].time_us == 50000u &&
                (*script)[0].pad.buttons == (kButtonStart | kButtonCross) && (*script)[1].pad.buttons == 0u,
            "script parsed");
    controller.set_pad({});
    controller.set_script(*script);
    require(controller.pad_at(49999u).buttons == 0u && controller.pad_at(50000u).buttons == 0x4008u &&
                controller.pad_at(100000u).buttons == 0u,
            "pad follows the script");
    // A game may read several PSP samples after a short tap has ended. The
    // press/release and analog values must survive in those older samples.
    Controller live;
    live.set_sampling_mode(1);
    live.set_pad({kButtonCross, 32, 200}, live.sample_time_us(2));
    live.set_pad({}, live.sample_time_us(4));
    require(live.write_samples(memory, buffer, 6, live.sample_time_us(6)) == 6,
            "live samples did not accumulate");
    for (unsigned i = 0; i < 6; ++i) {
        const bool pressed = i == 1 || i == 2;
        require(memory.load32(buffer + i * kSampleSize + 4) == (pressed ? kButtonCross : 0),
                "buffered live tap was lost or shifted");
        require(memory.load8(buffer + i * kSampleSize + 8) == (pressed ? 32 : 128) &&
                memory.load8(buffer + i * kSampleSize + 9) == (pressed ? 200 : 128),
                "buffered analog state was overwritten by release");
    }
    // Headless diagnostics can replace native input at the same guest instant.
    live.set_pad({kButtonCircle, 0, 255}, live.sample_time_us(7));
    live.set_pad({kButtonSquare, 255, 0}, live.sample_time_us(7));
    require(live.write_samples(memory, buffer, 1, live.sample_time_us(7)) == 1 &&
                memory.load32(buffer + 4) == kButtonSquare && memory.load8(buffer + 8) == 255,
            "same-time host override did not replace the sample");
    Controller delayed;
    delayed.set_sampling_mode(1);
    delayed.set_sampling_cycle(20000);
    for (unsigned i = 1; i <= 200; ++i)
        delayed.set_pad({i & 1 ? kButtonCross : kButtonCircle, static_cast<std::uint8_t>(i), 128}, i * 20000ull);
    require(delayed.write_samples(memory, buffer, 64, 4000000) == 64, "full live sample buffer was not returned");
    for (unsigned i = 0; i < 64; ++i) {
        const unsigned index = 137 + i;
        require(memory.load32(buffer + i * kSampleSize + 4) == (index & 1 ? kButtonCross : kButtonCircle) &&
                    memory.load8(buffer + i * kSampleSize + 8) == index,
                "oldest retained live samples lost their input history");
    }
    const auto stick = parse_input_script("1:STICKUP+CIRCLE;2:STICKRIGHT");
    require(stick && (*stick)[0].pad.ly == 0x00u && (*stick)[0].pad.lx == 0x80u &&
                (*stick)[0].pad.buttons == kButtonCircle && (*stick)[1].pad.lx == 0xFFu && (*stick)[1].pad.ly == 0x80u,
            "script moves the analog stick");
    return 0;
}
