#pragma once

#include <prism/core/utf8.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace prism::ui {

// Editable text with a caret and a selection, as plain data. The selection is the span between
// `anchor` (where it started) and `cursor` (where the caret is); they are equal when nothing is
// selected. Both are byte offsets on character boundaries. Widgets map keys and clicks onto the
// pure functions below, so editing behaviour is tested without any widget.
struct TextEdit {
    std::string text;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    bool operator==(const TextEdit&) const = default;
};

namespace text_edit {

inline bool has_selection(const TextEdit& e) { return e.cursor != e.anchor; }

inline std::pair<std::size_t, std::size_t> selection(const TextEdit& e) {
    return std::minmax(e.cursor, e.anchor);
}

inline std::string_view selected(const TextEdit& e) {
    auto [lo, hi] = selection(e);
    return std::string_view(e.text).substr(lo, hi - lo);
}

// Moves the caret; `extend` keeps the anchor (shift held), otherwise the selection collapses.
inline TextEdit move_to(TextEdit e, std::size_t pos, bool extend) {
    e.cursor = core::utf8::snap(e.text, pos);
    if (!extend) e.anchor = e.cursor;
    return e;
}

inline TextEdit select_all(TextEdit e) {
    e.anchor = 0;
    e.cursor = e.text.size();
    return e;
}

inline TextEdit replace_selection(TextEdit e, std::string_view with) {
    auto [lo, hi] = selection(e);
    e.text.replace(lo, hi - lo, with);
    e.cursor = e.anchor = lo + with.size();
    return e;
}

// Types `s` over the selection. max_length (characters, 0 = no limit) applies to the result;
// `s` is cut on a character boundary to fit.
inline TextEdit insert(TextEdit e, std::string_view s, std::size_t max_length) {
    if (max_length > 0) {
        std::size_t kept = core::utf8::count(e.text) - core::utf8::count(selected(e));
        std::size_t room = kept < max_length ? max_length - kept : 0;
        s = s.substr(0, core::utf8::offset(s, room));
    }
    return replace_selection(std::move(e), s);
}

// Backspace: the selection if any, else the character before the caret.
inline TextEdit erase_back(TextEdit e) {
    if (!has_selection(e)) e.anchor = core::utf8::prev(e.text, e.cursor);
    return replace_selection(std::move(e), "");
}

// Delete: the selection if any, else the character after the caret.
inline TextEdit erase_forward(TextEdit e) {
    if (!has_selection(e)) e.anchor = core::utf8::next(e.text, e.cursor);
    return replace_selection(std::move(e), "");
}

} // namespace text_edit
} // namespace prism::ui
