#include "audio/atrac_decoder.hpp"
#include "movie/avc_decoder.hpp"
#include <iostream>
int main() {
    if (!sims2::audio::AtracDecoder::available(sims2::audio::AtracCodec::Atrac3Plus)) {
        std::cerr << "FFmpeg is missing ATRAC3+ decoding required for game audio\n";
        return 1;
    }
    if (!sims2::movie::AvcDecoder::available()) {
        std::cerr << "FFmpeg is missing H.264 decoding required for game movies\n";
        return 1;
    }
    std::cout << "Required H.264 and ATRAC3+ decoders are available\n";
    std::cout << "Baseline ATRAC3 decoder: " << (sims2::audio::AtracDecoder::available(sims2::audio::AtracCodec::Atrac3) ? "available" : "not included") << '\n';
    return 0;
}
