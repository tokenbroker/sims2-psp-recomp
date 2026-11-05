#pragma once
#include <cstdint>
namespace sims2::display {
// Skip presentation deadlines, never alter the PSP simulation/audio clock.
class PresentationLimiter {
    std::uint64_t next_ns{}; unsigned previous_cap{};
public:
    void reset() {next_ns=0;previous_cap=0;}
    bool due(std::uint64_t now_us,unsigned cap) {
        if(cap!=previous_cap){reset();previous_cap=cap;}
        if(!cap)return true;
        const auto now=now_us*1000;
        if(next_ns && now<next_ns)return false;
        const auto period=1000000000ull/cap;
        if(!next_ns || now>next_ns+period*4)next_ns=now;
        do{next_ns+=period;}while(next_ns<=now);
        return true;
    }
};
}
