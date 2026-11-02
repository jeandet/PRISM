#pragma once

#include <prism/ui/text_bidi.hpp>
#include <prism/ui/text_segmentation.hpp>
#include <prism/core/utf8.hpp>
#include <prism/render/draw_list.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
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

// Byte offset of the character boundary nearest to x, reading the text left to right (exactly
// halfway: the later one). Binary search over boundaries (prefix widths grow with the prefix), so
// O(log n) measurements. index_at() below is the same for text with right-to-left runs.
inline std::size_t logical_index_at(const TextMeasurer* m, std::string_view utf8, float size, float x,
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

namespace bidi_detail {
// A run with where it lands on screen.
struct PlacedRun {
    BidiRun run;
    float x = 0.f, w = 0.f;
};

inline std::vector<PlacedRun> place_runs(const TextMeasurer* m, std::string_view line,
                                         const std::vector<BidiRun>& runs, float size, FontRole role) {
    std::vector<PlacedRun> placed;
    float x = 0.f;
    for (const auto& r : runs) {
        float w = text_width(m, line.substr(r.begin, r.end - r.begin), size, role);
        placed.push_back({r, x, w});
        x += w;
    }
    return placed;
}

// x of the boundary at `byte` inside run `p`: after the run's start when it reads left to right,
// before its end when it reads right to left.
inline float edge_x(const TextMeasurer* m, std::string_view line, const PlacedRun& p, std::size_t byte,
                    float size, FontRole role) {
    auto [a, b] = p.run.rtl ? std::pair{byte, p.run.end} : std::pair{p.run.begin, byte};
    return p.x + text_width(m, line.substr(a, b - a), size, role);
}
} // namespace bidi_detail

// x of the caret at byte offset `byte` of one line, with right-to-left runs drawn as they are.
// The caret goes with the character before it (the one just typed), so after a Hebrew letter it
// sits on that letter's left, where the next one appears.
// simplify: logical caret only; Left/Right step through the text in memory order, which inside a
// right-to-left run moves the caret the other way on screen. Upgrade: visual movement, as most
// editors default to.
inline float caret_x_at(const TextMeasurer* m, std::string_view line, std::size_t byte, float size,
                        FontRole role = FontRole::Ui) {
    byte = core::utf8::snap(line, byte);
    auto runs = bidi_runs(line);
    if (is_plain_ltr(runs)) return prefix_width(m, line, byte, size, role);
    std::size_t probe = byte == 0 ? 0 : byte - 1;   // a byte of the character before the caret
    for (const auto& p : bidi_detail::place_runs(m, line, runs, size, role))
        if (p.run.begin <= probe && probe < p.run.end) return bidi_detail::edge_x(m, line, p, byte, size, role);
    return prefix_width(m, line, byte, size, role);   // unreachable: the runs cover the line
}

// Byte offset of the character boundary nearest to x on one line, right-to-left runs included.
inline std::size_t index_at(const TextMeasurer* m, std::string_view line, float size, float x,
                            FontRole role = FontRole::Ui) {
    auto runs = bidi_runs(line);
    if (is_plain_ltr(runs)) return logical_index_at(m, line, size, x, role);
    auto placed = bidi_detail::place_runs(m, line, runs, size, role);
    auto hit = std::find_if(placed.begin(), placed.end(), [x](const auto& p) { return x < p.x + p.w; });
    const auto& p = hit == placed.end() ? placed.back() : *hit;
    auto run_text = line.substr(p.run.begin, p.run.end - p.run.begin);
    float local = p.run.rtl ? p.w - (x - p.x) : x - p.x;   // measured from where the run starts reading
    return p.run.begin + logical_index_at(m, run_text, size, local, role);
}

// The x spans that show the selection [lo, hi) of one line: one per run it touches, so a
// selection across a direction change can be several pieces.
inline std::vector<std::pair<float, float>> selection_spans(const TextMeasurer* m, std::string_view line,
                                                            std::size_t lo, std::size_t hi, float size,
                                                            FontRole role = FontRole::Ui) {
    std::vector<std::pair<float, float>> spans;
    for (const auto& p : bidi_detail::place_runs(m, line, bidi_runs(line), size, role)) {
        std::size_t a = std::max(lo, p.run.begin), b = std::min(hi, p.run.end);
        if (a >= b) continue;
        float xa = bidi_detail::edge_x(m, line, p, a, size, role), xb = bidi_detail::edge_x(m, line, p, b, size, role);
        spans.emplace_back(std::min(xa, xb), std::max(xa, xb));
    }
    return spans;
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
