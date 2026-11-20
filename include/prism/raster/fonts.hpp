#pragma once

// TrueType faces loaded from memory (the embedded assets by default) with a bounded glyph cache,
// implementing ui::TextMeasurer -- so the metrics PRISM's widget tree lays out with are exactly the
// ones the rasterizer draws with.
//
// size semantics: px per em, the same as SDL_ttf's TTF_SetFontSize at 72 dpi, which is what the
// SDL backend passes PRISM's cmd.size to.
//
// Faces: regular and bold. simplify: FontRole::Mono and italic fall back to the regular face --
// there is no mono or italic TTF in assets/fonts/embedded/. Upgrade: add the files and map role/style to them.
//
// Thread-safety: measure() runs on PRISM's logic thread, drawing on the render thread. The
// stbtt faces are immutable once initialized, so the metric queries read them lock-free; the glyph
// cache is a mutex-guarded map of shared_ptr, so a glyph in hand stays alive however the map
// changes (the cache clears as a whole when full -- see glyph()).

#include <prism/render/draw_list.hpp>
#include <prism/ui/text_metrics.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace prism::raster {

// One UTF-8 code point at byte i; malformed input decodes as U+FFFD, one byte each (the step
// between code points is core::utf8::next, which snaps the same way).
inline std::uint32_t decode_codepoint(std::string_view s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return c;
    const int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0 || i + static_cast<std::size_t>(len) > s.size()) return 0xFFFD;
    std::uint32_t cp = c & (0xFFu >> (len + 1));
    for (int k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    return cp;
}

class Fonts final : public prism::ui::TextMeasurer {
public:
    Fonts();   // the embedded faces (embedded_fonts.hpp)
    Fonts(std::span<const std::uint8_t> regular, std::span<const std::uint8_t> bold);
    ~Fonts();
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    // ui::TextMeasurer: width is the sum of advances plus kerning, line_height the line skip.
    float width(std::string_view utf8, float size, prism::render::FontStyle font) const override;
    float line_height(float size, prism::render::FontStyle font) const override;

    static constexpr int kRegular = 0, kBold = 1;
    [[nodiscard]] static int face_of(prism::render::FontStyle s) { return s.bold ? kBold : kRegular; }

    // Metrics in px. All of them (and the glyph cache) round `size` to 0.5 px first, so measuring
    // and drawing at 14.0 and 14.2 agree exactly.
    [[nodiscard]] float ascent(float size, prism::render::FontStyle font) const;  // baseline below the box top
    [[nodiscard]] float advance(int face, std::uint32_t codepoint, float size) const;
    [[nodiscard]] float kern(int face, std::uint32_t prev, std::uint32_t next, float size) const;

    struct Glyph {
        int w = 0, h = 0;                     // bitmap size in px
        int xoff = 0, yoff = 0;               // bitmap top-left, from the pen on the baseline
        float advance = 0.f;                  // pen advance in px
        std::vector<std::uint8_t> bitmap;     // w * h, 8-bit coverage
    };
    // Looks the glyph up in the cache, rasterizing it on a miss. Nullptr when the face failed to
    // load; a code point the face lacks gives glyph 0 (.notdef).
    [[nodiscard]] std::shared_ptr<const Glyph> glyph(int face, std::uint32_t codepoint, float size) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prism::raster
