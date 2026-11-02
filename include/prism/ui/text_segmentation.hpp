#pragma once

#include <graphemebreak.h>
#include <linebreak.h>

#include <cstddef>
#include <string_view>
#include <vector>

// Unicode text boundaries: user-perceived characters (UAX #29 graphemes) for the caret, and where
// a line may break (UAX #14) for word wrap. One interface, implemented with libunibreak (chosen
// at build time, doc/review-2026-11-02.md §4). UTF-8 in, byte offsets out.
// simplify: each call segments the whole string; a large document in a text area pays O(n) per
// caret step. Segment a window around the offset when that shows in profiles.
namespace prism::ui {

namespace segmentation_detail {
inline void init() {
    static const bool done = (init_graphemebreak(), init_linebreak(), true);   // thread-safe once
    (void)done;
}

// libunibreak's marks: one per byte, about the position after that byte.
inline std::vector<char> grapheme_marks(std::string_view s) {
    init();
    std::vector<char> marks(s.size());
    if (!s.empty())
        set_graphemebreaks_utf8(reinterpret_cast<const utf8_t*>(s.data()), s.size(), nullptr, marks.data());
    return marks;
}
} // namespace segmentation_detail

// The offset after the character that starts at `at` (s.size() at the end).
inline std::size_t next_grapheme(std::string_view s, std::size_t at) {
    if (at >= s.size()) return s.size();
    auto marks = segmentation_detail::grapheme_marks(s);
    for (std::size_t i = at; i < s.size(); ++i)
        if (marks[i] == GRAPHEMEBREAK_BREAK) return i + 1;
    return s.size();
}

// The offset where the character ending at `at` starts (0 at the start).
inline std::size_t prev_grapheme(std::string_view s, std::size_t at) {
    if (at == 0 || s.empty()) return 0;
    if (at > s.size()) at = s.size();
    auto marks = segmentation_detail::grapheme_marks(s);
    for (std::size_t i = at - 1; i-- > 0;)
        if (marks[i] == GRAPHEMEBREAK_BREAK) return i + 1;
    return 0;
}

// For each byte offset 0..s.size(): whether a line may start there -- after a space or a hyphen,
// between ideographs, at a newline, at the end; never inside a word or a character.
inline std::vector<bool> line_break_opportunities(std::string_view s) {
    segmentation_detail::init();
    std::vector<char> marks(s.size());
    if (!s.empty())
        set_linebreaks_utf8(reinterpret_cast<const utf8_t*>(s.data()), s.size(), nullptr, marks.data());
    std::vector<bool> ok(s.size() + 1, false);
    for (std::size_t i = 0; i < s.size(); ++i)
        ok[i + 1] = marks[i] == LINEBREAK_ALLOWBREAK || marks[i] == LINEBREAK_MUSTBREAK;
    if (!s.empty()) ok[s.size()] = true;
    return ok;
}

} // namespace prism::ui
