#pragma once
#include "psprecomp/runtime.hpp"
#include <map>
#include <array>
#include "movie/psmf_demuxer.hpp"
#include "movie/avc_decoder.hpp"
#include "audio/atrac_decoder.hpp"
#include "kernel/early_kernel.hpp"
#include <set>

namespace sims2::movie {
// MPEG ring callbacks, demux and software decoding. Presentation remains separate.
class Mpeg {
public:
    explicit Mpeg(kernel::EarlyKernel *kernel = nullptr) : kernel_(kernel) {}
    void install(psprecomp::Runtime &runtime);
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] std::uint64_t packets_read() const noexcept { return packets_read_; }
    [[nodiscard]] std::uint64_t video_frames() const noexcept { return video_frames_; }
private:
    bool initialized_{};
    struct Stream { std::uint32_t type, number; };
    struct Context {
        std::uint32_t handle, ring, frame_width, pixel_format{3u};
        std::map<std::uint32_t, Stream> streams;
        std::array<bool, 2> es_buffers{};
        struct QueuedUnit { AccessUnit unit; std::uint64_t packet; };
        PsmfDemuxer demux;
        std::deque<QueuedUnit> video_units, audio_units;
        std::map<std::uint32_t, AccessUnit> access_units;
        std::unique_ptr<AvcDecoder> decoder;
        std::unique_ptr<audio::AtracDecoder> audio_decoder;
        std::uint64_t supplied_packets{}, released_packets{}, video_consumed{}, audio_consumed{};
        std::uint32_t expected_packets{};
        bool eof{};
        bool video_in_header{true}, audio_in_header{};
    };
    std::map<std::uint32_t, Context> contexts_;
    std::uint32_t next_stream_{1u};
    struct YuvBuffer { std::uint32_t width, height, owner; Picture picture; bool ready{}; };
    std::map<std::uint32_t, YuvBuffer> yuv_buffers_;
    std::uint64_t packets_read_{}, video_frames_{};
    kernel::EarlyKernel *kernel_;
    std::set<std::uint32_t> pending_rings_;
    std::uint32_t finish_put(psprecomp::Runtime &runtime, const kernel::EarlyKernel::Action &action);
    void collect_units(Context &context);
    void release_packets(psprecomp::Runtime &runtime, Context &context);
};
}
