#include "font/native_font.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <vector>
#if defined(SIMS2_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#endif
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sims2::font {
namespace {
constexpr std::uint32_t invalid_handle = 0x80460002u, invalid_parameter = 0x80460003u;
constexpr std::uint32_t out_of_memory = 0x80460001u, too_many_fonts = 0x80460009u;
constexpr std::uint32_t style_size = 168u, info_size = 264u, char_info_size = 60u;
}
struct NativeFont::Impl {
    kernel::EarlyKernel &kernel;
    struct Library { std::uint32_t user, alloc, free, limit, opened{}; };
    std::map<std::uint32_t, Library> libraries;
    std::map<std::uint32_t, std::uint32_t> fonts;
#if defined(_WIN32)
    HDC dc{};
    HFONT font{};
    HGDIOBJ previous{};
    TEXTMETRICW metrics{};
#endif
#if defined(SIMS2_FREETYPE)
    FT_Library ft{};FT_Face face{};
    struct {int tmMaxCharWidth{},tmHeight{},tmAscent{},tmDescent{};} metrics;
#endif
    explicit Impl(kernel::EarlyKernel &value) : kernel(value) {
#if defined(_WIN32)
        dc = CreateCompatibleDC(nullptr);
        font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
        if (!dc || !font) {
            if (font) DeleteObject(font);
            if (dc) DeleteDC(dc);
            throw psprecomp::Error("Cannot initialize native diagnostic font");
        }
        previous = SelectObject(dc, font);
        GetTextMetricsW(dc, &metrics);
#elif defined(SIMS2_FREETYPE)
        if(FT_Init_FreeType(&ft))throw psprecomp::Error("Cannot initialize FreeType");
        auto *config=FcInitLoadConfigAndFonts();auto *pattern=FcNameParse(reinterpret_cast<const FcChar8*>("Liberation Sans"));
        if(!config||!pattern){if(pattern)FcPatternDestroy(pattern);if(config)FcConfigDestroy(config);FT_Done_FreeType(ft);ft=nullptr;throw psprecomp::Error("Cannot initialize fontconfig");}
        FcConfigSubstitute(config,pattern,FcMatchPattern);FcDefaultSubstitute(pattern);FcResult result;auto *match=FcFontMatch(config,pattern,&result);FcChar8 *file=nullptr;
        const bool found=match&&FcPatternGetString(match,FC_FILE,0,&file)==FcResultMatch;
        const bool loaded=found&&FT_New_Face(ft,reinterpret_cast<const char*>(file),0,&face)==0;
        if(match)FcPatternDestroy(match);FcPatternDestroy(pattern);FcConfigDestroy(config);
        if(!loaded||FT_Set_Pixel_Sizes(face,0,18)){if(face)FT_Done_Face(face);FT_Done_FreeType(ft);ft=nullptr;face=nullptr;throw psprecomp::Error("Install a sans-serif font (fonts-liberation or dejavu-sans-fonts)");}
        metrics={int(face->size->metrics.max_advance>>6),int(face->size->metrics.height>>6),int(face->size->metrics.ascender>>6),int(-face->size->metrics.descender>>6)};
#endif
    }
    ~Impl() {
#if defined(_WIN32)
        if (dc && previous) SelectObject(dc, previous);
        if (font) DeleteObject(font);
        if (dc) DeleteDC(dc);
#elif defined(SIMS2_FREETYPE)
        if(face)FT_Done_Face(face);if(ft)FT_Done_FreeType(ft);
#endif
    }
    std::uint32_t callback(psprecomp::Runtime &rt, const psprecomp::AllegrexContext &caller,
                           std::uint32_t address, std::uint32_t user, std::uint32_t argument) {
        auto ctx = caller;
        constexpr std::uint32_t return_pc = 0x0FFF0000u;
        ctx.pc = address;
        ctx.gpr[4] = user; ctx.gpr[5] = argument;
        ctx.gpr[29] = (ctx.gpr[29] - 0x100u) & ~15u;
        ctx.gpr[31] = return_pc;
        const auto owner = psprecomp::capture_runtime_execution_context();
        for (unsigned count = 0; count < 10000u && ctx.pc != return_pc; ++count) {
            const auto pc = ctx.pc;
            if (!rt.invoke_isolated_aot(pc, ctx) || rt.stopped() ||
                !psprecomp::runtime_execution_context_matches(owner))
                throw psprecomp::Error("Font allocator callback failed or attempted to switch threads");
        }
        if (ctx.pc != return_pc) throw psprecomp::Error("Font allocator callback exceeded dispatch budget");
        return ctx.gpr[2];
    }
    bool error(psprecomp::Runtime &rt, std::uint32_t address, std::uint32_t value) {
        if (!rt.memory().contains(address, 4u)) return false;
        rt.memory().store32(address, value); return true;
    }
    void style(psprecomp::GuestMemory &mem, std::uint32_t out) {
        mem.zero(out, style_size);
        mem.store32(out, std::bit_cast<std::uint32_t>(10.125f));
        mem.store32(out + 4u, std::bit_cast<std::uint32_t>(10.125f));
        mem.store32(out + 8u, std::bit_cast<std::uint32_t>(128.0f));
        mem.store32(out + 12u, std::bit_cast<std::uint32_t>(128.0f));
        mem.store16(out + 20u, 1u); mem.store16(out + 22u, 1u); // sans serif, regular
        mem.store16(out + 26u, 2u); mem.store16(out + 30u, 2u); // English, USA
        constexpr char name[] = "Arial (native substitute)";
        for (std::uint32_t i = 0; i < sizeof(name); ++i) mem.store8(out + 32u + i, name[i]);
    }
#if defined(_WIN32)
    struct Glyph { GLYPHMETRICS metrics{}; std::vector<std::uint8_t> bitmap; };
    Glyph glyph(std::uint32_t code) {
        Glyph result;
        MAT2 matrix{}; matrix.eM11.value = 1; matrix.eM22.value = 1;
        const auto length = GetGlyphOutlineW(dc, code & 0xFFFFu, GGO_GRAY8_BITMAP,
            &result.metrics, 0, nullptr, &matrix);
        if (length == GDI_ERROR || length > 1024u * 1024u) throw psprecomp::Error("Native font glyph lookup failed");
        result.bitmap.resize(length);
        if (length && GetGlyphOutlineW(dc, code & 0xFFFFu, GGO_GRAY8_BITMAP, &result.metrics,
            length, result.bitmap.data(), &matrix) == GDI_ERROR)
            throw psprecomp::Error("Native font glyph rasterization failed");
        return result;
    }
#elif defined(SIMS2_FREETYPE)
    struct Glyph {
        struct {unsigned gmBlackBoxX{},gmBlackBoxY{};struct {int x{},y{};} gmptGlyphOrigin;int gmCellIncX{},gmCellIncY{};} metrics;
        std::vector<std::uint8_t> bitmap;
    };
    Glyph glyph(std::uint32_t code){
        if(FT_Load_Char(face,code&0xffffu,FT_LOAD_RENDER))throw psprecomp::Error("FreeType glyph lookup failed");
        Glyph out;const auto &b=face->glyph->bitmap;auto &g=out.metrics;
        g.gmBlackBoxX=b.width;g.gmBlackBoxY=b.rows;g.gmptGlyphOrigin={face->glyph->bitmap_left,face->glyph->bitmap_top};
        g.gmCellIncX=int(face->glyph->advance.x>>6);g.gmCellIncY=int(face->glyph->advance.y>>6);
        const unsigned stride=(b.width+3)&~3u;out.bitmap.resize(stride*b.rows);
        for(unsigned y=0;y<b.rows;++y)for(unsigned x=0;x<b.width;++x){
            const auto *row=b.buffer+(b.pitch>=0?y:b.rows-1-y)*unsigned(std::abs(b.pitch));
            const unsigned value=b.pixel_mode==FT_PIXEL_MODE_MONO?((row[x/8]&(0x80>>(x%8)))?255u:0u):row[x];
            out.bitmap[y*stride+x]=std::uint8_t((value*64+127)/255);
        }return out;
    }
#endif
};

NativeFont::NativeFont(kernel::EarlyKernel &kernel) : impl_(std::make_unique<Impl>(kernel)) {}
NativeFont::~NativeFont() = default;
void NativeFont::install(psprecomp::Runtime &runtime) {
#if !defined(_WIN32) && !defined(SIMS2_FREETYPE)
    runtime.register_hle("sceLibFont", 0x67F17ED7u, [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &) {
        rt.stop("Native diagnostic font backend currently requires Windows");
    });
#else
    auto &state = *impl_;
    runtime.register_hle("sceLibFont", 0x67F17ED7u,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            auto &mem = rt.memory();
            const auto params = cpu.gpr[4], err = cpu.gpr[5];
            if (!mem.contains(params, 44u) || !state.error(rt, err, invalid_parameter)) { cpu.gpr[2] = 0; return; }
            Impl::Library lib{mem.load32(params), mem.load32(params + 12u), mem.load32(params + 16u),
                              std::min(mem.load32(params + 4u), 9u)};
            if (!lib.alloc || !lib.free) { cpu.gpr[2] = 0; return; }
            const auto handle = state.callback(rt, cpu, lib.alloc, lib.user, 256u);
            if (!handle || !mem.contains(handle, 256u)) { state.error(rt, err, out_of_memory); cpu.gpr[2] = 0; return; }
            mem.zero(handle, 256u);
            state.libraries.emplace(handle, lib);
            state.error(rt, err, 0u); cpu.gpr[2] = handle;
        });
    runtime.register_hle("sceLibFont", 0x27F6E642u,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const bool valid = state.libraries.contains(cpu.gpr[4]);
            if (!state.error(rt, cpu.gpr[5], valid ? 0u : invalid_handle)) { cpu.gpr[2] = 0; return; }
            cpu.gpr[2] = valid ? 1u : 0u;
        });
    runtime.register_hle("sceLibFont", 0xBC75D85Bu,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            if (!state.libraries.contains(cpu.gpr[4])) { cpu.gpr[2] = invalid_handle; return; }
            if (!cpu.gpr[6] || !rt.memory().contains(cpu.gpr[5], style_size)) { cpu.gpr[2] = invalid_parameter; return; }
            state.style(rt.memory(), cpu.gpr[5]); cpu.gpr[2] = 1u;
        });
    runtime.register_hle("sceLibFont", 0xA834319Du,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto library = cpu.gpr[4], err = cpu.gpr[7];
            const auto found = state.libraries.find(library);
            if (!state.error(rt, err, invalid_handle)) { cpu.gpr[2] = 0; return; }
            if (found == state.libraries.end()) { cpu.gpr[2] = 0; return; }
            if (cpu.gpr[5] != 0u) { state.error(rt, err, invalid_parameter); cpu.gpr[2] = 0; return; }
            if (found->second.opened >= found->second.limit) { state.error(rt, err, too_many_fonts); cpu.gpr[2] = 0; return; }
            const auto handle = state.kernel.allocate_uid();
            state.fonts.emplace(handle, library); ++found->second.opened;
            state.error(rt, err, 0u); cpu.gpr[2] = handle;
        });
    runtime.register_hle("sceLibFont", 0x3AEA8CB6u,
        [&state](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
            const auto found = state.fonts.find(cpu.gpr[4]);
            if (found == state.fonts.end()) { cpu.gpr[2] = invalid_handle; return; }
            --state.libraries.at(found->second).opened; state.fonts.erase(found); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceLibFont", 0x574B6FBCu,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto handle = cpu.gpr[4]; const auto found = state.libraries.find(handle);
            if (found == state.libraries.end()) { cpu.gpr[2] = invalid_handle; return; }
            const auto lib = found->second;
            state.callback(rt, cpu, lib.free, lib.user, handle);
            std::erase_if(state.fonts, [handle](const auto &entry) { return entry.second == handle; });
            state.libraries.erase(found); cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceLibFont", 0x0DA7535Eu,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto out = cpu.gpr[5]; auto &mem = rt.memory();
            if (!state.fonts.contains(cpu.gpr[4])) { cpu.gpr[2] = invalid_handle; return; }
            if (!mem.contains(out, info_size)) { cpu.gpr[2] = invalid_parameter; return; }
            mem.zero(out, info_size);
            const std::int32_t values[10] = {state.metrics.tmMaxCharWidth, state.metrics.tmHeight,
                state.metrics.tmAscent, state.metrics.tmDescent, 0, state.metrics.tmAscent,
                0, 0, state.metrics.tmMaxCharWidth, 0};
            for (unsigned i = 0; i < 10; ++i) {
                mem.store32(out + i * 4u, static_cast<std::uint32_t>(values[i] * 64));
                mem.store32(out + 40u + i * 4u, std::bit_cast<std::uint32_t>(static_cast<float>(values[i])));
            }
            mem.store16(out + 80u, static_cast<std::uint16_t>(state.metrics.tmMaxCharWidth));
            mem.store16(out + 82u, static_cast<std::uint16_t>(state.metrics.tmHeight));
            mem.store32(out + 84u, 65536u); state.style(mem, out + 92u); mem.store8(out + 260u, 4u);
            cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceLibFont", 0xDCC80C2Fu,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            const auto out = cpu.gpr[6]; auto &mem = rt.memory();
            if (!state.fonts.contains(cpu.gpr[4])) { cpu.gpr[2] = invalid_handle; return; }
            if (!mem.contains(out, char_info_size)) { cpu.gpr[2] = invalid_parameter; return; }
            const auto glyph = state.glyph(cpu.gpr[5]); const auto &g = glyph.metrics;
            mem.zero(out, char_info_size);
            const std::int32_t values[14] = {static_cast<std::int32_t>(g.gmBlackBoxX), static_cast<std::int32_t>(g.gmBlackBoxY),
                g.gmptGlyphOrigin.x, g.gmptGlyphOrigin.y, static_cast<std::int32_t>(g.gmBlackBoxX) * 64,
                static_cast<std::int32_t>(g.gmBlackBoxY) * 64, g.gmptGlyphOrigin.y * 64,
                (static_cast<std::int32_t>(g.gmBlackBoxY) - g.gmptGlyphOrigin.y) * 64,
                g.gmptGlyphOrigin.x * 64, g.gmptGlyphOrigin.y * 64, 0, 0, g.gmCellIncX * 64, g.gmCellIncY * 64};
            for (unsigned i = 0; i < 14; ++i) mem.store32(out + i * 4u, static_cast<std::uint32_t>(values[i]));
            cpu.gpr[2] = 0;
        });
    runtime.register_hle("sceLibFont", 0x980F4895u,
        [&state](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
            auto &mem = rt.memory(); const auto image = cpu.gpr[6];
            if (!state.fonts.contains(cpu.gpr[4])) { cpu.gpr[2] = invalid_handle; return; }
            if (!mem.contains(image, 24u)) { cpu.gpr[2] = invalid_parameter; return; }
            const auto format = mem.load32(image);
            const auto width = mem.load16(image + 12u), height = mem.load16(image + 14u);
            const auto pitch = mem.load16(image + 16u);
            const auto address = mem.load32(image + 20u);
            const auto x = static_cast<std::int32_t>(mem.load32(image + 4u)) >> 6;
            const auto y = static_cast<std::int32_t>(mem.load32(image + 8u)) >> 6;
            const std::uint32_t row_bytes = format < 2u ? (width + 1u) / 2u : width * (format == 2u ? 1u : format == 3u ? 3u : 4u);
            if (format > 4u || pitch < row_bytes || !mem.contains(address, static_cast<std::size_t>(pitch) * height)) {
                cpu.gpr[2] = invalid_parameter; return;
            }
            const auto glyph = state.glyph(cpu.gpr[5]); const auto &g = glyph.metrics;
            const auto stride = (g.gmBlackBoxX + 3u) & ~3u;
            for (unsigned gy = 0; gy < g.gmBlackBoxY; ++gy) for (unsigned gx = 0; gx < g.gmBlackBoxX; ++gx) {
                const auto px = x + static_cast<int>(gx), py = y + static_cast<int>(gy);
                if (px < 0 || py < 0 || px >= width || py >= height) continue;
                const auto coverage = static_cast<std::uint8_t>(std::min(64u, static_cast<unsigned>(glyph.bitmap.at(gy * stride + gx))) * 255u / 64u);
                const auto row = address + static_cast<unsigned>(py) * pitch;
                if (format < 2u) {
                    const auto dst = row + static_cast<unsigned>(px) / 2u;
                    const auto shift = ((static_cast<unsigned>(px) & 1u) ^ format) * 4u;
                    mem.store8(dst, static_cast<std::uint8_t>((mem.load8(dst) & ~(15u << shift)) | ((coverage >> 4u) << shift)));
                } else {
                    const auto bytes = format == 2u ? 1u : format == 3u ? 3u : 4u;
                    for (unsigned c = 0; c < bytes; ++c) mem.store8(row + static_cast<unsigned>(px) * bytes + c, coverage);
                }
            }
            cpu.gpr[2] = 0;
        });
#endif
}
}
