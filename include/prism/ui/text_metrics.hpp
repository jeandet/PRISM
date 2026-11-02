#pragma once

#include <prism/ui/text_segmentation.hpp>
#include <prism/core/utf8.hpp>
#include <prism/render/draw_list.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

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

// A visual line of wrapped text: a byte range of the text.
struct LineSpan {
    size_t start;
    size_t length;
};

inline bool is_wrap_space(char c) { return c == ' ' || c == '\t'; }

// Where a visual line ends, given that `fit` bytes of `rest` fit and that a line may start at
// rest offset i when can_start[base + i] (line_break_opportunities of the logical line, `rest`
// starting `base` bytes into it):
// - the fit ends among spaces: past them -- they hang off the end of the line instead of
//   starting the next one (as in browsers and word processors);
// - else at the last place a line may start within the fit (after a space or a hyphen, between
//   ideographs: UAX #14);
// - else inside the word, which is too long for a whole line.
inline size_t word_break(std::string_view rest, size_t fit, const std::vector<bool>& can_start, size_t base) {
    if (fit >= rest.size()) return rest.size();
    if (is_wrap_space(rest[fit])) {
        size_t end = fit;
        while (end < rest.size() && is_wrap_space(rest[end])) ++end;
        return end;
    }
    for (size_t p = fit; p > 0; --p)
        if (can_start[base + p]) return p;
    return fit;
}

// Greedy word wrap: each visual line takes the longest run that fits and ends where a line may
// break (word_break).
// A line's span includes its hanging spaces, so Home/End and the caret count them; the caret
// is drawn at the edge while it is among them.
// simplify: a word too long for a line breaks at a code point, not a grapheme. Cost: O(log n)
// measured prefixes per visual line,
// each a distinct string, so very large documents churn the measurer's cache. Upgrade:
// per-character advances (one linear scan).
inline std::vector<LineSpan> wrap_lines(std::string_view text, core::Width text_area_w,
                                        const TextMeasurer* m, float size, FontRole role) {
    std::vector<LineSpan> result;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        size_t logical_end = (nl == std::string_view::npos) ? text.size() : nl;
        size_t logical_len = logical_end - pos;

        if (logical_len == 0) {
            result.push_back({pos, 0});
        } else {
            auto can_start = line_break_opportunities(text.substr(pos, logical_len));
            size_t offset = 0;
            while (offset < logical_len) {
                auto rest = text.substr(pos + offset, logical_len - offset);
                size_t chunk = word_break(rest, fit_bytes(m, rest, size, text_area_w.raw(), role), can_start, offset);
                result.push_back({pos + offset, chunk});
                offset += chunk;
            }
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    if (result.empty()) result.push_back({0, 0});
    return result;
}

} // namespace prism::ui
