// SDL device/stream design adapted from Bigmission (MIT); see THIRD_PARTY.md.
#include "audio/native_audio.hpp"
#include "psprecomp/common.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace sims2::audio {
struct NativeAudio::Impl {
    SDL_AudioDeviceID device{};
    std::array<SDL_AudioStream *, 9> streams{};
    std::uint64_t frames{}, play_ns{}, max_play_ns{}, max_queued_bytes{}, waits{};
    std::array<std::uint64_t, 9> submissions{}, empty_queue_submissions{};
    bool profile = std::getenv("SIMS2_PROFILE") != nullptr;
    bool silent = std::getenv("SIMS2_AUDIO_SILENT") != nullptr;
    bool initialized{};
    ~Impl() {
        if (profile) std::cerr << "[profile] native_audio_ms=" << play_ns / 1000000.0
            << " max_audio_call_ms=" << max_play_ns / 1000000.0 << " max_queued_audio_ms=" << max_queued_bytes * 1000 / (44100 * 4)
            << " audio_backpressure_waits=" << waits << '\n';
        if (profile) for (std::size_t i = 0; i < streams.size(); ++i) {
            if (submissions[i]) std::cerr << "[profile] audio_channel=" << i
                << " submissions=" << submissions[i]
                << " empty_queue_submissions=" << empty_queue_submissions[i] << '\n';
        }
        for (auto *stream : streams) if (stream) SDL_DestroyAudioStream(stream);
        if (device) SDL_CloseAudioDevice(device);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};
NativeAudio::NativeAudio() : impl_(std::make_unique<Impl>()) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw psprecomp::Error(std::string("Audio initialization: ") + SDL_GetError());
    impl_->initialized = true;
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
    impl_->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!impl_->device) throw psprecomp::Error(std::string("Cannot open audio device: ") + SDL_GetError());
    for (auto &stream : impl_->streams) {
        stream = SDL_CreateAudioStream(&spec, nullptr);
        if (!stream || !SDL_BindAudioStream(impl_->device, stream))
            throw psprecomp::Error(std::string("Cannot create audio stream: ") + SDL_GetError());
    }
}
NativeAudio::~NativeAudio() = default;
void NativeAudio::set_gain(float gain) {
    if (!std::isfinite(gain)) throw psprecomp::Error("Invalid audio gain");
    if (!SDL_SetAudioDeviceGain(impl_->device, std::clamp(gain, 0.0f, 1.0f)))
        throw psprecomp::Error(std::string("Cannot set audio gain: ") + SDL_GetError());
}
std::uint64_t NativeAudio::frames_submitted() const noexcept { return impl_->frames; }
std::vector<std::int16_t> NativeAudio::stereo_samples(std::span<const std::int16_t> samples,
    bool mono, std::uint32_t left, std::uint32_t right) {
    if ((!mono && (samples.size() & 1u)) || left > 0xFFFFu || right > 0xFFFFu)
        throw psprecomp::Error("Invalid native audio block");
    const auto frames = samples.size() / (mono ? 1u : 2u);
    std::vector<std::int16_t> output(frames * 2u);
    for (std::size_t i = 0; i < frames; ++i) for (unsigned side = 0; side < 2; ++side) {
        const auto value = samples[i * (mono ? 1u : 2u) + (mono ? 0u : side)];
        const auto scaled = static_cast<std::int64_t>(value) * (side ? right : left) / 0x8000;
        output[i * 2u + side] = static_cast<std::int16_t>(std::clamp<std::int64_t>(scaled, -32768, 32767));
    }
    return output;
}
void NativeAudio::play(std::int32_t index, std::span<const std::int16_t> samples, bool mono,
    std::uint32_t left, std::uint32_t right) {
    const auto started = impl_->profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (index < 0 || index >= 9) throw psprecomp::Error("Invalid native audio channel");
    if (samples.empty()) return;
    auto output = stereo_samples(samples, mono, left, right);
    if (impl_->silent) std::fill(output.begin(), output.end(), 0);
    auto *stream = impl_->streams[static_cast<std::size_t>(index)];
    auto queued = SDL_GetAudioStreamQueued(stream);
    if (queued < 0) throw psprecomp::Error(std::string("Cannot query audio queue: ") + SDL_GetError());
    if (impl_->profile) {
        const auto channel = static_cast<std::size_t>(index);
        const auto available = SDL_GetAudioStreamAvailable(stream);
        if (available < 0) throw psprecomp::Error(std::string("Cannot query available audio: ") + SDL_GetError());
        // Exclude the first block. Empty submissions can also be intentional
        // silence between effects, so this is diagnostic evidence, not a count
        // of audible underruns. Count converted output as well as input PCM.
        if (impl_->submissions[channel] && queued == 0 && available == 0)
            ++impl_->empty_queue_submissions[channel];
        ++impl_->submissions[channel];
    }
    // The guest can advance while loading without presenting a framebuffer.
    // Apply playback backpressure instead of discarding sound when it runs ahead.
    // Leave 200 ms of headroom for guest block bursts and device scheduling;
    // a tight 100 ms cap stalled all guest threads while other channels drained.
    constexpr auto queue_limit_bytes = 44100 * 4 / 5;
    if (queued > queue_limit_bytes) {
        ++impl_->waits;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            SDL_Delay(2);
            queued = SDL_GetAudioStreamQueued(stream);
            if (queued < 0 || std::chrono::steady_clock::now() >= deadline)
                throw psprecomp::Error("Audio playback device stopped draining its queue");
        } while (queued > queue_limit_bytes);
    }
    if (!SDL_PutAudioStreamData(stream, output.data(), static_cast<int>(output.size() * sizeof(std::int16_t))))
        throw psprecomp::Error(std::string("Cannot queue audio: ") + SDL_GetError());
    const auto after = SDL_GetAudioStreamQueued(stream);
    if (after > 0) impl_->max_queued_bytes = std::max<std::uint64_t>(impl_->max_queued_bytes, after);
    impl_->frames += output.size() / 2u;
    if (impl_->profile) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count();
        impl_->play_ns += elapsed; impl_->max_play_ns = std::max<std::uint64_t>(impl_->max_play_ns, elapsed);
    }
}
}
