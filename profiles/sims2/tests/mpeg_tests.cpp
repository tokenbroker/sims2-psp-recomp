#include "movie/mpeg.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("MPEG frontend regression"); };
        psprecomp::Runtime runtime;
        sims2::movie::Mpeg mpeg; mpeg.install(runtime);
        auto &cpu = runtime.cpu(); auto &mem = runtime.memory();
        constexpr auto ring = 0x08805000u, handle = ring + 64u, au = ring + 128u;
        constexpr auto data = 0x09000000u, packets = 0x09100000u, header = 0x08808000u, out = header + 2048u;
        const auto call = [&](std::uint32_t nid) { runtime.invoke_import("sceMpeg", nid, cpu); return cpu.gpr[2]; };
        require(call(0x682A619Bu) == 0u && mpeg.initialized());
        cpu.gpr[4] = 16u; require(call(0xD7A29F46u) == 16u * 2152u);
        cpu.gpr[4] = ring; cpu.gpr[5] = 16u; cpu.gpr[6] = packets; cpu.gpr[7] = 16u * 2152u;
        cpu.gpr[8] = 0x08810000u; cpu.gpr[9] = 42u;
        require(call(0x37295ED8u) == 0u && mem.load32(ring + 24u) == 0x08810000u && mem.load32(ring + 28u) == 42u);
        cpu.gpr[4] = ring; require(call(0xB5F6DC87u) == 16u);
        cpu.gpr[4] = handle; cpu.gpr[5] = data; cpu.gpr[6] = 0x10000u; cpu.gpr[7] = ring; cpu.gpr[8] = 512u;
        require(call(0xD8C5F121u) == 0u && mem.load32(handle) == data + 0x30u && mem.load32(ring + 40u) == handle);
        cpu.gpr[4] = handle; require(call(0xA780CF7Eu) == 1u);
        require(call(0xA780CF7Eu) == 2u && call(0xA780CF7Eu) == 0u);
        cpu.gpr[5] = 1u; cpu.gpr[6] = au;
        require(call(0x167AFD9Eu) == 0u && mem.load32(au + 16u) == 1u && mem.load32(au + 20u) == 0u);
        constexpr std::array<std::uint8_t, 16> bytes{'P','S','M','F','0','0','1','4',0,0,8,0,0,0,0x10,0};
        mem.copy_in(header, bytes);
        cpu.gpr[4] = handle; cpu.gpr[5] = header; cpu.gpr[6] = out;
        require(call(0x21FF80E4u) == 0u && mem.load32(out) == 2048u);
        cpu.gpr[4] = header; cpu.gpr[5] = out;
        require(call(0x611E9E11u) == 0u && mem.load32(out) == 4096u);
        mem.store8(header, 'X');
        require(call(0x611E9E11u) == 0x806101FEu && mem.load32(out) == 0u);
        cpu.gpr[4] = 0xFFFFFFFFu;
        require(call(0xD7A29F46u) == 0x80610103u);
        cpu.gpr[4] = handle;
        require(call(0x606A4649u) == 0u && mem.load32(ring + 40u) == 0u);
        require(call(0x606A4649u) == 0x806101FEu);
        cpu.gpr[4] = ring; require(call(0x13407F13u) == 0u && mem.load32(ring) == 0u);
        call(0x874624D6u); require(!mpeg.initialized());
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
