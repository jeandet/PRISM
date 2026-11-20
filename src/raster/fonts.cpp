#include <prism/raster/fonts.hpp>

#include "embedded_fonts.hpp"

#include <prism/core/utf8.hpp>

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

// stb_truetype with the allocator wired to std::malloc/free; the implementation goes into this
// one .cpp (STB_TRUETYPE_IMPLEMENTATION below). <cstdlib> first: the macros below expand inside
// stb's implementation, where std::malloc must already be declared.
#define STBTT_malloc(x, u) ((void)(u), std::malloc(x))
#define STBTT_free(x, u)   ((void)(u), std::free(x))
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace prism::raster {
namespace utf8 = prism::core::utf8;
using prism::render::FontStyle;

namespace {

// The glyph cache (and every metric) rounds the size to 0.5 px, so nearby sizes share bitmaps.
float cache_size(float size) { return std::round(size * 2.f) * 0.5f; }
std::uint32_t size_code(float size) {
    return static_cast<std::uint32_t>(std::lround(cache_size(size) * 2.f));
}

// unitsPerEm lives in the 'head' table at +18, big-endian; stb_truetype keeps the table offset in
// info.head but does not expose the value.
int units_per_em(const stbtt_fontinfo& info) {
    const unsigned char* head = info.data + info.head;
    return (head[18] << 8) | head[19];
}

} // namespace

struct Fonts::Impl {
    // px per em -> font units scale, per face. size is in px per em (TTF_SetFontSize semantics).
    [[nodiscard]] float scale(int face, float size) const {
        return cache_size(size) / static_cast<float>(upem[face]);
    }

    stbtt_fontinfo faces[2] = {};
    int upem[2] = {1000, 1000};
    bool ok[2] = {false, false};
    std::span<const std::uint8_t> data[2];

    mutable std::mutex mutex;
    // Key: face | size code | code point. Values are shared_ptr so a glyph handed out stays alive
    // across a clear() (pointers into an unordered_map survive a rehash but not a clear).
    mutable std::unordered_map<std::uint64_t, std::shared_ptr<const Glyph>> cache;
    static constexpr std::size_t kCacheCap = 256;   // clear-when-full, see glyph()
};

Fonts::Fonts() : Fonts(embedded::noto_sans_latin(), embedded::noto_sans_bold_latin()) {}

Fonts::Fonts(std::span<const std::uint8_t> regular, std::span<const std::uint8_t> bold)
    : impl_(std::make_unique<Impl>()) {
    impl_->data[kRegular] = regular;
    impl_->data[kBold] = bold;
    for (int f = 0; f < 2; ++f) {
        const std::span<const std::uint8_t> d = impl_->data[f];
        if (d.empty()) continue;
        stbtt_fontinfo& info = impl_->faces[f];
        if (stbtt_InitFont(&info, d.data(), stbtt_GetFontOffsetForIndex(d.data(), 0)) == 0) continue;
        impl_->upem[f] = units_per_em(info);
        impl_->ok[f] = true;
    }
}

Fonts::~Fonts() = default;

float Fonts::width(std::string_view s, float size, FontStyle style) const {
    if (s.empty()) return 0.f;
    const int face = face_of(style);
    if (!impl_->ok[face]) return prism::ui::text_width(nullptr, s, size, style);   // the 0.6 em estimate
    const float scale = impl_->scale(face, size);
    const stbtt_fontinfo& info = impl_->faces[face];
    float w = 0.f;
    std::uint32_t prev = 0;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t next = utf8::next(s, i);
        const std::uint32_t cp = decode_codepoint(s, i);
        if (prev != 0) w += stbtt_GetCodepointKernAdvance(&info, static_cast<int>(prev), static_cast<int>(cp)) * scale;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &adv, &lsb);
        w += adv * scale;
        prev = cp;
        i = next;
    }
    return w;
}

float Fonts::line_height(float size, FontStyle style) const {
    const int face = face_of(style);
    if (!impl_->ok[face]) return prism::ui::TextMeasurer::line_height(size, style);   // 1.4 * size
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&impl_->faces[face], &ascent, &descent, &line_gap);
    // The line skip, as SDL_ttf's TTF_GetFontLineSkip reports it (FreeType's height metric).
    return (ascent - descent + line_gap) * impl_->scale(face, size);
}

float Fonts::ascent(float size, FontStyle style) const {
    const int face = face_of(style);
    if (!impl_->ok[face]) return 0.8f * cache_size(size);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&impl_->faces[face], &ascent, &descent, &line_gap);
    return ascent * impl_->scale(face, size);
}

float Fonts::advance(int face, std::uint32_t codepoint, float size) const {
    if (!impl_->ok[face]) return 0.f;
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&impl_->faces[face], static_cast<int>(codepoint), &adv, &lsb);
    return adv * impl_->scale(face, size);
}

float Fonts::kern(int face, std::uint32_t prev, std::uint32_t next, float size) const {
    if (!impl_->ok[face]) return 0.f;
    return stbtt_GetCodepointKernAdvance(&impl_->faces[face], static_cast<int>(prev),
                                         static_cast<int>(next)) * impl_->scale(face, size);
}

std::shared_ptr<const Fonts::Glyph> Fonts::glyph(int face, std::uint32_t codepoint, float size) const {
    if (!impl_->ok[face]) return nullptr;
    const std::uint64_t key = (static_cast<std::uint64_t>(face) << 56)
                            | (static_cast<std::uint64_t>(size_code(size)) << 32)
                            | codepoint;
    {
        std::lock_guard lock(impl_->mutex);
        if (auto it = impl_->cache.find(key); it != impl_->cache.end()) return it->second;
    }
    // Rasterize outside the lock: stbtt only reads the (immutable) face, so concurrent
    // rasterization of the same glyph is safe and a duplicate insert is harmless (emplace keeps
    // the first). The insert itself is under the lock, keeping the cache exactly bounded.
    auto g = std::make_shared<Glyph>();
    const float scale = impl_->scale(face, size);
    stbtt_fontinfo& info = impl_->faces[face];
    const int gi = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&info, gi, &adv, &lsb);
    g->advance = adv * scale;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bmp = stbtt_GetGlyphBitmap(&info, scale, scale, gi, &w, &h, &xoff, &yoff);
    g->w = w;  g->h = h;  g->xoff = xoff;  g->yoff = yoff;
    if (bmp) {
        g->bitmap.assign(bmp, bmp + static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        stbtt_FreeBitmap(bmp, nullptr);
    }
    std::lock_guard lock(impl_->mutex);
    // simplify: clear-when-full rather than an LRU -- a 480x272 UI at a handful of text sizes
    // holds far fewer than 256 distinct glyphs, so the clear almost never fires; if a profile
    // ever shows it thrashing (many sizes at once), replace with an LRU list.
    if (impl_->cache.size() >= Impl::kCacheCap) impl_->cache.clear();
    return impl_->cache.emplace(key, std::move(g)).first->second;
}

} // namespace prism::raster
