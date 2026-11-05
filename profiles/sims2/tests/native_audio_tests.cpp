#include "audio/native_audio.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <vector>
int main() {
    using sims2::audio::NativeAudio;
    const std::array<std::int16_t, 4> input{-32768, 32767, 16000, -16000};
    const auto stereo = NativeAudio::stereo_samples(input, false, 0x8000, 0x8000);
    if (!std::equal(stereo.begin(), stereo.end(), input.begin())) return 1;
    const auto mono = NativeAudio::stereo_samples(input, true, 0xFFFF, 0);
    if (mono.size() != 8 || mono[0] != -32768 || mono[1] != 0 || mono[2] != 32767 ||
        mono[3] != 0 || mono[4] != 31999) return 2;
    try { NativeAudio::stereo_samples(std::span(input).first(3), false, 0, 0); return 3; }
    catch (const std::exception &) {}
    // Exercise device creation, all nine bound streams, and PCM submission
    // without depending on installed speakers or producing sound in tests.
    SDL_setenv_unsafe("SDL_AUDIO_DRIVER", "dummy",1);
    NativeAudio device;
    for (int channel = 0; channel < 9; ++channel)
        device.play(channel, input, false, 0x8000, 0x8000);
    if (device.frames_submitted() != 18) return 4;
    // A loading burst must wait for playback rather than clear queued sound.
    const std::vector<std::int16_t> burst(44100, 0); // 500 ms stereo.
    device.play(0, burst, false, 0x8000, 0x8000);
    const auto started = std::chrono::steady_clock::now();
    device.play(0, input, false, 0x8000, 0x8000);
    if (std::chrono::steady_clock::now() - started < std::chrono::milliseconds(200)) return 5;
    if (device.frames_submitted() != 22070) return 6;
    return 0;
}
