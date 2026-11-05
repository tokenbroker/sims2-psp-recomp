#include "movie/mpeg.hpp"
#include "movie/color.hpp"
#include "psprecomp/common.hpp"
#include <array>
#include <algorithm>

namespace sims2::movie {
std::uint32_t Mpeg::finish_put(psprecomp::Runtime &rt, const kernel::EarlyKernel::Action &action) {
    const auto ring = action.arguments[0], destination = action.arguments[1], count = action.arguments[2];
    const auto result = static_cast<std::int32_t>(action.arguments[3]);
    pending_rings_.erase(ring);
    if (result < 0) return static_cast<std::uint32_t>(result);
    if (static_cast<std::uint32_t>(result) > count) throw psprecomp::Error("MPEG callback returned too many packets");
    const auto found = contexts_.find(rt.memory().load32(ring + 40u));
    if (found == contexts_.end()) throw psprecomp::Error("MPEG context removed during callback");
    auto &context = found->second; auto &mem = rt.memory();
    if (!result) { context.eof = true; context.demux.end_of_stream(); collect_units(context); return 0; }
    const auto capacity = mem.load32(ring), used = mem.load32(ring + 12u), added = static_cast<std::uint32_t>(result);
    if (used > capacity || added > capacity - used) throw psprecomp::Error("MPEG ring capacity changed during callback");
    std::array<std::uint8_t, 2048> packet{};
    for (std::uint32_t i = 0; i < added; ++i) {
        mem.copy_out(destination + i * 2048u, packet);
        ++context.supplied_packets; ++packets_read_;
        if (!context.demux.push_pack(packet)) throw psprecomp::Error("Invalid MPEG program-stream packet from guest callback");
        collect_units(context);
    }
    const auto position = (destination - mem.load32(ring + 20u)) / 2048u;
    mem.store32(ring + 8u, (position + added) % capacity);
    mem.store32(ring + 4u, mem.load32(ring + 4u) + added); mem.store32(ring + 12u, used + added);
    if (context.expected_packets && context.supplied_packets >= context.expected_packets) {
        context.eof = true; context.demux.end_of_stream(); collect_units(context);
    }
    return added;
}
void Mpeg::collect_units(Context &context) {
    while (auto unit = context.demux.pop_video()) context.video_units.push_back({std::move(*unit), context.supplied_packets});
    while (auto unit = context.demux.pop_audio()) context.audio_units.push_back({std::move(*unit), context.supplied_packets});
}
void Mpeg::release_packets(psprecomp::Runtime &runtime, Context &context) {
    const auto video = context.video_in_header ? context.video_consumed : context.supplied_packets;
    const auto audio = context.audio_in_header ? context.audio_consumed : context.supplied_packets;
    const auto consumed = std::min(video, audio);
    if (consumed > context.released_packets) {
        const auto used = runtime.memory().load32(context.ring + 12u);
        const auto amount = std::min<std::uint64_t>(used, consumed - context.released_packets);
        runtime.memory().store32(context.ring + 12u, used - static_cast<std::uint32_t>(amount));
        context.released_packets = consumed;
    }
}
void Mpeg::install(psprecomp::Runtime &runtime) {
    runtime.register_hle("sceMpeg", 0x606A4649u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end()) { cpu.gpr[2] = 0x806101FEu; return; }
            if (pending_rings_.contains(found->second.ring)) throw psprecomp::Error("Deleting MPEG during ring callback");
            rt.memory().store32(found->second.ring + 40u, 0u);
            std::erase_if(yuv_buffers_, [&cpu](const auto &entry) { return entry.second.owner == cpu.gpr[4]; });
            contexts_.erase(found); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x13407F13u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            if (!rt.memory().contains(cpu.gpr[4], 48u)) { cpu.gpr[2] = 0x80610103u; return; }
            if (pending_rings_.contains(cpu.gpr[4])) throw psprecomp::Error("Destroying active MPEG ring");
            rt.memory().zero(cpu.gpr[4], 48u); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0xF2930C9Cu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto context = contexts_.find(cpu.gpr[4]);
            auto &mem = rt.memory();
            if (context == contexts_.end() || !mem.contains(cpu.gpr[5], 4u) || !mem.contains(cpu.gpr[6], 4u)) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            const auto buffer = yuv_buffers_.find(mem.load32(cpu.gpr[5]));
            if (buffer == yuv_buffers_.end()) { cpu.gpr[2] = 0x806101FEu; return; }
            auto &yuv = buffer->second;
            yuv.ready = context->second.decoder && context->second.decoder->drain(yuv.picture);
            if (yuv.ready) ++video_frames_;
            mem.store32(cpu.gpr[6], yuv.ready ? 1u : 0u); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x31BD0272u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto context = contexts_.find(cpu.gpr[4]);
            const auto buffer = yuv_buffers_.find(cpu.gpr[5]);
            auto &mem = rt.memory();
            if (context == contexts_.end() || buffer == yuv_buffers_.end() || buffer->second.owner != cpu.gpr[4] || !buffer->second.ready ||
                !mem.contains(cpu.gpr[6], 16u)) { cpu.gpr[2] = 0x80610103u; return; }
            const auto x = mem.load32(cpu.gpr[6]), y = mem.load32(cpu.gpr[6] + 4u);
            const auto width = mem.load32(cpu.gpr[6] + 8u), height = mem.load32(cpu.gpr[6] + 12u);
            const auto stride = cpu.gpr[7], out = cpu.gpr[8], format = context->second.pixel_format;
            const auto bpp = format == 3u ? 4u : 2u;
            const auto size = height ? (static_cast<std::uint64_t>(height - 1u) * stride + width) * bpp : 0u;
            if ((x | y | width | height) & 15u || size > 0x200000u || !mem.contains(out, size)) {
                cpu.gpr[2] = 0x806101FEu; return;
            }
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size));
            // Preserve padding at the end of each row.
            mem.copy_out(out, pixels);
            if (!convert_picture(buffer->second.picture, format, stride, x, y, width, height, pixels)) {
                cpu.gpr[2] = 0x806101FEu; return;
            }
            mem.copy_in(out, pixels); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0xF0EB1125u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end() || !rt.memory().contains(cpu.gpr[6], 4u) ||
                !rt.memory().contains(cpu.gpr[7], 4u)) { cpu.gpr[2] = 0x80610103u; return; }
            auto &context = found->second;
            const auto unit = context.access_units.find(cpu.gpr[5]);
            const auto buffer = yuv_buffers_.find(rt.memory().load32(cpu.gpr[6]));
            if (buffer == yuv_buffers_.end() || buffer->second.owner != cpu.gpr[4])
                throw psprecomp::Error("Unknown movie YCbCr decode buffer");
            if (unit == context.access_units.end()) { cpu.gpr[2] = 0x80618001u; return; }
            if (!context.decoder) {
                context.decoder = std::make_unique<AvcDecoder>();
                if (!context.decoder->open()) throw psprecomp::Error("Cannot initialize movie H.264 decoder");
            }
            auto &yuv = buffer->second;
            yuv.ready = context.decoder->decode(unit->second.data, yuv.picture);
            if (yuv.ready) {
                if (yuv.picture.width > yuv.width || yuv.picture.height > yuv.height)
                    throw psprecomp::Error("Decoded movie exceeds guest YCbCr buffer");
                ++video_frames_;
            }
            rt.memory().store32(cpu.gpr[7], yuv.ready ? 1u : 0u);
            context.access_units.erase(unit); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x800C44DFu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end() || !rt.memory().contains(cpu.gpr[6], 8192u)) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            auto &context = found->second;
            const auto unit = context.access_units.find(cpu.gpr[5]);
            if (unit == context.access_units.end()) { cpu.gpr[2] = 0x80618001u; return; }
            if (cpu.gpr[7]) context.audio_decoder.reset();
            if (!context.audio_decoder) {
                context.audio_decoder = std::make_unique<audio::AtracDecoder>();
                if (!context.audio_decoder->open(audio::AtracCodec::Atrac3Plus,
                    std::max(1u, context.demux.audio_channels()),
                    static_cast<unsigned>(unit->second.data.size()), {}))
                    throw psprecomp::Error("Cannot initialize movie ATRAC3plus decoder");
            }
            std::array<std::int16_t, 4096> samples{};
            if (context.audio_decoder->decode(unit->second.data, samples.data()) != 2048u)
                throw psprecomp::Error("Movie ATRAC3plus frame decode failed");
            rt.memory().copy_in(cpu.gpr[6], std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(samples.data()), sizeof(samples)));
            context.access_units.erase(unit); cpu.gpr[2] = 0;
        });
    if (kernel_) kernel_->set_action_handler(kernel::EarlyKernel::ActionKind::MpegRingPut,
        [this, &runtime](const kernel::EarlyKernel::Action &action) { return finish_put(runtime, action); });
    runtime.register_hle("sceMpeg", 0xB240A59Eu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto ring = cpu.gpr[4];
            if (!rt.memory().contains(ring, 48u)) { cpu.gpr[2] = 0x80610103u; return; }
            const auto found = contexts_.find(rt.memory().load32(ring + 40u));
            if (found == contexts_.end()) { cpu.gpr[2] = 0x806101FEu; return; }
            auto &context = found->second; auto &mem = rt.memory();
            const auto capacity = mem.load32(ring), used = mem.load32(ring + 12u);
            if (!capacity || used > capacity || cpu.gpr[5] > 0x7FFFFFFFu || cpu.gpr[6] > 0x7FFFFFFFu) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            if (!kernel_) throw psprecomp::Error("MPEG packet callbacks require the profile kernel");
            if (pending_rings_.contains(ring)) throw psprecomp::Error("Concurrent MPEG ring writers are not modeled");
            if (context.eof) { cpu.gpr[2] = 0; return; }
            const auto position = mem.load32(ring + 8u) % capacity;
            auto count = std::min({cpu.gpr[5], cpu.gpr[6], capacity - used, capacity - position});
            if (context.expected_packets)
                count = std::min<std::uint64_t>(count, context.expected_packets - context.supplied_packets);
            if (!count) { cpu.gpr[2] = 0; return; }
            const auto destination = mem.load32(ring + 20u) + position * 2048u;
            if (!mem.contains(destination, static_cast<std::size_t>(count) * 2048u)) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            const std::array args{destination, count, mem.load32(ring + 28u)};
            kernel_->start_guest_call(cpu, mem.load32(ring + 24u), args, mem.load32(ring + 44u),
                {kernel::EarlyKernel::ActionKind::MpegRingPut, {ring, destination, count, 0u}});
            pending_rings_.insert(ring);
        });
    const auto get_au = [this](bool video) {
        return [this, video](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end() || !rt.memory().contains(cpu.gpr[6], 24u) ||
                (cpu.gpr[7] && !rt.memory().contains(cpu.gpr[7], 4u))) { cpu.gpr[2] = 0x80610103u; return; }
            auto &context = found->second;
            const auto stream = context.streams.find(cpu.gpr[5]);
            if (stream == context.streams.end() || stream->second.type != (video ? 0u : 1u)) { cpu.gpr[2] = 0x806101FEu; return; }
            auto &queue = video ? context.video_units : context.audio_units;
            if (queue.empty()) { cpu.gpr[2] = 0x80618001u; return; }
            auto queued = std::move(queue.front()); queue.pop_front();
            const auto au = cpu.gpr[6]; auto &mem = rt.memory();
            const auto timestamp = [&mem, au](unsigned offset, std::int64_t value) {
                const auto bits = static_cast<std::uint64_t>(value);
                mem.store32(au + offset, static_cast<std::uint32_t>(bits >> 32u));
                mem.store32(au + offset + 4u, static_cast<std::uint32_t>(bits));
            };
            timestamp(0, queued.unit.pts); timestamp(8, queued.unit.dts);
            mem.store32(au + 20u, static_cast<std::uint32_t>(queued.unit.data.size()));
            if (cpu.gpr[7]) mem.store32(cpu.gpr[7], 0u);
            context.access_units[au] = std::move(queued.unit);
            auto &consumed = video ? context.video_consumed : context.audio_consumed;
            consumed = context.eof && queue.empty() ? context.supplied_packets : queued.packet;
            release_packets(rt, context); cpu.gpr[2] = 0;
        };
    };
    runtime.register_hle("sceMpeg", 0xFE246728u, get_au(true));
    runtime.register_hle("sceMpeg", 0xE1CE83A7u, get_au(false));
    // The HLE owns the YCbCr representation. Guest buffers reserve a header
    // plus enough linear 4:2:0 storage; decode/CSC must go through this frontend.
    runtime.register_hle("sceMpeg", 0x211A057Cu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto mode = cpu.gpr[5], width = cpu.gpr[6], height = cpu.gpr[7], out = cpu.gpr[8];
            if (!contexts_.contains(cpu.gpr[4]) || mode > 2u || !width || !height ||
                width > 512u || height > 512u || (width & 15u) || (height & 15u) ||
                !rt.memory().contains(out, 4u)) { cpu.gpr[2] = 0x80610103u; return; }
            rt.memory().store32(out, 128u + width * height * 3u / 2u); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x67179B1Bu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            // sceMpegAvcInitYCbCr(mpeg, mode, width, height, buffer).
            const auto mode = cpu.gpr[5], width = cpu.gpr[6], height = cpu.gpr[7], buffer = cpu.gpr[8];
            if (!contexts_.contains(cpu.gpr[4]) || mode > 2u || !width || !height ||
                width > 512u || height > 512u || (width & 15u) || (height & 15u) ||
                !rt.memory().contains(buffer, 128u + width * height * 3u / 2u)) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            rt.memory().zero(buffer, 128u + width * height * 3u / 2u);
            yuv_buffers_[buffer] = {width, height, cpu.gpr[4]}; cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x167AFD9Eu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]); const auto buffer = cpu.gpr[5], out = cpu.gpr[6];
            if (found == contexts_.end() || !rt.memory().contains(out, 24u)) { cpu.gpr[2] = 0x80610103u; return; }
            const bool avc = buffer >= 1u && buffer <= 2u && found->second.es_buffers[buffer - 1u];
            if (!avc && !rt.memory().contains(buffer, 2112u)) { cpu.gpr[2] = 0x80610103u; return; }
            auto &mem = rt.memory(); mem.zero(out, 24u);
            for (unsigned i = 0; i < 4; ++i) mem.store32(out + i * 4u, 0xFFFFFFFFu);
            mem.store32(out + 16u, buffer); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0xF8DCB679u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            if (!contexts_.contains(cpu.gpr[4]) || !rt.memory().contains(cpu.gpr[5], 4u) ||
                !rt.memory().contains(cpu.gpr[6], 4u)) { cpu.gpr[2] = 0x80610103u; return; }
            rt.memory().store32(cpu.gpr[5], 2112u); rt.memory().store32(cpu.gpr[6], 8192u); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0xB5F6DC87u,
        [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto ring = cpu.gpr[4];
            if (!rt.memory().contains(ring, 48u)) { cpu.gpr[2] = 0x80610103u; return; }
            const auto capacity = rt.memory().load32(ring), used = rt.memory().load32(ring + 12u);
            cpu.gpr[2] = used <= capacity ? capacity - used : 0x80610103u;
        });
    const auto query_header = [](psprecomp::Runtime &rt, std::uint32_t buffer, std::uint32_t out, unsigned field) {
        auto &mem = rt.memory();
        if (!mem.contains(buffer, 2048u) || !mem.contains(out, 4u)) return 0x80610103u;
        const auto be32 = [&mem](std::uint32_t address) {
            return (static_cast<std::uint32_t>(mem.load8(address)) << 24u) |
                (static_cast<std::uint32_t>(mem.load8(address + 1u)) << 16u) |
                (static_cast<std::uint32_t>(mem.load8(address + 2u)) << 8u) | mem.load8(address + 3u);
        };
        const auto version = be32(buffer + 4u), offset = be32(buffer + 8u), size = be32(buffer + 12u);
        if (be32(buffer) != 0x50534D46u || version < 0x30303132u || version > 0x30303135u ||
            offset < 2048u || (offset & 2047u) || !size || (size & 2047u) ||
            static_cast<std::uint64_t>(offset) + size > 0xFFFFFFFFull) {
            mem.store32(out, 0u); return 0x806101FEu;
        }
        mem.store32(out, field == 8u ? offset : size); return 0u;
    };
    runtime.register_hle("sceMpeg", 0x21FF80E4u,
        [this, query_header](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            cpu.gpr[2] = found != contexts_.end() ? query_header(rt, cpu.gpr[5], cpu.gpr[6], 8u) : 0x806101FEu;
            if (!cpu.gpr[2]) {
                auto &context = found->second; const auto buffer = cpu.gpr[5]; auto &mem = rt.memory();
                context.expected_packets = ((static_cast<std::uint32_t>(mem.load8(buffer + 12u)) << 24u) |
                    (static_cast<std::uint32_t>(mem.load8(buffer + 13u)) << 16u) |
                    (static_cast<std::uint32_t>(mem.load8(buffer + 14u)) << 8u) | mem.load8(buffer + 15u)) / 2048u;
                const auto streams = (static_cast<unsigned>(mem.load8(buffer + 0x80u)) << 8u) | mem.load8(buffer + 0x81u);
                context.video_in_header = false; context.audio_in_header = false;
                if (streams > 64u) throw psprecomp::Error("Invalid PSMF stream table");
                for (unsigned i = 0; i < streams; ++i) {
                    const auto id = mem.load8(buffer + 0x82u + i * 16u);
                    context.video_in_header |= (id & 0xF0u) == 0xE0u;
                    context.audio_in_header |= id == 0xBDu;
                }
            }
        });
    runtime.register_hle("sceMpeg", 0x611E9E11u,
        [query_header](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            cpu.gpr[2] = query_header(rt, cpu.gpr[4], cpu.gpr[5], 12u);
        });
    runtime.register_hle("sceMpeg", 0xA11C7026u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end() || !rt.memory().contains(cpu.gpr[5], 8u)) { cpu.gpr[2] = 0x80610103u; return; }
            const auto format = rt.memory().load32(cpu.gpr[5] + 4u);
            if (format != 0xFFFFFFFFu && format > 3u) { cpu.gpr[2] = 0x806101FEu; return; }
            if (format != 0xFFFFFFFFu) found->second.pixel_format = format;
            cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x42560F23u,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end()) { cpu.gpr[2] = 0x806101FEu; return; }
            if (cpu.gpr[5] > 2u || cpu.gpr[6] > 15u) { cpu.gpr[2] = 0x806101FEu; return; }
            const auto uid = next_stream_++;
            found->second.streams.emplace(uid, Stream{cpu.gpr[5], cpu.gpr[6]}); cpu.gpr[2] = uid;
        });
    runtime.register_hle("sceMpeg", 0x591A4AA2u,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            cpu.gpr[2] = found != contexts_.end() && found->second.streams.erase(cpu.gpr[5]) ? 0u : 0x806101FEu;
        });
    runtime.register_hle("sceMpeg", 0xA780CF7Eu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]);
            if (found == contexts_.end()) { cpu.gpr[2] = 0x806101FEu; return; }
            cpu.gpr[2] = 0;
            for (unsigned i = 0; i < 2; ++i) if (!found->second.es_buffers[i]) {
                found->second.es_buffers[i] = true; cpu.gpr[2] = i + 1u; break;
            }
        });
    runtime.register_hle("sceMpeg", 0xCEB870B1u,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto found = contexts_.find(cpu.gpr[4]); const auto id = cpu.gpr[5];
            if (found == contexts_.end() || id < 1 || id > 2 || !found->second.es_buffers[id - 1]) {
                cpu.gpr[2] = 0x806101FEu; return;
            }
            found->second.es_buffers[id - 1] = false; cpu.gpr[2] = 0;
        });
    // Ring/context ABI follows the MIT VCS host's PSP MPEG bindings. Stream
    // data and decoder calls are not stubbed out: missing APIs still stop.
    runtime.register_hle("sceMpeg", 0x37295ED8u,
        [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto ring = cpu.gpr[4], packets = cpu.gpr[5], data = cpu.gpr[6], size = cpu.gpr[7];
            const auto required = static_cast<std::uint64_t>(packets) * (2048u + 104u);
            if (packets > 0x7FFFFFFFu || required > size || !rt.memory().contains(ring, 48u) ||
                !rt.memory().contains(data, static_cast<std::size_t>(packets) * 2048u)) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            auto &mem = rt.memory(); mem.zero(ring, 48u);
            mem.store32(ring, packets); mem.store32(ring + 16u, 2048u); mem.store32(ring + 20u, data);
            mem.store32(ring + 24u, cpu.gpr[8]); mem.store32(ring + 28u, cpu.gpr[9]);
            mem.store32(ring + 32u, data + packets * 2048u); mem.store32(ring + 44u, cpu.gpr[28]);
            cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0xD8C5F121u,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto out = cpu.gpr[4], data = cpu.gpr[5], size = cpu.gpr[6], ring = cpu.gpr[7];
            if (!initialized_ || size < 0x10000u || !rt.memory().contains(out, 4u) ||
                !rt.memory().contains(data, size) || !rt.memory().contains(ring, 48u)) {
                cpu.gpr[2] = 0x80610103u; return;
            }
            const auto handle = data + 0x30u; auto &mem = rt.memory();
            constexpr std::array<std::uint8_t, 12> magic{'L','I','B','M','P','E','G',0,'0','0','1',0};
            mem.copy_in(handle, magic); mem.store32(handle + 12u, 0xFFFFFFFFu);
            mem.store32(handle + 16u, ring); mem.store32(handle + 20u, mem.load32(ring + 32u));
            mem.store32(out, handle); mem.store32(ring + 40u, out);
            contexts_[out] = {handle, ring, cpu.gpr[8]}; cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x682A619Bu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            initialized_ = true; cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0x874624D6u,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            initialized_ = false; contexts_.clear(); yuv_buffers_.clear(); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceMpeg", 0xC132E22Fu,
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) { cpu.gpr[2] = 0x10000u; });
    runtime.register_hle("sceMpeg", 0xD7A29F46u,
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto packets = cpu.gpr[4];
            cpu.gpr[2] = packets > 0x7FFFFFFFu / (2048u + 104u) ?
                0x80610103u : packets * (2048u + 104u);
        });
}
}
