#pragma once

#include <prism/core/utf8.hpp>
#include <prism/render/draw_list.hpp>

#include <cstddef>
#include <string_view>

namespace prism::ui {

using render::FontRole;

// Measures UTF-8 text in logical points. Backends implement it (they own the fonts) and hand it
// to the widget tree through Theme::measurer; it is only ever called on the logic thread.
// A virtual interface, unlike the compile-time Widget<T> dispatch, because the backend -- and so
// the font engine -- is chosen at runtime.
// Contract: width() never decreases as a prefix grows (advances >= 0); index_at() and fit_bytes()
// binary-search on that.
struct TextMeasurer {
    virtual ~TextMeasurer() = default;
    virtual float width(std::string_view utf8, float size, FontRole role) const = 0;
    // Distance between two baselines. Default: the estimate below, so a measurer that only
    // knows widths still lays out lines.
    virtual float line_height(float size, FontRole) const { return 1.4f * size; }
};

// Without a measurer (headless tests, backends without fonts): the bundled font is monospace
// with a 0.6 em advance, so this is exact for it.
inline float text_width(const TextMeasurer* m, std::string_view utf8, float size,
                        FontRole role = FontRole::Ui) {
    if (m) return m->width(utf8, size, role);
    return 0.6f * size * static_cast<float>(core::utf8::count(utf8));
}

inline float line_height(const TextMeasurer* m, float size, FontRole role = FontRole::Ui) {
    return m ? m->line_height(size, role) : 1.4f * size;
}

// x of a caret at byte offset `byte` (snapped to a character start).
// simplify: measures the prefix on its own, so a ligature or contextual shaping across the caret
// position can shift it by a fraction of a glyph. Upgrade: a shaper returning per-cluster advances.
inline float prefix_width(const TextMeasurer* m, std::string_view utf8, std::size_t byte, float size,
                          FontRole role = FontRole::Ui) {
    return text_width(m, utf8.substr(0, core::utf8::snap(utf8, byte)), size, role);
}

// Byte offset of the character boundary nearest to x (exactly halfway: the later one). Binary search over boundaries (prefix
// widths grow with the prefix), so O(log n) measurements.
inline std::size_t index_at(const TextMeasurer* m, std::string_view utf8, float size, float x,
                            FontRole role = FontRole::Ui) {
    std::size_t lo = 0, hi = core::utf8::count(utf8);   // in characters
    while (lo < hi) {   // first boundary whose prefix width is >= x
        std::size_t mid = (lo + hi) / 2;
        if (prefix_width(m, utf8, core::utf8::offset(utf8, mid), size, role) < x) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;
    std::size_t after = core::utf8::offset(utf8, lo), before = core::utf8::offset(utf8, lo - 1);
    float w_after = prefix_width(m, utf8, after, size, role), w_before = prefix_width(m, utf8, before, size, role);
    return (x - w_before < w_after - x) ? before : after;   // a tie rounds to the later boundary
}

// Bytes of the longest prefix that fits in max_width -- at least one character, so line
// wrapping always makes progress.
inline std::size_t fit_bytes(const TextMeasurer* m, std::string_view utf8, float size, float max_width,
                             FontRole role = FontRole::Ui) {
    std::size_t lo = 1, hi = core::utf8::count(utf8);   // answer in [1, n] characters
    if (hi == 0) return 0;
    while (lo < hi) {   // last count whose prefix fits
        std::size_t mid = (lo + hi + 1) / 2;
        if (prefix_width(m, utf8, core::utf8::offset(utf8, mid), size, role) <= max_width) lo = mid;
        else hi = mid - 1;
    }
    return core::utf8::offset(utf8, lo);
}

} // namespace prism::ui
