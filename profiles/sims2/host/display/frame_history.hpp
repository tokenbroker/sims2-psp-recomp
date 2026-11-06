#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace sims2::display {
// Host presentation-call cadence, not PSP simulation rate or monitor scanout.
struct FrameHistory {
    std::array<float, 120> times{};
    unsigned sample{}, count{};
    struct Summary { float average{}, minimum{}, maximum{}, fps{}; };
    void reset_anchor() { anchored = false; }
    void observe(std::uint64_t now_us, bool active) {
        if (!active) { reset_anchor(); return; }
        if (anchored && now_us > previous_us) {
            // Keep long stalls: dropping them makes a stuttering game look smooth.
            times[sample++ % times.size()] = float(double(now_us - previous_us) / 1000.0);
            count = std::min(unsigned(times.size()), count + 1);
        }
        previous_us = now_us;
        anchored = true;
    }
    Summary summary() const {
        if (!count) return {};
        double sum{};
        Summary result;
        result.minimum = times[0];
        for (unsigned i = 0; i < count; ++i) {
            sum += times[i];
            result.minimum = std::min(result.minimum, times[i]);
            result.maximum = std::max(result.maximum, times[i]);
        }
        result.average = float(sum / count);
        result.fps = result.average > 0 ? 1000.f / result.average : 0;
        return result;
    }
private:
    std::uint64_t previous_us{};
    bool anchored{};
};
}
