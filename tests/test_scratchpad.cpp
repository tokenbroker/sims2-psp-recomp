#include "psprecomp/guest_memory.hpp"
#include "psprecomp/common.hpp"
#include <array>
#include <iostream>
#include <limits>

int main() {
    try {
        psprecomp::GuestMemory first, second;
        constexpr auto base = psprecomp::GuestMemory::kScratchpadBase;
        constexpr auto size = psprecomp::GuestMemory::kScratchpadSize;
        const auto require = [](bool value) { if (!value) throw psprecomp::Error("Scratchpad regression"); };
        require(first.contains(base, size) && !first.contains(base - 1u) && !first.contains(base + size));
        require(!first.contains(base, std::numeric_limits<std::size_t>::max()));
        auto fast = first.aot_fast_view();
        fast.aot_store32(base, 0x12345678u);
        require(first.load32(base) == 0x12345678u && second.load32(base) == 0u);
        require(fast.aot_load16(base) == 0x5678u && fast.aot_load8(base + 3u) == 0x12u);
        first.store32(base + size - 4u, 0x89ABCDEFu);
        require(fast.aot_load32(base + size - 4u) == 0x89ABCDEFu);
        require(first.raw_pointer(base, size) != nullptr && first.raw_pointer(base + size - 1u, 2u) == nullptr);
        std::array<std::uint8_t, 4> data{1, 2, 3, 4}, copied{};
        first.copy_in(base + 16u, data);
        first.copy_out(base + 16u, copied);
        require(data == copied);
        first.zero(base + 16u, 4u);
        require(first.load32(base + 16u) == 0u);
        bool rejected = false;
        try { fast.aot_store32(base + size - 2u, 0u); } catch (const psprecomp::Error &) { rejected = true; }
        require(rejected);
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
