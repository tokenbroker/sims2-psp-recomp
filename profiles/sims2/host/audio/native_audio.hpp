#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace sims2::audio {
class NativeAudio {
public:
    NativeAudio();
    ~NativeAudio();
    void play(std::int32_t channel, std::span<const std::int16_t> samples, bool mono,
        std::uint32_t left, std::uint32_t right);
    void set_gain(float gain);
    static std::vector<std::int16_t> stereo_samples(std::span<const std::int16_t> samples,
        bool mono, std::uint32_t left, std::uint32_t right);
    [[nodiscard]] std::uint64_t frames_submitted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
